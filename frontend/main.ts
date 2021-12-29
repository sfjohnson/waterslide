import { spawn } from 'child_process'
import protobuf from 'protobufjs'
import { promises as fsp } from 'fs'

// const parseAddrIpv4 = (str: string) => {
//   let [ addr, port ] = str.split(':')
//   return {
//     addr: Buffer.from(addr.split('.').map((octet: string) => parseInt(octet))),
//     port: parseInt(port)
//   }
// }

if (process.argv.length < 3) {
  console.log('Usage: waterslide <config-file>')
  process.exit(1)
}

let configFile: any = await fsp.readFile(process.argv[2])
configFile = JSON.parse(configFile)
for (const endpoint of configFile.endpoints) {
  endpoint.addr = Buffer.from(endpoint.addr)
}

const initConfigProto = (await protobuf.load('../protobufs/init-config.proto')).lookupType('InitConfigProto')
const configString = (initConfigProto.encode(configFile).finish() as Buffer).toString('base64')

const waterslide = spawn('../bin/waterslide-macos10', [configString])
waterslide.stdout.pipe(process.stdout)
waterslide.stderr.pipe(process.stderr)
waterslide.on('close', (code) => {
  console.log(`Child process exited with code ${code}`);
})
