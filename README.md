# waterslide
Realtime data transport with link aggregation

## Setup (common)

1. Install NodeJS 16 and npm 8

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
```

## Setup (Android specific)

1. Install Android NDK
2. Set the `ANDROID_NDK` env var, e.g.
```sh
export ANDROID_NDK=/Users/<username>/Library/Android/sdk/ndk/21.4.7075529
```

## Build macOS 10.x

```sh
make -f android30.mk clean
make -f macos10.mk
```

## Build Android API 30 (Android 11)

```sh
make -f macos10.mk clean
make -f android30.mk
```
