<script>
  import AudioSection from './AudioSection.svelte'
  import BlocksSection from './BlocksSection.svelte'
  import EndpointsSection from './EndpointsSection.svelte'
  import protobuf from 'protobufjs'

  const wsServerAddr = 'ws://localhost:7681'

  const config = {
    streamBufferSize: 8192
  }

  let currentState = {}

  // let currentState = {
  //   dupBlockCount: 0,
  //   oooBlockCount: 0,
  //   endpoint: [
  //     {
  //       interface: 'eth0',
  //       dupPacketCount: 0,
  //       oooPacketCount: 0,
  //       lastRelativeSbn: 0
  //     },
  //     {
  //       interface: 'en15',
  //       dupPacketCount: 0,
  //       oooPacketCount: 0,
  //       lastRelativeSbn: 0
  //     }
  //   ],
  //   audioStats: {
  //     audioChannel: [
  //       {
  //         clippingCount: 0,
  //         levelFast: 0,
  //         levelSlow: 0
  //       },
  //       {
  //         clippingCount: 0,
  //         levelFast: 0,
  //         levelSlow: 0
  //       }
  //     ],
  //     bufferOverrunCount: 0,
  //     bufferUnderrunCount: 0,
  //     codecErrorCount: 0,
  //     streamBufferPos: 0
  //   }
  // }

  const wsClient = new WebSocket(wsServerAddr)
  wsClient.addEventListener('open', async (event) => {
    const proto = (await protobuf.load('./monitor.proto')).lookupType('MonitorProto')

    wsClient.send('Hello Server!')
    wsClient.onmessage = async (event) => {
      if (!(event.data instanceof Blob)) return

      const msgCh1 = proto.decode(new Uint8Array(await event.data.arrayBuffer())).muxChannel[0]
      currentState = msgCh1
    }
  })
</script>

<div id="main">
  <h1><span>ch1</span></h1>
  <div class="row">
    <AudioSection
      config={config}
      data={currentState.audioStats}
    />
    <BlocksSection
      data={currentState}
    />
  </div>

  <div class="row">
    <EndpointsSection
      data={currentState.endpoint}
    />
  </div>
</div>

<style>
  :global(body) {
    margin: 10px 20px 20px 20px;
    font-family: sans-serif;
  }

  h1 {
    font-size: 26px;
    font-weight: normal;
    width: 100%;
    margin: 0px 0px 10px 0px;
    background: linear-gradient(white 0%, white 49%, black 50%, black 51%, white 52%, white 100%);
  }

  h1 span {
    background: white;
    padding: 0px 5px 0px 0px;
  }

  .row {
    display: flex;
    flex-wrap: wrap;
    align-items: flex-start;
  }

  #main {
    display: flex;
    flex-direction: column;
    align-items: flex-start;
  }
</style>
