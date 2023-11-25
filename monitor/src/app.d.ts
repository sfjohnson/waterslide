// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

namespace App {
  interface AudioChannel {
    clippingCount?: number
    levelFast?: number
    levelSlow?: number
  }

  interface OpusStats {
    codecErrorCount?: number
  }

  interface PCMStats {
    crcFailCount?: number
  }

  interface AudioStats {
    audioChannel?: AudioChannel[]
    streamBufferSize?: number
    streamMeterBins?: Uint8Array
    bufferOverrunCount?: number
    bufferUnderrunCount?: number
    encodeThreadJitterCount?: number
    audioLoopXrunCount?: number
    clockError?: number
    opusStats?: OpusStats
    pcmStats?: PCMStats
  }

  interface EndpointStats {
    interfaceName?: string
    lastRelativeSbn?: number
    open?: boolean
    bytesOut?: number
    bytesIn?: number
    sendCongestion?: number
  }

  interface MonitorData {
    dupBlockCount?: number
    oooBlockCount?: number
    blockTiming?: Uint8Array
    endpoint?: EndpointStats[]
    audioStats?: AudioStats
  }
}
