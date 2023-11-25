<!--
  Copyright 2023 Sam Johnson
  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at https://mozilla.org/MPL/2.0/.
-->

<script lang="ts">
  import ShaderBox from './ShaderBox.svelte'
  import audioBoxFrag from '../shaders/audio-meter.frag'
  import audioBoxVert from '../shaders/basic.vert'

  const MAX_AUDIO_CHANNELS = 64

  export let data: App.AudioChannel[] = []

  let clipping = []

  const clippingMarkerTime = 4000 // ms
  const meterHeight = 300
  const zeroMarkerHeight = 3
  const pixelsPerDb = 4.6
  const markerIntervalDb = 6
  const levelMarkerCount = 11

  let audioBoxUniforms = {
    uLevels: {
      values: new Array(MAX_AUDIO_CHANNELS),
      length: 0
    },
    uPeaks: {
      values: new Array(MAX_AUDIO_CHANNELS),
      length: 0
    },
    uClipping: {
      values: new Array(MAX_AUDIO_CHANNELS),
      length: 0
    }
  }

  $: levelMarkers = Array.from(new Array(levelMarkerCount), (_, i) => {
    return {
      label: markerIntervalDb * i,
      pos: zeroMarkerHeight + pixelsPerDb * markerIntervalDb * i
    }
  })

  $: for (let i = 0; i < data.length; i++) {
    audioBoxUniforms.uLevels.length = data.length
    audioBoxUniforms.uPeaks.length = data.length
    audioBoxUniforms.uLevels.values[i] = -20.0 * pixelsPerDb*Math.log10(data[i].levelSlow) / (meterHeight-zeroMarkerHeight)
    audioBoxUniforms.uPeaks.values[i] = -20.0 * pixelsPerDb*Math.log10(data[i].levelFast) / (meterHeight-zeroMarkerHeight)

    let entry = clipping[i]
    if (!entry) entry = clipping[i] = { active: false, prevCount: 0, timeout: null }
    if (data[i].clippingCount > entry.prevCount) {
      clearTimeout(entry.timeout)
      entry.active = true
      audioBoxUniforms.uClipping.values[i] = 1.0
      entry.timeout = setTimeout(() => {
        entry.active = false
        audioBoxUniforms.uClipping.values[i] = 0.0
        clipping = clipping
      }, clippingMarkerTime)
    }
    entry.prevCount = data[i].clippingCount
    clipping = clipping
  }
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
    class="meters"
    width={15 * data.length}
    height={300}
    vertShaderCode={audioBoxVert}
    fragShaderCode={audioBoxFrag}
    uniforms={audioBoxUniforms}
  />
</div>

<style>
  .container {
    display: flex;
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
