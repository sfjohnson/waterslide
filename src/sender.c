// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include "utils.h"
#include "opus/opus_multistream.h"
#include "globals.h"
#include "endpoint.h"
#include "mux.h"
#include "pcm.h"
#include "audio.h"
#include "config.h"
#include "sender.h"

static ck_ring_t encodeRing;
static ck_ring_buffer_t *encodeRingBuf;
static int targetEncodeRingSize, encodeRingMaxSize;
static int audioFrameSize;
static int encodedPacketSize;
static uint8_t chIdConfig, chIdAudio;
static uint8_t *receiverConfigBuf = NULL;
static int receiverConfigBufLen = 0;

static atomic_bool threadsRunning;
static pthread_t audioLoopThread, configLoopThread;
static OpusMSEncoder *opusEncoder = NULL;
static pcm_codec_t pcmEncoder = { 0 };
float *sampleBufFloat; // For Opus
double *sampleBufDouble; // For PCM
uint8_t *audioEncodedBuf;

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

static int initAudioLoop (void) {
  const int networkChannelCount = globals_get1i(audio, networkChannelCount);
  const unsigned int audioEncoding = globals_get1ui(audio, encoding);

  sampleBufFloat = (float*)malloc(4 * networkChannelCount * audioFrameSize); // For Opus
  sampleBufDouble = (double*)malloc(8 * networkChannelCount * audioFrameSize); // For PCM
  audioEncodedBuf = (uint8_t*)malloc(encodedPacketSize);

  if (sampleBufFloat == NULL || sampleBufDouble == NULL || audioEncodedBuf == NULL) return -1;
  if (audioEncoding == AUDIO_ENCODING_OPUS && initOpusEncoder(&opusEncoder) < 0) return -2;

  return 0;
}

static void *startAudioLoop (UNUSED void *arg) {
  const int networkChannelCount = globals_get1i(audio, networkChannelCount);
  const double networkSampleRate = globals_get1i(audio, networkSampleRate); // Hz
  const unsigned int audioEncoding = globals_get1ui(audio, encoding);
  uint16_t audioPacketSeq = 0;

  // Sleep for 30% of our desired interval time to make sure encodeRingSize doesn't get too full despite OS jitter.
  double targetSizeNs = 300000000.0 * targetEncodeRingSize / (double)(networkChannelCount * networkSampleRate);
  struct timespec loopSleep;
  loopSleep.tv_nsec = targetSizeNs;
  loopSleep.tv_sec = 0;

  // pin each channel encode thread to a different core, leaving core 0 for other stuff (Linux only)
  // DEBUG: check the CPU core count before calling this
  utils_setCallerThreadRealtime(98, 2);

  while (threadsRunning) {
    int encodeRingSize = utils_ringSize(&encodeRing);
    int encodeRingSizeFrames = encodeRingSize / networkChannelCount;
    globals_add1uiv(statsCh1Audio, streamMeterBins, STATS_STREAM_METER_BINS * encodeRingSize / encodeRingMaxSize, 1);

    // TODO: do xwait_notify from audio thread instead of nanosleeping here; I'm pretty sure xwait_notify doesn't do a syscall
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

    // TODO: do something if mux_writeData returns error
    /*int err = */mux_writeData(chIdAudio, audioEncodedBuf, encodedLen + 2);
  }

  return NULL;
}

static void *startConfigLoop (UNUSED void *arg) {
  while (threadsRunning) {
    mux_writeData(chIdConfig, receiverConfigBuf, receiverConfigBufLen);
    utils_usleep(500000);
  }

  return NULL;
}

/////////////////////
// public
/////////////////////

int sender_init (void) {
  const int networkChannelCount = globals_get1i(audio, networkChannelCount);
  const unsigned int audioEncoding = globals_get1ui(audio, encoding);
  const double networkSampleRate = globals_get1i(audio, networkSampleRate); // Hz

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

  if (mux_init(endpoint_send) < 0) return -2;

  receiverConfigBufLen = config_encodeReceiverConfig(&receiverConfigBuf);
  if (receiverConfigBufLen < 0) return receiverConfigBufLen - 2;

  int chId = mux_addChannel(
    receiverConfigBufLen,
    globals_get1iv(fec, sourceSymbolsPerBlock, 0),
    globals_get1iv(fec, repairSymbolsPerBlock, 0),
    globals_get1iv(fec, symbolLen, 0)
  );
  if (chId < 0) return -5;
  chIdConfig = chId;

  chId = mux_addChannel(
    encodedPacketSize,
    globals_get1iv(fec, sourceSymbolsPerBlock, 1),
    globals_get1iv(fec, repairSymbolsPerBlock, 1),
    globals_get1iv(fec, symbolLen, 1)
  );
  if (chId < 0) return -6;
  chIdAudio = chId;

  mux_setAnchorChannel(chIdAudio);

  char privateKey[SEC_KEY_LENGTH + 1] = { 0 };
  char peerPublicKey[SEC_KEY_LENGTH + 1] = { 0 };
  globals_get1s(root, privateKey, privateKey, sizeof(privateKey));
  globals_get1s(root, peerPublicKey, peerPublicKey, sizeof(peerPublicKey));

  if (strlen(privateKey) != SEC_KEY_LENGTH || strlen(peerPublicKey) != SEC_KEY_LENGTH) {
    printf("Expected privateKey and peerPublicKey to be base64 x25519 keys.\n");
    return -7;
  }

  int err = audio_init(false);
  if (err < 0) return err - 7;

  // NOTE: endpoint_init will block until network discovery is completed
  err = endpoint_init(NULL);
  if (err < 0) return err - 16;

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
  if (err < 0) return err - 23;

  err = audio_start(&encodeRing, encodeRingBuf, encodeRingMaxSize);
  if (err < 0) return err - 24;

  err = initAudioLoop();
  if (err < 0) return err - 36;

  threadsRunning = true;
  if (pthread_create(&audioLoopThread, NULL, startAudioLoop, NULL) != 0) return -39;
  if (pthread_create(&configLoopThread, NULL, startConfigLoop, NULL) != 0) return -40;

  return 0;
}

int sender_deinit (void) {
  threadsRunning = false;
  pthread_join(audioLoopThread, NULL);
  pthread_join(configLoopThread, NULL);
  mux_deinit();
  return audio_deinit();
}
