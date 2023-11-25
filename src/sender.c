// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>
#include <time.h>
#include "utils.h"
#include "opus/opus_multistream.h"
#include "globals.h"
#include "endpoint.h"
#include "mux.h"
#include "raptorq/raptorq.h"
#include "pcm.h"
#include "xsem.h"
#include "audio.h"
#include "sender.h"

static ck_ring_t encodeRing;
static ck_ring_buffer_t *encodeRingBuf;
static mux_transfer_t transfer;
static int targetEncodeRingSize, encodeRingMaxSize;
static int audioFrameSize;
static int encodedPacketSize;
static double networkSampleRate; // Hz
static xsem_t encodeLoopInitSem; // TODO: convert to atomic_wait
static atomic_int encodeLoopStatus = 0;

static int initOpusEncoder (OpusMSEncoder **encoder) {
  const int networkChannelCount = globals_get1i(audio, networkChannelCount);
  unsigned char mapping[networkChannelCount];
  for (int i = 0; i < networkChannelCount; i++) mapping[i] = i;

  int err1, err2;
  *encoder = opus_multistream_encoder_create(AUDIO_OPUS_SAMPLE_RATE, networkChannelCount, networkChannelCount, 0, mapping, OPUS_APPLICATION_AUDIO, &err1);
  if (err1 < 0) {
    printf("Error: opus_multistream_encoder_create failed: %s\n", opus_strerror(err1));
    return -1;
  }

  err1 = opus_multistream_encoder_ctl(*encoder, OPUS_SET_BITRATE(globals_get1i(opus, bitrate)));
  err2 = opus_multistream_encoder_ctl(*encoder, OPUS_SET_VBR(0));
  if (err1 < 0 || err2 < 0) {
    printf("Error: opus_multistream_encoder_ctl failed\n");
    return -2;
  }

  return 0;
}

static inline void setEncodeLoopStatus (int status) {
  encodeLoopStatus = status;
  xsem_post(&encodeLoopInitSem);
}

static void *startEncodeLoop (UNUSED void *arg) {
  const int symbolLen = globals_get1i(fec, symbolLen);
  const int sourceSymbolsPerBlock = globals_get1i(fec, sourceSymbolsPerBlock);
  const int repairSymbolsPerBlock = globals_get1i(fec, repairSymbolsPerBlock);
  const int fecBlockBaseLen = symbolLen * sourceSymbolsPerBlock;
  const int networkChannelCount = globals_get1i(audio, networkChannelCount);
  const unsigned int audioEncoding = globals_get1ui(audio, encoding);

  int fecBlockPos = 0;
  uint8_t fecSBN = 0;
  uint16_t audioPacketSeq = 0;

  int fecEncodedBufLen = (4+symbolLen) * (sourceSymbolsPerBlock+repairSymbolsPerBlock);
  // fecBlockBuf maximum possible length:
  //   fecBlockBaseLen - 1 (once the length is >= fecBlockBaseLen, block(s) are sent)
  // + 2*encodedPacketSize (assuming worst-case scenario of every byte being a SLIP escape sequence)
  // + 1 (for 0xc0)
  uint8_t *fecBlockBuf = (uint8_t*)malloc(fecBlockBaseLen + 2*encodedPacketSize);
  uint8_t *fecEncodedBuf = (uint8_t*)malloc(fecEncodedBufLen);
  float *sampleBufFloat = (float*)malloc(4 * networkChannelCount * audioFrameSize); // For Opus
  double *sampleBufDouble = (double*)malloc(8 * networkChannelCount * audioFrameSize); // For PCM
  uint8_t *audioEncodedBuf = (uint8_t*)malloc(encodedPacketSize);

  OpusMSEncoder *opusEncoder = NULL;
  pcm_codec_t pcmEncoder = { 0 };

  if (fecBlockBuf == NULL || fecEncodedBuf == NULL || sampleBufFloat == NULL || sampleBufDouble == NULL || audioEncodedBuf == NULL) {
    setEncodeLoopStatus(-1);
    return NULL;
  }

  if (audioEncoding == AUDIO_ENCODING_OPUS && initOpusEncoder(&opusEncoder) < 0) {
    setEncodeLoopStatus(-2);
    return NULL;
  }

  raptorq_initEncoder(fecBlockBaseLen, sourceSymbolsPerBlock);

  // Sleep for 30% of our desired interval time to make sure encodeRingSize doesn't get too full despite OS jitter.
  double targetSizeNs = 300000000.0 * targetEncodeRingSize / (double)(networkChannelCount * networkSampleRate);
  struct timespec loopSleep;
  loopSleep.tv_nsec = targetSizeNs;
  loopSleep.tv_sec = 0;

  #if defined(__linux__) || defined(__ANDROID__)
  if (utils_setCallerThreadRealtime(98, 0) < 0) {
    setEncodeLoopStatus(-3);
    return NULL;
  }
  #elif defined(__APPLE__)
  if (utils_setCallerThreadPrioHigh() < 0) {
    setEncodeLoopStatus(-3);
    return NULL;
  }
  #endif

  // successfully initialised, tell the main thread
  setEncodeLoopStatus(1);
  while (encodeLoopStatus == 1) {
    int encodeRingSize = utils_ringSize(&encodeRing);
    int encodeRingSizeFrames = encodeRingSize / networkChannelCount;
    globals_add1uiv(statsCh1Audio, streamMeterBins, STATS_STREAM_METER_BINS * encodeRingSize / encodeRingMaxSize, 1);

    if (encodeRingSizeFrames < audioFrameSize) {
      nanosleep(&loopSleep, NULL);
      continue;
    }

    if (encodeRingSize > 2 * targetEncodeRingSize) {
      // encodeRing is fuller than it should be due to this thread being preempted by the OS for too long.
      globals_add1ui(statsCh1Audio, encodeThreadJitterCount, 1);
    }

    for (int i = 0; i < networkChannelCount * audioFrameSize; i++) {
      sampleBufDouble[i] = utils_ringDequeueSample(&encodeRing, encodeRingBuf);
      sampleBufFloat[i] = sampleBufDouble[i];
    }

    // Write sequence number to audioEncodedBuf
    utils_writeU16LE(audioEncodedBuf, audioPacketSeq++);

    int encodedLen = 0;
    switch (audioEncoding) {
      case AUDIO_ENCODING_OPUS:
        encodedLen = opus_multistream_encode_float(opusEncoder, sampleBufFloat, audioFrameSize, &audioEncodedBuf[2], encodedPacketSize - 2);
        if (encodedLen < 0 || encodedLen != encodedPacketSize - 2) {
          globals_add1ui(statsCh1AudioOpus, codecErrorCount, 1);
          continue;
        }
        break;

      case AUDIO_ENCODING_PCM:
        encodedLen = pcm_encode(&pcmEncoder, sampleBufDouble, networkChannelCount * audioFrameSize, &audioEncodedBuf[2]);
        break;
    }

    fecBlockPos += utils_slipEncode(audioEncodedBuf, encodedLen + 2, &fecBlockBuf[fecBlockPos]);
    fecBlockBuf[fecBlockPos++] = 0xc0;

    while (fecBlockPos >= fecBlockBaseLen) {
      int fecEncodedLen = raptorq_encodeBlock(
        fecSBN++,
        fecBlockBuf,
        fecEncodedBuf,
        sourceSymbolsPerBlock + repairSymbolsPerBlock
      );
      if (fecEncodedLen != fecEncodedBufLen) {
        // DEBUG: mark encode error here
        fecBlockPos = 0;
        continue;
      }
      memmove(fecBlockBuf, &fecBlockBuf[fecBlockBaseLen], fecBlockPos - fecBlockBaseLen);
      fecBlockPos -= fecBlockBaseLen;

      mux_resetTransfer(&transfer);
      mux_setChannel(&transfer, 1, sourceSymbolsPerBlock + repairSymbolsPerBlock, 4 + symbolLen, fecEncodedBuf);
      mux_emitPackets(&transfer, endpoint_send);
    }
  }

  return NULL;
}

int sender_init (void) {
  const int networkChannelCount = globals_get1i(audio, networkChannelCount);
  const unsigned int audioEncoding = globals_get1ui(audio, encoding);

  networkSampleRate = globals_get1i(audio, networkSampleRate);

  switch (audioEncoding) {
    case AUDIO_ENCODING_OPUS:
      audioFrameSize = globals_get1i(opus, frameSize);
      // CBR + 2 bytes for sequence number
      encodedPacketSize = globals_get1i(opus, bitrate) * globals_get1i(opus, frameSize) / (8 * AUDIO_OPUS_SAMPLE_RATE) + 2;
      break;
    case AUDIO_ENCODING_PCM:
      audioFrameSize = globals_get1i(pcm, frameSize);
      // 24-bit samples + 2 bytes for CRC + 2 bytes for sequence number
      encodedPacketSize = 3 * networkChannelCount * audioFrameSize + 4;
      break;
    default:
      printf("Error: Audio encoding %d not implemented.\n", audioEncoding);
      return -1;
  }

  if (mux_init() < 0) return -2;
  if (mux_initTransfer(&transfer) < 0) return -3;

  char privateKey[SEC_KEY_LENGTH + 1] = { 0 };
  char peerPublicKey[SEC_KEY_LENGTH + 1] = { 0 };
  globals_get1s(root, privateKey, privateKey, sizeof(privateKey));
  globals_get1s(root, peerPublicKey, peerPublicKey, sizeof(peerPublicKey));

  if (strlen(privateKey) != SEC_KEY_LENGTH || strlen(peerPublicKey) != SEC_KEY_LENGTH) {
    printf("Expected privateKey and peerPublicKey to be base64 x25519 keys.\n");
    return -4;
  }

  // NOTE: endpoint_init will block until network discovery is completed
  int err = endpoint_init(NULL);
  if (err < 0) return err - 4;

  err = audio_init(false);
  if (err < 0) return err - 21;

  double deviceLatency = audio_getDeviceLatency();

  // We want there to be about targetEncodeRingSize values in the encodeRing each time the encodeThread
  // wakes from sleep. If the device latency is shorter than the encoding latency, the encodeThread can wait
  // longer for there to be a full frame in the encodeRing.
  // NOTE: Each frame in encodeRing is 1/networkSampleRate seconds, not 1/deviceSampleRate seconds.
  if (deviceLatency * networkSampleRate > audioFrameSize) {
    targetEncodeRingSize = deviceLatency * networkSampleRate;
  } else {
    targetEncodeRingSize = audioFrameSize;
  }
  targetEncodeRingSize *= networkChannelCount;

  // encodeRingMaxSize is the maximum number of double values that can be stored in encodeRing, with
  // networkChannelCount values per frame.
  // In theory the encode thread should loop often enough that the encodeRing never gets much larger than
  // targetEncodeRingSize, but we multiply by 4 to allow plenty of room in encodeRing
  // for timing jitter caused by the operating system's scheduler. Also ck requires a power of two size.
  encodeRingMaxSize = utils_roundUpPowerOfTwo(4 * targetEncodeRingSize);
  globals_set1i(statsCh1Audio, streamBufferSize, encodeRingMaxSize / networkChannelCount);

  err = utils_ringInit(&encodeRing, &encodeRingBuf, encodeRingMaxSize);
  if (err < 0) return err - 30;

  err = audio_start(&encodeRing, encodeRingBuf, encodeRingMaxSize);
  if (err < 0) return err - 31;

  pthread_t encodeLoopThread;
  // TODO: convert to C++20 atomic_wait so we can delete xsem.h
  if (xsem_init(&encodeLoopInitSem, 0) < 0) return -35;
  if (pthread_create(&encodeLoopThread, NULL, startEncodeLoop, NULL) != 0) return -36;

  // Wait for encodeLoop to initialise
  xsem_wait(&encodeLoopInitSem);
  if (encodeLoopStatus < 0) {
    if (pthread_join(encodeLoopThread, NULL) != 0) return -37;
    if (xsem_destroy(&encodeLoopInitSem) != 0) return -38;
    return encodeLoopStatus - 38;
  }

  return 0;
}

int sender_deinit (void) {
  return 0;
}
