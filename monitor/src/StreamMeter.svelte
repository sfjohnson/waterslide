<script lang="ts">
  import ShaderBox from './ShaderBox.svelte'
  import meterBoxFrag from '../shaders/stream-meter.frag'
  import meterBoxVert from '../shaders/basic.vert'

  export let data: App.AudioStats = {}

  const meterHeight = 300
  const levelDiv = 8
  const endMarkerHeight = 3
  const activeMeterHeight = meterHeight - 2*endMarkerHeight

  let markers = [
    { prevCount: 0, timeout: null }, // underrun
    { prevCount: 0, timeout: null } // overrun
  ]

  let meterBoxUniforms = null

  const setMarkerUniform = (index: number, active: boolean) => {
    if (meterBoxUniforms === null) return

    if (index === 0) {
      meterBoxUniforms.uUnderrun = active ? 1.0 : 0.0
    } else {
      meterBoxUniforms.uOverrun = active ? 1.0 : 0.0
    }
  }

  $: if (meterBoxUniforms === null && data.streamMeterBins) {
    meterBoxUniforms = {
      uBins: {
        buffer: new Uint8Array(data.streamMeterBins.length),
        length: data.streamMeterBins.length
      },
      uUnderrun: 0.0,
      uOverrun: 0.0
    }
  }

  $: if (meterBoxUniforms !== null) {
    // copying data.streamMeterBins is necessary because it might be unaligned (to 4 bytes) due to the way protobuf parsed it
    const bufferCopy = new Uint8Array(data.streamMeterBins.length)
    bufferCopy.set(data.streamMeterBins)
    meterBoxUniforms.uBins.buffer = bufferCopy
  }

  $: bufferSize = data.streamBufferSize || 1024

  $: if (data.bufferUnderrunCount || data.bufferOverrunCount) {
    for (let i = 0; i < markers.length; i++) {
      const marker = markers[i]
      const currentCount = i === 0 ? data.bufferUnderrunCount : data.bufferOverrunCount
      if (currentCount > marker.prevCount) {
        clearTimeout(marker.timeout)
        setMarkerUniform(i, true)
        marker.timeout = setTimeout((markerIndex) => {
          setMarkerUniform(markerIndex, false)
        }, 4000, i)
      }
      marker.prevCount = currentCount
    }
  }

  $: levelMarkers = Array.from(new Array(levelDiv + 1), (_, i) => {
    return {
      label: (levelDiv-i)*bufferSize / levelDiv,
      pos: endMarkerHeight + activeMeterHeight*i / levelDiv
    }
  })
</script>

<div class="container">
  <div class="markers-container">
    {#each levelMarkers as marker}
      <div class="level-marker" style="top: {marker.pos}px;">
        <p>{marker.label}</p>
        <div class="dash"></div>
      </div>
    {/each}
  </div>

  <ShaderBox
    class="meter-box"
    width={10}
    height={300}
    vertShaderCode={meterBoxVert}
    fragShaderCode={meterBoxFrag}
    uniforms={meterBoxUniforms}
  />
</div>

<style>
  .container {
    display: flex;
    margin-left: 50px;
  }

  .markers-container {
    width: 30px;
    margin-right: 5px;
    display: flex;
    justify-content: flex-end;
    position: relative;
  }

  .level-marker {
    display: flex;
    align-items: center;
    position: absolute;
    transform: translateY(-50%);
  }

  .level-marker .dash {
    width: 6px;
    height: 1px;
    background-color: black;
  }

  .level-marker p {
    font-size: 12px;
    margin: 0px 3px 0px 0px;
  }
</style>
