<script>
  import AudioMeter from './AudioMeter.svelte'
  import StreamMeter from './StreamMeter.svelte'

  export let config = {
    streamBufferSize: 1024
  }

  export let data = {}
</script>

<div class="container">
  <h1><span>audio</span></h1>
  <div class="sub-container">
    <AudioMeter data={data.audioChannel} />
    <StreamMeter
      bufferSize={config.streamBufferSize}
      data={data}
    />
    <div class="stats-container">
      <div class="entry">
        <div class="label">clipping (total):</div>
        <div class="value">{data.audioChannel && data.audioChannel.reduce((prev, current) => prev + current.clippingCount, 0)}</div>
      </div>
      <div class="entry">
        <div class="label">buffer overruns:</div>
        <div class="value">{data.bufferOverrunCount}</div>
      </div>
      <div class="entry">
        <div class="label">buffer underruns:</div>
        <div class="value">{data.bufferUnderrunCount}</div>
      </div>
      <div class="entry">
        <div class="label">codec errors:</div>
        <div class="value">{data.codecErrorCount}</div>
      </div>
    </div>
  </div>
</div>

<style>
  .container {
    /* background-color: rgb(230, 230, 230); */
    display: flex;
    flex-direction: column;
    margin-right: 20px;
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
    margin-left: 50px;
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