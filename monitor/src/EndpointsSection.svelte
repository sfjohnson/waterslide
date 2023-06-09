<script>
  import TimingGraph from './TimingGraph.svelte'
  import SyncGraph from './SyncGraph.svelte'

  export let data = []
</script>

<div class="container">
  <h1><span>endpoints</span></h1>
  <div class="sub-container">
    {#each data as endpoint}
      <div class="endpoint-container">
        <h2><span>{endpoint.interfaceName}</span></h2>
        <TimingGraph />
        <div class="stats-container">
          <div class="entry">
            <div class="label">up:</div>
            <div class="value">{endpoint.open}</div>
          </div>
          <div class="entry">
            <div class="label">sent:</div>
            <div class="value">{`${(endpoint.bytesOut / 1000000).toFixed(2)} MB`}</div>
          </div>
          <div class="entry">
            <div class="label">received:</div>
            <div class="value">{`${(endpoint.bytesIn / 1000000).toFixed(2)} MB`}</div>
          </div>
          <div class="entry">
            <div class="label">duplicated packets:</div>
            <div class="value">{endpoint.dupPacketCount}</div>
          </div>
          <div class="entry">
            <div class="label">out-of-order packets:</div>
            <div class="value">{endpoint.oooPacketCount}</div>
          </div>
          <div class="entry">
            <div class="label">send congestion:</div>
            <div class="value">{endpoint.sendCongestion}</div>
          </div>
        </div>
      </div>
    {/each}
  </div>
  <div class="sub-container">
    <div class="sync-container">
      <h2><span>SBN sync</span></h2>
      <SyncGraph data={data} />
    </div>
  </div>
</div>

<style>
  .container {
    /* background-color: rgb(230, 230, 230); */
    display: flex;
    flex-direction: column;
    margin-top: 20px;
  }

  .sub-container {
    display: flex;
  }

  .endpoint-container {
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    margin-right: 10px;
  }

  .sync-container {
    display: flex;
    flex-direction: column;
    align-items: flex-start;
    margin-top: 10px;
  }

  .endpoint-container:last-child {
    margin-right: 0px;
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

  h2 {
    width: 100%;
    font-size: 16px;
    font-weight: normal;
    margin: 0px 0px 10px 0px;
    background: linear-gradient(white 0%, white 48%, black 49%, black 52%, white 53%, white 100%);
  }

  h2 span {
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
    min-width: 100px;
  }
</style>
