#!/bin/bash
set -e

# Copyright 2023 Sam Johnson
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

USERNAME=sfjohnson

rm -rf lib/macos-arm64 lib/macos12 lib/android30 lib/rpi lib/rpi-arm64 lib/linux-x64 include/deps

if [[ "$OSTYPE" == "darwin"* ]]; then
  if [[ $(uname -m) == 'arm64' ]]; then
    ARCH=macos-arm64
  else
    ARCH=macos12
  fi
  mkdir -p bin lib/$ARCH lib/android30 lib/rpi lib/rpi-arm64 lib/linux-x64 licenses
elif [[ "$OSTYPE" == "linux-gnu"* && $(uname -m) == 'x86_64' ]]; then
  ARCH=linux
  mkdir -p bin lib/android30 lib/rpi lib/rpi-arm64 lib/linux-x64 licenses
else
  echo "This script only runs on x86_64 macOS, arm64 macOS or x86_64 Linux";
  exit 1;
fi

download () {
  echo "Downloading $3 $2 platform $1"

  if [ $1 != "macos" ]; then
    curl https://github.com/$USERNAME/$3/releases/download/$2/$4-android30.a --output lib/android30/$4.a -s -L
    curl https://github.com/$USERNAME/$3/releases/download/$2/$4-rpi-arm64.a --output lib/rpi-arm64/$4.a -s -L
    curl https://github.com/$USERNAME/$3/releases/download/$2/$4-linux-x64.a --output lib/linux-x64/$4.a -s -L
  fi
  if [[ $1 != "linux" && $ARCH != "linux" ]]; then
    curl https://github.com/$USERNAME/$3/releases/download/$2/$4-$ARCH.a --output lib/$ARCH/$4.a -s -L
  fi

  mkdir -p include/deps/$3
  pushd include/deps/$3 > /dev/null
  curl https://github.com/$USERNAME/$3/releases/download/$2/include.zip --output include.zip -s -L
  unzip -qq include.zip
  rm include.zip
  popd > /dev/null

  curl https://github.com/$USERNAME/$3/releases/download/$2/LICENSE --output licenses/$3.txt -s -L

  if [ $3 = "protobuf" ]; then
    echo "Downloading protoc binary..."
    if [ $ARCH = "linux" ]; then
      curl https://github.com/$USERNAME/$3/releases/download/$2/protoc-linux --output bin/protoc -s -L
    else
      curl https://github.com/$USERNAME/$3/releases/download/$2/protoc-$ARCH --output bin/protoc -s -L
    fi
    chmod +x bin/protoc
  fi
}

download all     v25.3.1      protobuf         libprotobuf-lite
download all     v0.8.2       ck               libck
download linux   v2.2.2       tinyalsa         libtinyalsa
download macos   v19.8.0      portaudio        libportaudio
download all     v6.6.1       r8brain-free-src libr8brain
download all     v1.5.4       opus             libopus
download all     v2.0.14      raptorq          libraptorq
download all     v19.8.3      uWebSockets      libuwebsockets
download all     v0.8.4       boringtun        libboringtun

echo "Fixing protobuf include paths..."
cp -r include/deps/protobuf/absl include/deps
mkdir -p include/deps/google
cp -a include/deps/protobuf include/deps/google
rm -rf include/deps/protobuf
