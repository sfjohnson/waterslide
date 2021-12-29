#!/bin/bash

USERNAME=sfjohnson

download () {
  echo "Downloading $3 $2 platform $1"

  if [ $1 = "android" ]; then
    curl https://github.com/$USERNAME/$3/releases/download/$2/$4-android30.a --output lib/android30/$4.a -s -L
  elif [ $1 = "macos" ]; then
    curl https://github.com/$USERNAME/$3/releases/download/$2/$4-macos10.a --output lib/macos10/$4.a -s -L
  else
    curl https://github.com/$USERNAME/$3/releases/download/$2/$4-android30.a --output lib/android30/$4.a -s -L
    curl https://github.com/$USERNAME/$3/releases/download/$2/$4-macos10.a --output lib/macos10/$4.a -s -L
  fi

  mkdir -p include/deps/$3
  pushd include/deps/$3 > /dev/null
  curl https://github.com/$USERNAME/$3/releases/download/$2/include.zip -s -L | jar x
  popd > /dev/null

  curl https://github.com/$USERNAME/$3/releases/download/$2/LICENSE --output licenses/$3.txt -s -L

  if [ $3 = "protobuf" ]; then
    echo "Downloading protoc binary..."
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
      curl https://github.com/$USERNAME/$3/releases/download/$2/protoc-linux --output bin/protoc -s -L
    elif [[ "$OSTYPE" == "darwin"* ]]; then
      curl https://github.com/$USERNAME/$3/releases/download/$2/protoc-macos10 --output bin/protoc -s -L
    fi
    chmod +x bin/protoc
  fi
}

mkdir -p bin lib/macos10 lib/android30 licenses

download all     v3.19.27     protobuf         libprotobuf-lite
download all     v0.7.12      ck               libck
download android v1.6.3       oboe             liboboe
download macos   v19.7.3      portaudio        libportaudio
download all     v5.2.5       r8brain-free-src libr8brain
download all     v1.4.8       opus             libopus
download all     v1.7.4       raptorq          libraptorq
download all     v19.7.11     uWebSockets      libuwebsockets

echo "Fixing protobuf include path..."
mkdir -p include/deps/google
cp -a include/deps/protobuf include/deps/google
rm -rf include/deps/protobuf
