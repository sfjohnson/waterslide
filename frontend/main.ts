import express from 'express'
import { spawn } from 'child_process'
import protobuf from 'protobufjs'
import { promises as fsp } from 'fs'
import url from 'url'
import path from 'path'
import os from 'os'

const app = express()
const __dirname = path.dirname(url.fileURLToPath(import.meta.url))

if (process.argv.length < 3) {
  console.log('Usage: waterslide <config-file>')
  process.exit(1)
}

let binPath = path.join(__dirname, '../../bin/waterslide')
const platformAndArch = `${os.platform()}-${os.arch()}`
switch (platformAndArch) {
  case 'darwin-arm64':
    binPath += '-macos-arm64'
    break

  case 'darwin-x64':
    binPath += '-macos10'
    break

  case 'android-arm64':
    binPath += '-android30'
    break

  default:
    console.log(`Unsupported platform: ${platformAndArch}`)
    process.exit(1)
}

const protobufPath = path.join(__dirname, '../../protobufs/init-config.proto')

let configFile: any = await fsp.readFile(process.argv[2])
configFile = JSON.parse(configFile)
// protobufjs expects a Buffer instead of an array
configFile.discovery.serverAddr = Buffer.from(configFile.discovery.serverAddr)

const initConfigProto = (await protobuf.load(protobufPath)).lookupType('InitConfigProto')
const configString = (initConfigProto.encode(configFile).finish() as Buffer).toString('base64')

const waterslide = spawn(binPath, [configString])
waterslide.stdout.pipe(process.stdout)
waterslide.stderr.pipe(process.stderr)
waterslide.on('close', (code) => {
  console.log(`Child process exited with code ${code}`);
})

app.use(express.static(path.join(__dirname, '../monitor')))
app.listen(8080, () => {
  console.log('Serving monitor on port 8080')
})
