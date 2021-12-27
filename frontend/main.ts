import { spawn } from 'child_process'
import protobuf from 'protobufjs'

const parseAddrIpv4 = (str: string) => {
  let [ addr, port ] = str.split(':')
  return {
    addr: Buffer.from(addr.split('.').map((octet: string) => parseInt(octet))),
    port: parseInt(port)
  }
}

if (process.argv.length < 3) {
  console.log('Usage: waterslide <remote-addr>:<remote-port>')
  process.exit(1)
}

const initConfigProto = (await protobuf.load('../protobufs/init-config.proto')).lookupType('InitConfigProto')
const configString = (initConfigProto.encode({
  mode: 0, // 0 = sender, 1 = receiver
  endpoints: [parseAddrIpv4(process.argv[2])],
  mux: {
    maxChannels: 10,
    maxPacketSize: 1500
  },
  audio: {
    channelCount: 2,
    ioSampleRate: 44100,
    deviceName: 'Soundflower (2ch)',
    levelSlowAttack: 0.004,
    levelSlowRelease: 0.0008,
    levelFastAttack: 0.31,
    levelFastRelease: 0.00003
  },
  opus: {
    bitrate: 256000,
    frameSize: 240,
    maxPacketSize: 400,
    sampleRate: 48000,
    encodeRingLength: 8192,
    decodeRingLength: 8192
  },
  fec: {
    symbolLen: 256,
    sourceSymbolsPerBlock: 6,
    repairSymbolsPerBlock: 3
  },
  monitor: {
    wsPort: 7681
  }
}).finish() as Buffer).toString('base64')

const waterslide = spawn('../bin/waterslide-macos10', [configString])
waterslide.stdout.pipe(process.stdout)
waterslide.stderr.pipe(process.stderr)
waterslide.on('close', (code) => {
  console.log(`Child process exited with code ${code}`);
})
