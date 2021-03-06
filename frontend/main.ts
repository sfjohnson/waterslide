import { spawn } from 'child_process'
import protobuf from 'protobufjs'
import { promises as fsp } from 'fs'
import url from 'url'
import path from 'path'

const __dirname = path.dirname(url.fileURLToPath(import.meta.url))

if (process.argv.length < 3) {
  console.log('Usage: waterslide <config-file>')
  process.exit(1)
}

let binPath = path.join(__dirname, '../../bin/waterslide')
switch (process.platform) {
  case 'darwin':
    binPath += '-macos10'
    break

  case 'android':
    binPath += '-android30'
    break

  default:
    console.log(`Unsupported platform: ${process.platform}`)
    process.exit(1)
}

const protobufPath = path.join(__dirname, '../../protobufs/init-config.proto')

let configFile: any = await fsp.readFile(process.argv[2])
configFile = JSON.parse(configFile)
for (const endpoint of configFile.endpoints) {
  endpoint.addr = Buffer.from(endpoint.addr)
}

const initConfigProto = (await protobuf.load(protobufPath)).lookupType('InitConfigProto')
const configString = (initConfigProto.encode(configFile).finish() as Buffer).toString('base64')

const waterslide = spawn(binPath, [configString])
waterslide.stdout.pipe(process.stdout)
waterslide.stderr.pipe(process.stderr)
waterslide.on('close', (code) => {
  console.log(`Child process exited with code ${code}`);
})
