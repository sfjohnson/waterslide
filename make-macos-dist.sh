#!/bin/bash
set -e

# Copyright 2023 Sam Johnson
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

NODE_VERSION=v20.17.0
ARCH_OS=$(uname -m)
ARCH_WTRSL=$([ $ARCH_OS = "arm64" ] && echo "macos-arm64" || echo "macos12")
NPM=waterslide-macos-dist/bin/npm

mkdir -p waterslide-macos-dist/frontend waterslide-macos-dist/protobufs
cp protobufs/init-config.proto waterslide-macos-dist/protobufs
echo -e '#!/bin/bash\n$(dirname "$BASH_SOURCE")/bin/node $(dirname "$BASH_SOURCE")/frontend/out/main.js "$@"' > waterslide-macos-dist/waterslide
chmod 755 waterslide-macos-dist/waterslide

echo "Downloading nodejs $NODE_VERSION"
curl https://nodejs.org/dist/$NODE_VERSION/node-$NODE_VERSION-darwin-$ARCH_OS.tar.gz -s -L | tar xz -C waterslide-macos-dist --strip-components=1

mv waterslide-macos-dist/LICENSE waterslide-macos-dist/LICENSE-NODE.md
cp LICENSE waterslide-macos-dist/LICENSE-WTRSL.md

echo "Downloading waterslide dependencies"
./pull-deps.sh

echo "Make waterslide"
make -f macos.mk

cp bin/waterslide-$ARCH_WTRSL waterslide-macos-dist/bin
make -f macos.mk clean

echo "Make monitor"
cp protobufs/monitor.proto monitor/public
pushd monitor > /dev/null
../$NPM run build
popd > /dev/null

echo "Make frontend"
pushd frontend > /dev/null
../$NPM ci
rm -rf out
../$NPM run build
popd > /dev/null
cp frontend/package.json waterslide-macos-dist/frontend
cp -r frontend/node_modules waterslide-macos-dist/frontend
cp -r frontend/out waterslide-macos-dist/frontend
cp -r monitor/dist waterslide-macos-dist/frontend/monitor

echo "Make dist tar"
tar -cjSf waterslide-macos-$ARCH_OS-dist.tar.bz2 waterslide-macos-dist
rm -rf waterslide-macos-dist
echo "Made waterslide-macos-$ARCH_OS-dist.tar.bz2"
