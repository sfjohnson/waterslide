<!--
  Copyright 2023 Sam Johnson
  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at https://mozilla.org/MPL/2.0/.
-->

<script>
  import axios from 'axios'
  import protobuf from 'protobufjs/dist/protobuf.min'
  import AudioSection from './AudioSection.svelte'
  import BlocksSection from './BlocksSection.svelte'
  import EndpointsSection from './EndpointsSection.svelte'

  let currentState = {}

  const entrypoint = async () => {
    const proto = (await protobuf.load('./monitor.proto')).lookupType('MonitorProto')

    let wsPort = '7681'
    try {
      wsPort = (await axios('/ws-port')).data
    } catch { }

    const wsClient = new WebSocket(`ws://${window.location.hostname}:${wsPort}`)
    wsClient.addEventListener('open', async (event) => {
      wsClient.send('Hello Server!')
      wsClient.onmessage = async (event) => {
        if (!(event.data instanceof Blob)) return

        const msgCh1 = proto.decode(new Uint8Array(await event.data.arrayBuffer())).muxChannel[0]
        currentState = msgCh1
      }
    })
  }

  entrypoint()
</script>

<div id="main">
  <h1><span>ch1</span></h1>
  <div class="row">
    <AudioSection data={currentState.audioStats} />
    <BlocksSection data={currentState} />
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
