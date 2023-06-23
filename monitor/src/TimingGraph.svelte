<script lang="ts">
  import ShaderBox from './ShaderBox.svelte'
  import fragShader from '../shaders/timing-graph.frag'
  import vertShader from '../shaders/basic.vert'

  // a higher TIMING_SCALE shows more detail of small timing variations
  const TIMING_SCALE = 160.0

  export let data: Uint8Array
  export let totalTimeS: number // in seconds
  export let maxRelTimeMs: number // in milliseconds
  let relTimes = null
  let uniforms = null

  const updateRelTimes = (rawData: Uint8Array) => {
    if (relTimes === null) {
      const relTimesCount = rawData.length/4 - 1
      // const uBinsWidth = Math.floor(relTimesCount / RESOLUTION_RATIO)
      relTimes = new Array(relTimesCount)
      uniforms = {
        uBins: {
          pixels: new Uint8ClampedArray(4 * relTimesCount),
          width: relTimesCount,
          height: 1
        }
      }
    } else if (relTimes.length !== rawData.length/4 - 1) {
      return // rawData must always be the same length
    }

    // rawData is a Uint8Array but we need to re-interpret it as an array of uint32 values
    let lastAbsTimeUs
    let minRelTime = null, avgRelTime, maxRelTime
    let firstAbsTimeUs = null
    let relTimesCount = 0
    for (let i = 0; i < rawData.length/4; i++) {
      const absTimeUs = rawData[4*i] | (rawData[4*i+1] << 8) | (rawData[4*i+2] << 16) | (rawData[4*i+3] << 24)
      if (absTimeUs === 0) {
        // no data yet, rawData is not full
        continue
      }

      if (firstAbsTimeUs === null) {
        firstAbsTimeUs = absTimeUs
      } else {
        let relTime = absTimeUs - lastAbsTimeUs
        if (relTime < 0) relTime += 1000000000 // overflow
        if (minRelTime === null) {
          minRelTime = avgRelTime = maxRelTime = relTimes[0] = relTime
        } else {
          relTimes[i-1] = relTime
          avgRelTime += relTimes[i-1]
          if (relTimes[i-1] < minRelTime) minRelTime = relTimes[i-1]
          else if (relTimes[i-1] > maxRelTime) maxRelTime = relTimes[i-1]
        }
        relTimesCount++
      }

      lastAbsTimeUs = absTimeUs
    }

    if (relTimesCount === 0 || minRelTime === maxRelTime) return // check for division by zero

    avgRelTime /= relTimesCount
    if (relTimesCount === rawData.length/4 - 1) { // full data
      totalTimeS = lastAbsTimeUs - firstAbsTimeUs
      if (totalTimeS < 0) totalTimeS += 1000000000 // overflow
      totalTimeS *= 0.000001 // to seconds
      maxRelTimeMs = maxRelTime / 1000.0
    }

    for (let i = 0; i < relTimesCount; i++) {
      const index = i + rawData.length/4 - 1 - relTimesCount
      const mappedRelTime = TIMING_SCALE * (relTimes[index] - minRelTime) / (avgRelTime - minRelTime)
      // Red channel is mapped relTime value, green channel is value unset (0) / value set (255)
      uniforms.uBins.pixels[4*index] = mappedRelTime
      uniforms.uBins.pixels[4*index + 1] = 255
    }
  }

  $: if (data) updateRelTimes(data)
  
</script>

<div>
  <ShaderBox
    class="timing-graph"
    width={300}
    height={30}
    vertShaderCode={vertShader}
    fragShaderCode={fragShader}
    uniforms={uniforms}
  />
</div>
