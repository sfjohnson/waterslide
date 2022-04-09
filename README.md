# waterslide

Realtime data transport with multihoming.

## Features (implemented and to-do)

- [x] Simplex (one-way) protocol (excluding NAT traversal)
- [x] High bitrate Opus compressed audio transport
- [x] Forward error correction (no waiting for packet re-transmissions)
- [x] Works over the internet and on a LAN
- [x] Multihoming
- [ ] Lossless audio
- [x] Multi-channel audio
- [ ] Resampling to correct for clock drift between sender and receiver
- [ ] Encryption
- [ ] Network discovery

## Anti-features

- Lowering the bitrate for a long time when a short spike of packet loss or latency is detected
- Processing the audio to improve speech legibility
- Warping the received audio to allow "catch up" after significant packet loss

## Platforms

- [x] macOS x64
- [x] Android (receiver)
- [ ] Android (sender)
- [ ] macOS ARM
- [ ] Raspberry Pi

## Setup (common)

1. Install Node.js 16 and npm 8

2. Run
```sh
git clone https://github.com/sfjohnson/waterslide.git
cd waterslide
./pull-deps.sh
```

## Setup (macOS specific)

1. Install Homebrew
2. Run
```sh
brew install openssl@3
brew install llvm@13
```

## Setup (Android specific)

1. Install Android NDK
2. Set the `ANDROID_NDK_HOME` env var, e.g.
```sh
export ANDROID_NDK_HOME=/Users/<username>/Library/Android/sdk/ndk/21.4.7075529
```

## Build macOS 10.x

```sh
make -f android30.mk clean
make -f macos10.mk
```

## Build Android (binary only)

API 30 (Android 11)

```sh
make -f macos10.mk clean
make -f android30.mk
```

## Build Android (distributable tar)

1.
```sh
./make-android-dist.sh
```
2. Copy `waterslide-android-dist.tar.bz2` to the Android device.
3. Extract it to a folder the user can execute from e.g.
```sh
cd /data/local/tmp
tar xf waterslide-android-dist.tar.bz2
```
4. Run frontend script:
```sh
cd waterslide-android-dist
./waterslide <config-file>
```

## Frontend

Processing, sending and receiving data is handled by the binary made by the `make` command above. This code is written in C/C++/Rust. NAT traversal, discovery and configuration are handled by code written in TypeScript/Node.js which is the frontend of waterslide.

### Build frontend

```sh
cd frontend
npm run build
```

Built files go to the `frontend/out` folder.

### Run frontend

```sh
cd frontend
npm start config.json
```

### Example config (sender)

```json
{
  "mode": 0,
  "endpoints": [
    {
      "addr": [192, 168, 1, 5],
      "port": 53531,
      "interface": "en0"
    }
  ],
  "mux": {
    "maxChannels": 10,
    "maxPacketSize": 1500
  },
  "audio": {
    "channelCount": 2,
    "ioSampleRate": 44100,
    "deviceName": "Soundflower (2ch)",
    "levelSlowAttack": 0.004,
    "levelSlowRelease": 0.0008,
    "levelFastAttack": 0.31,
    "levelFastRelease": 0.00003
  },
  "opus": {
    "bitrate": 256000,
    "frameSize": 240,
    "maxPacketSize": 250,
    "sampleRate": 48000,
    "decodeRingLength": 8192
  },
  "fec": {
    "symbolLen": 256,
    "sourceSymbolsPerBlock": 6,
    "repairSymbolsPerBlock": 3
  },
  "monitor": {
    "wsPort": 7681
  }
}
```

### Example config (receiver)

```json
{
  "mode": 1,
  "endpoints": [
    {
      "addr": [],
      "port": 53531,
      "interface": "en0"
    }
  ],
  "mux": {
    "maxChannels": 10,
    "maxPacketSize": 1500
  },
  "audio": {
    "channelCount": 2,
    "ioSampleRate": 44100,
    "deviceName": "Built-in Output",
    "levelSlowAttack": 0.004,
    "levelSlowRelease": 0.0008,
    "levelFastAttack": 0.31,
    "levelFastRelease": 0.00003
  },
  "opus": {
    "bitrate": 256000,
    "frameSize": 240,
    "maxPacketSize": 250,
    "sampleRate": 48000,
    "decodeRingLength": 8192
  },
  "fec": {
    "symbolLen": 256,
    "sourceSymbolsPerBlock": 6,
    "repairSymbolsPerBlock": 3
  },
  "monitor": {
    "wsPort": 7681
  }
}
```

## Monitor

The C/C++/Rust code runs a WebSocket server that provides analytics data. An interface called the monitor connects to this server and provides audio meters and detailed information on the status and health of the stream.

### Dev server

1. Copy `protobufs/monitor.proto` to the `monitor/public` folder.
2.
```sh
cd monitor
npm run dev
```
3. Go to http://localhost:3000
4. If waterslide is started or restarted, refresh the page.
5. in `monitor/src/App.svelte` the address of the WebSocket server can be changed for monitoring a remote device running waterslide.

## Licensing

Licenses for dependencies are in the `licenses` folder (after running `./pull-deps.sh`). Contributions to dependencies are made under each dependency's specific license. All other code is licensed under MPL-2.0. Contributions to this repository are made under MPL-2.0.

This software uses an implementation of RaptorQ (RFC 6330). Use of this software must adhere to Qualcomm's conditions, see here: https://datatracker.ietf.org/ipr/2554/
