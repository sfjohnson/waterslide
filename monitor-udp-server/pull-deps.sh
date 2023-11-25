#!/bin/bash
set -e

# Copyright 2023 Sam Johnson
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

USERNAME=sfjohnson
ARCH=linux-x64

rm -rf lib include/deps
mkdir -p bin lib licenses

download () {
  echo "Downloading $3 $2 platform $1"

  curl https://github.com/$USERNAME/$3/releases/download/$2/$4-$ARCH.a --output lib/$4.a -s -L

  mkdir -p include/deps/$3
  pushd include/deps/$3 > /dev/null
  curl https://github.com/$USERNAME/$3/releases/download/$2/include.zip --output include.zip -s -L
  unzip -qq include.zip
  rm include.zip
  popd > /dev/null

  curl https://github.com/$USERNAME/$3/releases/download/$2/LICENSE --output licenses/$3.txt -s -L
}

download all     v19.7.31     uWebSockets      libuwebsockets
