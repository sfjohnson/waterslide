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
    receiverSync?: number
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
