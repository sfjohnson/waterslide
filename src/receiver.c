#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "receiver.h"
#include "opus/opus_multistream.h"
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

static unsigned int audioEncoding;
static int networkChannelCount;
static int maxEncodedPacketSize, audioFrameSize, decodeRingMaxSize;
static float *sampleBufFloat;
static uint8_t *audioEncodedBuf;
static int audioEncodedBufPos = 0;
static bool slipEsc = false;
// static double avgAudioFramesPerBlock = 0.0;

// returns: number of audio frames added to decodeRing (after resampling), or negative error code
static int decodePacket (const uint8_t *buf, int len) {
  static bool overrun = false;

  if (len < 2) return -1;
  int seq = utils_readU16LE(buf);
  buf += 2;
  len -= 2;

  int ringCurrentSize = utils_ringSize(&decodeRing);
  globals_add1uiv(statsCh1Audio, streamMeterBins, STATS_STREAM_METER_BINS * ringCurrentSize / decodeRingMaxSize, 1);

  const uint8_t *pcmSamples;
  int result;

  if (audioEncoding == AUDIO_ENCODING_OPUS) {
    result = opus_multistream_decode_float(decoder, buf, len, sampleBufFloat, audioFrameSize, 0);
    if (result != audioFrameSize) {
      globals_add1ui(statsCh1AudioOpus, codecErrorCount, 1);
      return -2;
    }
  } else { // audioEncoding == AUDIO_ENCODING_PCM
    result = pcm_decode(&pcmDecoder, buf, len, &pcmSamples);
    if (result != networkChannelCount * audioFrameSize) {
      if (result == -3) globals_add1ui(statsCh1AudioPCM, crcFailCount, 1);
      return -3;
    }
  }

  if (overrun) {
    // Let the audio callback empty the ring to about half-way before pushing to it again.
    if (ringCurrentSize > decodeRingMaxSize / 2) {
      return 0;
    } else {
      overrun = false;
    }
  }

  globals_set1ui(statsCh1Audio, codecRingActive, 1);

  if (audioEncoding == AUDIO_ENCODING_OPUS) {
    result = syncer_enqueueBufF32(sampleBufFloat, audioFrameSize, networkChannelCount, false, seq);
  } else { // audioEncoding == AUDIO_ENCODING_PCM
    result = syncer_enqueueBufS24Packed(pcmSamples, audioFrameSize, networkChannelCount, false, seq);
  }
  if (result == -3) {
    overrun = true;
    return -4;
  }

  return result;
}

// static void enqueueSilence (int frameCount) {
//   int totalSamples = networkChannelCount * frameCount;
//   // Don't ever let the ring fill completely, that way the channels stay in order
//   if ((int)utils_ringSize(&decodeRing) + totalSamples > decodeRingMaxSize) return;

//   for (int i = 0; i < totalSamples; i++) {
//     utils_ringEnqueueSample(&decodeRing, decodeRingBuf, 0);
//   }
// }

// returns: number of audio frames added to decodeRing (after resampling), or negative error code
static int slipDecodeBlock (const uint8_t *buf, int bufLen) {
  int result = 0;

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
      int frameCount = decodePacket(audioEncodedBuf, audioEncodedBufPos);
      // Only return total frame count if all calls to decodePacket were successful
      if (result < 0 || frameCount < 0) {
        result = -4; // decodePacket returned an error, either this time or previously
      } else if (result >= 0) {
        result += frameCount;
      }
      audioEncodedBufPos = 0;
      continue;
    }

    if (buf[i] == 0xdb) {
      slipEsc = true;
    } else {
      if (audioEncodedBufPos >= maxEncodedPacketSize) return -5;
      audioEncodedBuf[audioEncodedBufPos++] = buf[i];
    }
  }

  return result;
}

// The channel lock in the demux module protects the static variables accessed here
static void onBlockCh1 (const uint8_t *buf, int sbn) {
  static struct timespec tsp;
  static int sbnLast = -1;
  bool tryDecode = true;

  if (clock_gettime(CLOCK_MONOTONIC, &tsp) == 0) {
    // us will be between 0 and 999_999_999 and will roll back to zero every 1000 seconds
    unsigned int us = 1000000u * (tsp.tv_sec % 1000) + (tsp.tv_nsec / 1000);
    unsigned int ringPos = globals_get1ui(statsCh1, blockTimingRingPos);
    globals_set1uiv(statsCh1, blockTimingRing, ringPos, us);
    if (++ringPos == STATS_BLOCK_TIMING_RING_LEN) ringPos = 0;
    // NOTE: blockTimingRingPos must only be written to here
    globals_set1ui(statsCh1, blockTimingRingPos, ringPos);
  }

  if (sbnLast != -1) {
    int sbnDiff;
    if (sbnLast - sbn > 128) {
      // Overflow
      sbnDiff = 256 - sbnLast + sbn;
    } else {
      sbnDiff = sbn - sbnLast;
    }

    if (sbnDiff == 0) {
      // Duplicate block
      globals_add1ui(statsCh1, dupBlockCount, 1);
      tryDecode = false;
    } else if (sbnDiff < 0) {
      // Out-of-order old block
      globals_add1ui(statsCh1, oooBlockCount, 1);
      tryDecode = false;
    } else if (sbnDiff > 1) {
      int blockDroppedCount = sbnDiff - 1;
      // Out-of-order, previous block(s) were dropped
      globals_add1ui(statsCh1, oooBlockCount, blockDroppedCount);
      // Fill in decodeRing with zeros based on average block size plus
      // one assumed lost packet due to resetting of SLIP state
      // enqueueSilence((int)(blockDroppedCount * avgAudioFramesPerBlock) + audioFrameSize);
      audioEncodedBufPos = 0;
      slipEsc = false;
      tryDecode = false;
    }
  }

  sbnLast = sbn;

  // Don't bother SLIP decoding duplicate or out-of-order blocks
  if (!tryDecode) return;

  int result = slipDecodeBlock(buf, channel1.symbolsPerBlock * channel1.symbolLen);
  if (result >= 0) {
    // avgAudioFramesPerBlock += 0.003 * ((double)result - avgAudioFramesPerBlock);
    // DEBUG: log
    // globals_set1uiv(statsEndpoints, bytesOut, 0, (unsigned int)(1000000.0f * avgAudioFramesPerBlock));
  } else if (result == -4) {
    // decodePacket error
  } else {
    // SLIP error
    audioEncodedBufPos = 0;
    slipEsc = false;
  }
}

int receiver_init (void) {
  networkChannelCount = globals_get1i(audio, networkChannelCount);
  audioEncoding = globals_get1ui(audio, encoding);

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

  int decodeRingLength = globals_get1i(audio, decodeRingLength);
  decodeRingMaxSize = networkChannelCount * decodeRingLength;
  globals_set1i(statsCh1Audio, streamBufferSize, decodeRingLength);

  sampleBufFloat = (float *)malloc(4 * networkChannelCount * audioFrameSize);
  audioEncodedBuf = (uint8_t *)malloc(maxEncodedPacketSize);
  if (sampleBufFloat == NULL || audioEncodedBuf == NULL) return -2;

  if (utils_ringInit(&decodeRing, &decodeRingBuf, decodeRingMaxSize) < 0) return -2;
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

  // enqueueSilence(decodeRingLength / 2);

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
  if (err < 0) return err - 14;

  // NOTE: endpointsec_init will block until network discovery is completed
  err = endpointsec_init(demux_readPacket);
  if (err < 0) return err - 18;

  return 0;
}
