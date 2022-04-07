#!/bin/bash
set -e

BUILD_VERSION=v1.1.8
NODE_VERSION=v16.14.2

rm -rf waterslide-android-dist
mkdir -p waterslide-android-dist/frontend waterslide-android-dist/protobufs
cp protobufs/init-config.proto waterslide-android-dist/protobufs
echo -e '#!/system/bin/sh\nbin/node frontend/out/main.js "$@"' > waterslide-android-dist/waterslide
chmod 755 waterslide-android-dist/waterslide

echo "Downloading nodejs-android $NODE_VERSION"
curl https://github.com/sfjohnson/nodejs-android/releases/download/$BUILD_VERSION/node-$NODE_VERSION-android-arm64.tar.xz -s -L | tar xz -C waterslide-android-dist

echo "Downloading waterslide dependencies"
./pull-deps.sh

echo "Make waterslide"
make -f macos10.mk clean
make -f android30.mk

cp bin/waterslide-android30 waterslide-android-dist/bin

echo "Make frontend"
pushd frontend > /dev/null
npm ci
rm -rf out
npm run build
popd > /dev/null
cp frontend/package.json waterslide-android-dist/frontend
cp -r frontend/node_modules waterslide-android-dist/frontend
cp -r frontend/out waterslide-android-dist/frontend

echo "Make dist tar"
tar -cjSf waterslide-android-dist.tar.bz2 waterslide-android-dist
