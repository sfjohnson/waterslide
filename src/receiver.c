#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "receiver.h"
#include "opus/opus_multistream.h"
#include "ck/ck_ring.h"
#include "globals.h"
#include "demux.h"
#include "endpoint.h"
#include "audio.h"
#include "utils.h"

static OpusMSDecoder *decoder;
static demux_channel_t channel1 = { 0 };
static ck_ring_t decodeRing;
static ck_ring_buffer_t *decodeRingBuf;
static int sbnLast = -1;

static int networkChannelCount;
static int opusMaxPacketSize, opusFrameSize, decodeRingMaxSize;
static float *opusDecodedBuf;
static uint8_t *opusEncodedBuf;

static void decodeOpusPacket (const uint8_t *buf, int len) {
  static bool overrun = false;

  int ringCurrentSize = ck_ring_size(&decodeRing);
  globals_set1ui(statsCh1Audio, streamBufferPos, ringCurrentSize / networkChannelCount);

  int result = opus_multistream_decode_float(decoder, buf, len, opusDecodedBuf, opusFrameSize, 0);
  if (result != opusFrameSize) {
    globals_add1ui(statsCh1Audio, codecErrorCount, 1);
    return;
  }

  if (overrun) {
    // Let the audio callback empty the ring to about half-way before pushing to it again.
    if (ringCurrentSize > decodeRingMaxSize / 2) {
      return;
    } else {
      overrun = false;
    }
  }

  result = audio_enqueueBuf(opusDecodedBuf, opusFrameSize, networkChannelCount);
  if (result == -2) overrun = true;
}

// The channel lock in the demux module protects the static variables accessed here
static int onBlockCh1 (const uint8_t *buf, int sbn) {
  static int opusEncodedBufPos = 0;
  // DEBUG: handle block loss

  int sbnDiff;
  if (sbnLast == -1) {
    sbnDiff = 0;
  } else if (sbnLast - sbn > 220) { // DEBUG: why 220?
    // Overflow
    sbnDiff = 256 - sbnLast + sbn;
  } else {
    sbnDiff = sbn - sbnLast;
  }
  sbnLast = sbn;

  if (sbnDiff == 0) {
    globals_add1ui(statsCh1, dupBlockCount, 1);
  } else if (sbnDiff < 0 || sbnDiff > 1) {
    globals_add1ui(statsCh1, oooBlockCount, 1);
  }

  bool esc = false;
  for (int i = 0; i < channel1.symbolsPerBlock * channel1.symbolLen; i++) {
    if (buf[i] == 0xc0) {
      if (opusEncodedBufPos == 0) continue;
      decodeOpusPacket(opusEncodedBuf, opusEncodedBufPos);
      opusEncodedBufPos = 0;
      continue;
    }

    if (esc) {
      if (buf[i] == 0xdc) {
        if (opusEncodedBufPos >= opusMaxPacketSize) return -1;
        opusEncodedBuf[opusEncodedBufPos++] = 0xc0;
      } else if (buf[i] == 0xdd) {
        if (opusEncodedBufPos >= opusMaxPacketSize) return -2;
        opusEncodedBuf[opusEncodedBufPos++] = 0xdb;
      } else {
        // DEBUG: handle invalid SLIP
      }
      esc = false;
      continue;
    }

    if (buf[i] == 0xdb) {
      esc = true;
    } else {
      if (opusEncodedBufPos >= opusMaxPacketSize) return -3;
      opusEncodedBuf[opusEncodedBufPos++] = buf[i];
    }
  }

  return 0;
}

int receiver_init () {
  opusMaxPacketSize = globals_get1i(opus, maxPacketSize);
  opusFrameSize = globals_get1i(opus, frameSize);
  networkChannelCount = globals_get1i(audio, networkChannelCount);
  decodeRingMaxSize = networkChannelCount * globals_get1i(opus, decodeRingLength);
  // ck requires the size to be a power of two but we will pretend decodeRing contains
  // decodeRingMaxSize number of float values, and ignore the rest.
  int decodeRingAllocSize = utils_roundUpPowerOfTwo(decodeRingMaxSize);

  opusDecodedBuf = (float*)malloc(4 * networkChannelCount * opusFrameSize);
  if (opusDecodedBuf == NULL) return -1;
  opusEncodedBuf = (uint8_t *)malloc(opusMaxPacketSize);
  if (opusEncodedBuf == NULL) return -2;
  decodeRingBuf = (ck_ring_buffer_t*)malloc(sizeof(ck_ring_buffer_t) * decodeRingAllocSize);
  if (decodeRingBuf == NULL) return -3;
  memset(decodeRingBuf, 0, sizeof(ck_ring_buffer_t) * decodeRingAllocSize);

  if (demux_init() < 0) return -4;

  channel1.chId = 1;
  channel1.symbolsPerBlock = globals_get1i(fec, sourceSymbolsPerBlock);
  channel1.symbolLen = globals_get1i(fec, symbolLen);
  channel1.onBlock = onBlockCh1;
  demux_addChannel(&channel1);

  unsigned char mapping[networkChannelCount];
  for (int i = 0; i < networkChannelCount; i++) mapping[i] = i;

  int err;
  decoder = opus_multistream_decoder_create(globals_get1i(opus, sampleRate), networkChannelCount, networkChannelCount, 0, mapping, &err);
  if (err < 0) {
    printf("opus_multistream_decoder_create failed: %s\n", opus_strerror(err));
    return -5;
  }

  // https://twitter.com/marcan42/status/1264844348933369858
  // err = opus_decoder_ctl(decoder, OPUS_SET_PHASE_INVERSION_DISABLED(1));
  // if (err < 0) {
  //   printf("opus_decoder_ctl failed: %s\n", opus_strerror(err));
  //   return -6;
  // }

  ck_ring_init(&decodeRing, decodeRingAllocSize);
  // half-fill ring buffer
  for (int i = 0; i < decodeRingMaxSize / 2; i++) {
    intptr_t inFrame = 0;
    ck_ring_enqueue_spsc(&decodeRing, decodeRingBuf, (void*)inFrame);
  }

  if (audio_init(&decodeRing, decodeRingBuf, decodeRingMaxSize) < 0) return -7;

  char audioDeviceName[MAX_DEVICE_NAME_LEN + 1] = { 0 };
  globals_get1s(audio, deviceName, audioDeviceName, sizeof(audioDeviceName));
  if (audio_start(audioDeviceName) < 0) return -8;

  err = endpoint_init(true, demux_readPacket);
  if (err < 0) {
    printf("endpoint_init error: %d\n", err);
    return -10;
  }

  return 0;
}
