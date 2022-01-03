<script>
  import TimingGraph from './TimingGraph.svelte'

  export let data = {}
</script>

<div class="container">
  <h1><span>blocks</span></h1>
  <div class="sub-container">
    <TimingGraph />
    <div class="stats-container">
      <div class="entry">
        <div class="label">duplicated:</div>
        <div class="value">{data.dupBlockCount}</div>
      </div>
      <div class="entry">
        <div class="label">out-of-order:</div>
        <div class="value">{data.oooBlockCount}</div>
      </div>
    </div>
  </div>
</div>

<style>
  .container {
    /* background-color: rgb(230, 230, 230); */
    display: flex;
    flex-direction: column;
  }

  .sub-container {
    display: flex;
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
    margin-left: 20px;
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
    min-width: 100px;
  }
</style>
