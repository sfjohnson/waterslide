#!/bin/bash
set -e

USERNAME=sfjohnson

rm -rf lib/macos-arm64 lib/macos11 lib/android30 lib/rpi include/deps

if [[ "$OSTYPE" == "darwin"* ]]; then
  if [[ $(uname -m) == 'arm64' ]]; then
    ARCH=macos-arm64
  else
    ARCH=macos11
  fi
  mkdir -p bin lib/$ARCH lib/android30 lib/rpi licenses
elif [[ "$OSTYPE" == "linux-gnu"* && $(uname -m) == 'x86_64' ]]; then
  ARCH=linux
  mkdir -p bin lib/android30 lib/rpi licenses
else
  echo "This script only runs on x86_64 macOS, arm64 macOS or x86_64 Linux";
  exit 1;
fi

download () {
  echo "Downloading $3 $2 platform $1"

  if [ $1 != "macos" ]; then
    curl https://github.com/$USERNAME/$3/releases/download/$2/$4-android30.a --output lib/android30/$4.a -s -L
    curl https://github.com/$USERNAME/$3/releases/download/$2/$4-rpi.a --output lib/rpi/$4.a -s -L
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

download all     v21.12.2     protobuf         libprotobuf-lite
download all     v0.7.20      ck               libck
download linux   v2.1.2       tinyalsa         libtinyalsa
download macos   v19.7.5      portaudio        libportaudio
download all     v6.2.4       r8brain-free-src libr8brain
download all     v1.4.15      opus             libopus
download all     v1.8.1       raptorq          libraptorq
download all     v19.7.30     uWebSockets      libuwebsockets
download all     v0.5.10      boringtun        libboringtun

echo "Fixing protobuf include path..."
mkdir -p include/deps/google
cp -a include/deps/protobuf include/deps/google
rm -rf include/deps/protobuf
