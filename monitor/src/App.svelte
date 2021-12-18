<script>
  import AudioMeter from './AudioMeter.svelte'
  import StreamMeter from './StreamMeter.svelte'
  import StreamStats from './StreamStats.svelte'

  const wsServerAddr = "ws://192.168.1.101:7681"

  const config = {
    audioChannelCount: 2,
    streamBufferSize: 8192,
    statsLabels: [
      { name: 'dupBlockCount', label: 'duplicate blocks' },
      { name: 'oooBlockCount', label: 'out-of-order blocks' },
      { name: 'codecErrorCount', label: 'codec errors' },
      { name: 'bufferUnderrunCount', label: 'buffer underruns' },
      { name: 'bufferOverrunCount', label: 'buffer overruns' },
    ]
  }

  const currentState = {
    audioLevelsFast: [0.0, 0.0], // [0.501, 0.00794] -6 dB, -42 dB
    audioLevelsSlow: [0.0, 0.0],
    audioClippingCount: [0, 0],
    streamBufferPos: config.streamBufferSize / 2,
    bufferOverrunCount: 0,
    bufferUnderrunCount: 0,
    dupBlockCount: 0,
    oooBlockCount: 0,
    codecErrorCount: 0
  }

  const wsClient = new WebSocket(wsServerAddr)
  wsClient.addEventListener('open', (event) => {
    wsClient.send('Hello Server!')
    wsClient.onmessage = async (event) => {
      if (!(event.data instanceof Blob)) return

      const view = new DataView(await event.data.arrayBuffer());
      if (view.byteLength !== 64) return
      currentState.audioLevelsFast[0] = view.getFloat64(0, true);
      currentState.audioLevelsFast[1] = view.getFloat64(8, true);
      currentState.audioLevelsSlow[0] = view.getFloat64(16, true);
      currentState.audioLevelsSlow[1] = view.getFloat64(24, true);
      currentState.audioClippingCount[0] = view.getInt32(32, true);
      currentState.audioClippingCount[1] = view.getInt32(36, true);
      currentState.streamBufferPos = view.getInt32(40, true);
      currentState.bufferOverrunCount = view.getInt32(44, true);
      currentState.bufferUnderrunCount = view.getInt32(48, true);
      currentState.dupBlockCount = view.getInt32(52, true);
      currentState.oooBlockCount = view.getInt32(56, true);
      currentState.codecErrorCount = view.getInt32(60, true);
    }
  })
</script>

<main>
  <AudioMeter
    channelCount={config.audioChannelCount}
    levelsLinearFast={currentState.audioLevelsFast}
    levelsLinearSlow={currentState.audioLevelsSlow}
    clippingCount={currentState.audioClippingCount}
  />
  <StreamMeter
    bufferSize={config.streamBufferSize}
    bufferPos={currentState.streamBufferPos}
    underrunCount={currentState.bufferUnderrunCount}
    overrunCount={currentState.bufferOverrunCount}
  />
  <StreamStats
    labels={config.statsLabels}
    data={currentState}
  />
</main>

<style>
  :global(body) {
    margin: 10px;
    font-family: sans-serif;
  }

  main {
    display: inline-flex;
    padding: 20px;
    background-color: rgb(248, 227, 239);
  }
</style>
