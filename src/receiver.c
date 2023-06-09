#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "receiver.h"
#include "opus/opus_multistream.h"
#include "ck/ck_ring.h"
#include "globals.h"
#include "demux.h"
#include "endpoint-secure.h"
#include "syncer.h"
#include "audio.h"
#include "utils.h"
#include "pcm.h"

static OpusMSDecoder *decoder = NULL;
static pcm_codec_t pcmDecoder = { 0 };
static demux_channel_t channel1 = { 0 };
static ck_ring_t decodeRing;
static ck_ring_buffer_t *decodeRingBuf;
static int sbnLast = -1;

static unsigned int audioEncoding;
static int networkChannelCount;
static int maxEncodedPacketSize, audioFrameSize, decodeRingMaxSize;
static float *sampleBufFloat;
static uint8_t *audioEncodedBuf;
static int audioEncodedBufPos = 0;
static bool slipEsc = false;

static void decodePacket (const uint8_t *buf, int len) {
  static bool overrun = false;

  if (len < 2) return;
  int seq = utils_readU16LE(buf);
  buf += 2;
  len -= 2;

  int ringCurrentSize = ck_ring_size(&decodeRing);
  globals_set1ui(statsCh1Audio, streamBufferPos, ringCurrentSize / networkChannelCount);

  const uint8_t *pcmSamples;
  int result;

  if (audioEncoding == AUDIO_ENCODING_OPUS) {
    result = opus_multistream_decode_float(decoder, buf, len, sampleBufFloat, audioFrameSize, 0);
    if (result != audioFrameSize) {
      globals_add1ui(statsCh1AudioOpus, codecErrorCount, 1);
      return;
    }
  } else { // audioEncoding == AUDIO_ENCODING_PCM
    result = pcm_decode(&pcmDecoder, buf, len, &pcmSamples);
    if (result != networkChannelCount * audioFrameSize) {
      if (result == -3) globals_add1ui(statsCh1AudioPCM, crcFailCount, 1);
      return;
    }
  }

  if (overrun) {
    // Let the audio callback empty the ring to about half-way before pushing to it again.
    if (ringCurrentSize > decodeRingMaxSize / 2) {
      return;
    } else {
      overrun = false;
    }
  }

  if (audioEncoding == AUDIO_ENCODING_OPUS) {
    result = syncer_enqueueBufF32(sampleBufFloat, audioFrameSize, networkChannelCount, false, seq);
  } else { // audioEncoding == AUDIO_ENCODING_PCM
    result = syncer_enqueueBufS24(pcmSamples, audioFrameSize, networkChannelCount, false, seq);
  }
  if (result == -3) overrun = true;
}

static int slipDecodeBlock (const uint8_t *buf, int bufLen) {
  for (int i = 0; i < bufLen; i++) {
    if (slipEsc) {
      if (buf[i] == 0xdc) {
        if (audioEncodedBufPos >= maxEncodedPacketSize) return -1;
        audioEncodedBuf[audioEncodedBufPos++] = 0xc0;
      } else if (buf[i] == 0xdd) {
        if (audioEncodedBufPos >= maxEncodedPacketSize) return -2;
        audioEncodedBuf[audioEncodedBufPos++] = 0xdb;
      } else {
        return -3;
      }
      slipEsc = false;
      continue;
    }

    if (buf[i] == 0xc0) {
      if (audioEncodedBufPos == 0) continue;
      decodePacket(audioEncodedBuf, audioEncodedBufPos);
      audioEncodedBufPos = 0;
      continue;
    }

    if (buf[i] == 0xdb) {
      slipEsc = true;
    } else {
      if (audioEncodedBufPos >= maxEncodedPacketSize) return -4;
      audioEncodedBuf[audioEncodedBufPos++] = buf[i];
    }
  }

  return bufLen;
}

// The channel lock in the demux module protects the static variables accessed here
static void onBlockCh1 (const uint8_t *buf, int sbn) {
  bool tryDecode = true;

  if (sbnLast != -1) {
    int sbnDiff;
    if (sbnLast - sbn > 128) {
      // Overflow
      sbnDiff = 256 - sbnLast + sbn;
    } else {
      sbnDiff = sbn - sbnLast;
    }

    if (sbnDiff == 0) {
      // Duplicate block, ignore
      globals_add1ui(statsCh1, dupBlockCount, 1);
      tryDecode = false;
    } else if (sbnDiff < 0) {
      // Out-of-order old block, ignore
      globals_add1ui(statsCh1, oooBlockCount, 1);
      tryDecode = false;
    } else if (sbnDiff > 1) {
      int blockDroppedCount = sbnDiff - 1;
      // Out-of-order, previous block(s) were dropped
      globals_add1ui(statsCh1, oooBlockCount, blockDroppedCount);
      audioEncodedBufPos = 0;
      slipEsc = false;
      tryDecode = false;
    }
  }

  sbnLast = sbn;

  // Don't bother SLIP decoding duplicate or out-of-order blocks
  if (!tryDecode) return;

  int result = slipDecodeBlock(buf, channel1.symbolsPerBlock * channel1.symbolLen);
  if (result < 0) {
    audioEncodedBufPos = 0;
    slipEsc = false;
  }
}

int receiver_init (void) {
  networkChannelCount = globals_get1i(audio, networkChannelCount);
  audioEncoding = globals_get1ui(audio, encoding);
  int decodeRingLength;

  switch (audioEncoding) {
    case AUDIO_ENCODING_OPUS:
      audioFrameSize = globals_get1i(opus, frameSize);
      maxEncodedPacketSize = globals_get1i(opus, maxPacketSize);
      break;
    case AUDIO_ENCODING_PCM:
      audioFrameSize = globals_get1i(pcm, frameSize);
      // 24-bit samples + 2 bytes for CRC + 2 bytes for sequence number
      maxEncodedPacketSize = 3 * networkChannelCount * audioFrameSize + 4;
      break;
    default:
      printf("Error: Audio encoding %d not implemented.\n", audioEncoding);
      return -1;
  }

  decodeRingLength = globals_get1i(audio, decodeRingLength);
  decodeRingMaxSize = networkChannelCount * decodeRingLength;
  // ck requires the size to be a power of two but we will pretend decodeRing contains
  // decodeRingMaxSize number of double values, and ignore the rest.
  int decodeRingAllocSize = utils_roundUpPowerOfTwo(decodeRingMaxSize);

  sampleBufFloat = (float *)malloc(4 * networkChannelCount * audioFrameSize);
  audioEncodedBuf = (uint8_t *)malloc(maxEncodedPacketSize);
  decodeRingBuf = (ck_ring_buffer_t*)malloc(sizeof(ck_ring_buffer_t) * decodeRingAllocSize);
  if (sampleBufFloat == NULL || audioEncodedBuf == NULL || decodeRingBuf == NULL) {
    return -2;
  }
  memset(decodeRingBuf, 0, sizeof(ck_ring_buffer_t) * decodeRingAllocSize);

  if (demux_init() < 0) return -3;

  channel1.chId = 1;
  channel1.symbolsPerBlock = globals_get1i(fec, sourceSymbolsPerBlock);
  channel1.symbolLen = globals_get1i(fec, symbolLen);
  channel1.onBlock = onBlockCh1;
  demux_addChannel(&channel1);

  int err;
  if (audioEncoding == AUDIO_ENCODING_OPUS) {
    unsigned char mapping[networkChannelCount];
    for (int i = 0; i < networkChannelCount; i++) mapping[i] = i;
    decoder = opus_multistream_decoder_create(AUDIO_OPUS_SAMPLE_RATE, networkChannelCount, networkChannelCount, 0, mapping, &err);
    if (err < 0) return -4;
  }

  ck_ring_init(&decodeRing, decodeRingAllocSize);

  char privateKey[SEC_KEY_LENGTH + 1] = { 0 };
  char peerPublicKey[SEC_KEY_LENGTH + 1] = { 0 };
  globals_get1s(root, privateKey, privateKey, sizeof(privateKey));
  globals_get1s(root, peerPublicKey, peerPublicKey, sizeof(peerPublicKey));

  if (strlen(privateKey) != SEC_KEY_LENGTH || strlen(peerPublicKey) != SEC_KEY_LENGTH) {
    printf("Expected privateKey and peerPublicKey to be base64 x25519 keys.\n");
    return -5;
  }

  err = audio_init(true);
  if (err < 0) return err - 5;

  // Start audio before endpointsec so that we don't call audio_enqueueBuf before audio module has called syncer_init
  err = audio_start(&decodeRing, decodeRingBuf, decodeRingMaxSize);
  if (err < 0) return err - 13;

  err = endpointsec_init(demux_readPacket);
  if (err < 0) return err - 17;

  return 0;
}
