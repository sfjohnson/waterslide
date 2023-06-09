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

// Let the child process decide when the parent exits. The signals are passed automatically
// to the child and we need to make sure we don't exit on those signals, to give the child
// time to deinit.
waterslide.on('close', (code, signal) => {
  if (code === null) console.log('Closed by', signal)
  process.exit(code ?? 0)
})
process.on('SIGINT', () => { })
process.on('SIGHUP', () => { })
process.on('SIGQUIT', () =>{ })
process.on('SIGTERM', () => { })

app.use(express.static(path.join(__dirname, '../monitor')))
app.listen(8080, () => {
  console.log('Serving monitor on port 8080')
})
