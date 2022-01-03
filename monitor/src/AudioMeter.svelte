<script>
  export let data = []

  const levelHeights = []
  const peakHeights = []
  let clipping = []

  const clippingMarkerTime = 4000 // ms
  const meterHeight = 300
  const zeroMarkerHeight = 3
  const pixelsPerDb = 4.6
  const markerIntervalDb = 6
  const levelMarkerCount = 11

  $: levelMarkers = Array.from(new Array(levelMarkerCount), (_, i) => {
    return {
      label: markerIntervalDb * i,
      pos: zeroMarkerHeight + pixelsPerDb * markerIntervalDb * i
    }
  })

  $: for (let i = 0; i < data.length; i++) {
    levelHeights[i] = (meterHeight-zeroMarkerHeight) + 20 * pixelsPerDb * Math.log10(data[i].levelSlow)
    peakHeights[i] = (meterHeight-zeroMarkerHeight) + 20 * pixelsPerDb * Math.log10(data[i].levelFast)

    let entry = clipping[i]
    if (!entry) entry = clipping[i] = { active: false, prevCount: 0, timeout: null }
    if (data[i].clippingCount > entry.prevCount) {
      clearTimeout(entry.timeout)
      entry.active = true
      entry.timeout = setTimeout(() => {
        entry.active = false
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
  {#each data as _, i}
    <div class="meter-bar">
      <div class="{clipping[i].active ? "clipping clipping-marker" : "clipping-marker"}"></div>
      <div class="fill"></div>
      <div class="level" style="height: {levelHeights[i]}px;"></div>
      <div class="peak-marker" style="bottom: {peakHeights[i]}px;"></div>
    </div>
  {/each}
</div>

<style>
  .container {
    display: flex;
  }

  .meter-bar {
    display: flex;
    position: relative;
    justify-content: flex-end;
    flex-direction: column;
    width: 10px;
    height: 300px;
    margin-right: 3px;
    background-color: black;
  }

  .clipping-marker {
    width: 10px;
    height: 3px;
    background-color: #ccc;
  }

  .clipping {
    background-color: red;
  }

  .fill {
    flex: 1;
  }

  .peak-marker {
    width: 10px;
    height: 1px;
    background-color: yellow;
    position: absolute;
  }

  .level {
    width: 10px;
    background-color: rgb(74, 216, 74);
    position: absolute;
    /* transition: height 0.2s linear; */
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
