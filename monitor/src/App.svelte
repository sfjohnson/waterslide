<script>
  import AudioMeter from './AudioMeter.svelte'
  import StreamMeter from './StreamMeter.svelte'
  import StreamStats from './StreamStats.svelte'
  import protobuf from 'protobufjs'

  const wsServerAddr = "ws://localhost:7681"

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
  wsClient.addEventListener('open', async (event) => {
    const proto = (await protobuf.load('./monitor.proto')).lookupType('MonitorProto')

    wsClient.send('Hello Server!')
    wsClient.onmessage = async (event) => {
      if (!(event.data instanceof Blob)) return

      const msgCh1 = proto.decode(new Uint8Array(await event.data.arrayBuffer())).muxChannel[0]
      currentState.audioLevelsFast[0] = msgCh1.audioStats.audioChannel[0].levelFast
      currentState.audioLevelsFast[1] = msgCh1.audioStats.audioChannel[1].levelFast
      currentState.audioLevelsSlow[0] = msgCh1.audioStats.audioChannel[0].levelSlow
      currentState.audioLevelsSlow[1] = msgCh1.audioStats.audioChannel[1].levelSlow
      currentState.audioClippingCount[0] = msgCh1.audioStats.audioChannel[0].clippingCount
      currentState.audioClippingCount[1] = msgCh1.audioStats.audioChannel[1].clippingCount
      currentState.streamBufferPos = msgCh1.audioStats.streamBufferPos
      currentState.bufferOverrunCount = msgCh1.audioStats.bufferOverrunCount
      currentState.bufferUnderrunCount = msgCh1.audioStats.bufferUnderrunCount
      currentState.dupBlockCount = msgCh1.dupBlockCount
      currentState.oooBlockCount = msgCh1.oooBlockCount
      currentState.codecErrorCount = msgCh1.audioStats.codecErrorCount
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
