// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import express from 'express'
import { spawn } from 'child_process'
import protobuf from 'protobufjs'
import { promises as fsp } from 'fs'
import url from 'url'
import path from 'path'
import os from 'os'

const app = express()
const __dirname = path.dirname(url.fileURLToPath(import.meta.url))

if (process.argv.length != 4) {
  console.log(
    'Usage: ./waterslide OPTION CONFIG\n',
    ' -f JSON_FILE      Read config from a JSON file and launch waterslide.\n',
    ' -p PROTOBUF       Read config from a Base64 encoded protobuf message\n',
    '                   literal that was encoded by init-config.proto and\n',
    '                   launch waterslide.\n',
    ' -e JSON_FILE      Encode a JSON file using init-config.proto and output\n',
    '                   the message as Base64, without launching waterslide.'
  )
  process.exit(1)
}

const encodeProtobuf = async (configObj: any): Promise<string> => {
  // protobufjs expects a Buffer instead of an array
  configObj.discovery.serverAddr = Buffer.from(configObj.discovery.serverAddr)

  const protobufPath = path.join(__dirname, '../../protobufs/init-config.proto')
  const initConfigProto = (await protobuf.load(protobufPath)).lookupType('InitConfigProto')
  return (initConfigProto.encode(configObj).finish() as Buffer).toString('base64')
}

const startMonitorServer = (port: number) => {
  app.use(express.static(path.join(__dirname, '../monitor')))
  app.listen(port, () => {
    console.log(`Serving monitor on port ${port}`)
  })
}

const decodeProtobuf = async (b64Config: string): Promise<any> => {
  const protobufPath = path.join(__dirname, '../../protobufs/init-config.proto')
  const initConfigProto = (await protobuf.load(protobufPath)).lookupType('InitConfigProto')
  return initConfigProto.decode(Buffer.from(b64Config, 'base64')).toJSON()
}

const startWaterslide = (b64Config: string): void => {
  let binPath = path.join(__dirname, '../../bin/waterslide')
  const platformAndArch = `${os.platform()}-${os.arch()}`
  switch (platformAndArch) {
    case 'darwin-arm64':
      binPath += '-macos-arm64'
      break
  
    case 'darwin-x64':
      binPath += '-macos11'
      break
  
    case 'android-arm64':
      binPath += '-android30'
      break
    
    case 'linux-x64':
      binPath += '-linux-x64'
      break
  
    default:
      console.log(`Unsupported platform: ${platformAndArch}`)
      process.exit(1)
  }

  const waterslide = spawn(binPath, [b64Config])
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
}

let configObj: any
switch (process.argv[2]) {
  case '-f':
    configObj = JSON.parse(await fsp.readFile(process.argv[3], { encoding: 'utf-8' }))
    if (typeof configObj.monitor.uiPort === 'number') {
      startMonitorServer(configObj.monitor.uiPort)
    }
    startWaterslide(await encodeProtobuf(configObj))
    break

  case '-p':
    configObj = await decodeProtobuf(process.argv[3])
    if (typeof configObj.monitor.uiPort === 'number') {
      startMonitorServer(configObj.monitor.uiPort)
    }
    startWaterslide(process.argv[3])
    break

  case '-e':
    configObj = JSON.parse(await fsp.readFile(process.argv[3], { encoding: 'utf-8' }))
    console.log(await encodeProtobuf(configObj))
    break
}
