<script lang="ts">
  import TimingGraph from './TimingGraph.svelte'

  export let data: App.MonitorData = {}
  let totalTimeS: number // in seconds
  let maxRelTimeMs: number // in milliseconds
</script>

<div class="container">
  <h1><span>blocks</span></h1>
  <div class="sub-container">
    <TimingGraph bind:totalTimeS={totalTimeS} bind:maxRelTimeMs={maxRelTimeMs} data={data.blockTiming} />
    <div class="stats-container">
      <div class="entry">
        <div class="label">duplicated:</div>
        <div class="value">{data.dupBlockCount}</div>
      </div>
      <div class="entry">
        <div class="label">out-of-order:</div>
        <div class="value">{data.oooBlockCount}</div>
      </div>
      <div class="entry">
        <div class="label">max gap (last {(totalTimeS || 0).toFixed(1)} s):</div>
        <div class="value">{(maxRelTimeMs || 0).toFixed(1)} ms</div>
      </div>
    </div>
  </div>
</div>

<style>
  .container {
    display: flex;
    flex-direction: column;
    margin-bottom: 20px;
  }

  .sub-container {
    display: flex;
    flex-direction: column;
  }

  h1 {
    font-size: 26px;
    font-weight: normal;
    margin: 0px 0px 10px 0px;
    background: linear-gradient(white 0%, white 49%, black 50%, black 51%, white 52%, white 100%);
  }

  h1 span {
    background: white;
    padding: 0px 5px 0px 0px;
  }

  .stats-container {
    display: flex;
    flex-direction: column;
    font-size: 16px;
    margin-top: 10px;
  }

  .entry {
    margin-bottom: 10px;
    display: flex;
  }

  .entry .label {
    flex: 1;
    margin-right: 10px;
  }

  .entry .value {
    min-width: 135px;
  }
</style>
