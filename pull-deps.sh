#!/bin/bash
set -e

USERNAME=sfjohnson

if [[ $(uname -m) == 'arm64' ]]; then
  ARCH=macos-arm64
else
  ARCH=macos10
fi

download () {
  echo "Downloading $3 $2 platform $1"

  if [ $1 = "android" ]; then
    curl https://github.com/$USERNAME/$3/releases/download/$2/$4-android30.a --output lib/android30/$4.a -s -L
  elif [ $1 = "macos" ]; then
    curl https://github.com/$USERNAME/$3/releases/download/$2/$4-$ARCH.a --output lib/$ARCH/$4.a -s -L
  else
    curl https://github.com/$USERNAME/$3/releases/download/$2/$4-android30.a --output lib/android30/$4.a -s -L
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
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
      curl https://github.com/$USERNAME/$3/releases/download/$2/protoc-linux --output bin/protoc -s -L
    elif [[ "$OSTYPE" == "darwin"* ]]; then
      curl https://github.com/$USERNAME/$3/releases/download/$2/protoc-$ARCH --output bin/protoc -s -L
    fi
    chmod +x bin/protoc
  fi
}

rm -rf lib/$ARCH lib/android30 include/deps
mkdir -p bin lib/$ARCH lib/android30 licenses

download all     v21.12.1     protobuf         libprotobuf-lite
download all     v0.7.17      ck               libck
download android v2.1.0       tinyalsa         libtinyalsa
download macos   v19.7.5      portaudio        libportaudio
download all     v6.2.3       r8brain-free-src libr8brain
download all     v1.4.11      opus             libopus
download all     v1.8.0       raptorq          libraptorq
download all     v19.7.22     uWebSockets      libuwebsockets
download all     v0.5.9       boringtun        libboringtun

echo "Fixing protobuf include path..."
mkdir -p include/deps/google
cp -a include/deps/protobuf include/deps/google
rm -rf include/deps/protobuf
