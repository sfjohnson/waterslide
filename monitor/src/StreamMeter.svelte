<script>
  export let bufferSize = 1024
  export let bufferPos = 0
  export let underrunCount = 0
  export let overrunCount = 0

  const meterHeight = 300
  const levelDiv = 8
  const endMarkerHeight = 3
  const activeMeterHeight = meterHeight - 2*endMarkerHeight

  let markers = [
    { active: false, prevCount: 0, timeout: null }, // underrun
    { active: false, prevCount: 0, timeout: null } // overrun
  ]

  $: if (underrunCount || overrunCount) {
    for (let i = 0; i < markers.length; i++) {
      const marker = markers[i]
      const currentCount = i === 0 ? underrunCount : overrunCount
      if (currentCount > marker.prevCount) {
        clearTimeout(marker.timeout)
        marker.active = true
        marker.timeout = setTimeout(() => {
          marker.active = false
          markers = markers
        }, 4000)
      }
      marker.prevCount = currentCount
    }
    markers = markers
  }

  $: levelMarkers = Array.from(new Array(levelDiv + 1), (_, i) => {
    return {
      label: (levelDiv-i)*bufferSize / levelDiv,
      pos: endMarkerHeight + activeMeterHeight*i / levelDiv
    }
  })

  $: redZoneHeight = activeMeterHeight / levelDiv

  $: posMarkerTop = activeMeterHeight * (1 - bufferPos/(bufferSize-1)) + endMarkerHeight
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

  <div class="meter-bar">
    <div class="{markers[1].active ? "active-marker marker" : "marker"}"></div>
    <div class="red-zone" style="height: {redZoneHeight}px;"></div>
    <div class="fill"></div>
    <div class="red-zone" style="height: {redZoneHeight}px;"></div>
    <div class="pos-marker" style="top: {posMarkerTop}px;"></div>
    <div class="{markers[0].active ? "active-marker marker" : "marker"}"></div>
  </div>
</div>

<style>
  .container {
    display: flex;
    margin-left: 50px;
  }

  .meter-bar {
    display: flex;
    justify-content: flex-end;
    flex-direction: column;
    width: 10px;
    height: 300px;
    margin-right: 3px;
    background-color: black;
    position: relative;
  }

  .marker {
    width: 10px;
    height: 3px;
    background-color: #ccc;
  }

  .active-marker {
    background-color: red;
  }

  .fill {
    flex: 1;
  }

  .red-zone {
    width: 10px;
    background-color: rgb(145, 59, 59);
  }

  .pos-marker {
    width: 10px;
    height: 1px;
    background-color: rgb(74, 216, 74);
    position: absolute;
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
