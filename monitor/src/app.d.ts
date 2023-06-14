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
    opusStats?: OpusStats
    pcmStats?: PCMStats
  }

  interface EndpointStats {
    interfaceName?: string
    lastRelativeSbn?: number
    open?: boolean
    dupPacketCount?: number
    oooPacketCount?: number
    bytesOut?: number
    bytesIn?: number
    sendCongestion?: number
  }

  interface MonitorData {
    dupBlockCount?: number
    oooBlockCount?: number
    endpoint?: EndpointStats[]
    audioStats?: AudioStats
  }
}
