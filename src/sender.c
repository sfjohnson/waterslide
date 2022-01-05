#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
// #include <emmintrin.h>
#include "sender.h"
#include "opus/opus.h"
#include "portaudio/portaudio.h"
#include "ck/ck_ring.h"
#include "globals.h"
#include "endpoint.h"
#include "mux.h"
#include "raptorq/raptorq.h"
#include "syncer.h"
#include "xsem.h"

#define UNUSED __attribute__((unused))

static OpusEncoder *encoder;
static uint8_t *opusEncodedBuf;
static ck_ring_t encodeRing;
static ck_ring_buffer_t *encodeRingBuf;
static mux_transfer_t transfer;
static xsem_t encodeSem;

static int16_t *opusFrameBuf;
static int opusFrameSize, opusMaxPacketSize;
static int audioChannelCount;
static int encodeRingLength;

// DEBUG: implement for Android
#ifndef __ANDROID__
static int recordCallback (const void *inputBuffer, UNUSED void *outputBuffer, unsigned long framesPerBuffer, UNUSED const PaStreamCallbackTimeInfo* timeInfo, UNUSED PaStreamCallbackFlags statusFlags, UNUSED void *userData) {
  // NOTE: the resampling is happening in the high-priority audio thread.
  int result = syncer_enqueueBuf(inputBuffer, framesPerBuffer, &encodeRing, encodeRingBuf);
  if (result < 0) return paContinue;

  // DEBUG: the time xsem_post takes varies a lot and it probably isn't good to have it here in the audio callback.
  if ((int)ck_ring_size(&encodeRing) >= opusFrameSize) xsem_post(&encodeSem);

  return paContinue;
}
#endif

static void *startEncodeThread (UNUSED void *arg) {
  const int symbolLen = globals_get1i(fec, symbolLen);
  const int sourceSymbolsPerBlock = globals_get1i(fec, sourceSymbolsPerBlock);
  const int repairSymbolsPerBlock = globals_get1i(fec, repairSymbolsPerBlock);
  const int fecBlockBaseLen = globals_get1i(fec, symbolLen) * sourceSymbolsPerBlock;
  const int opusMaxPacketSize = globals_get1i(opus, maxPacketSize);

  int fecBlockPos = 0;
  uint8_t fecSBN = 0;

  int fecEncodedBufLen = (4+symbolLen) * (sourceSymbolsPerBlock+repairSymbolsPerBlock);
  uint8_t *fecBlockBuf = (uint8_t*)malloc(symbolLen*sourceSymbolsPerBlock + 2*opusMaxPacketSize);
  uint8_t *fecEncodedBuf = (uint8_t*)malloc(fecEncodedBufLen);

  while (true) {
    int encodeRingSize = ck_ring_size(&encodeRing);
    globals_set1ui(statsCh1Audio, streamBufferPos, encodeRingSize);

    if (encodeRingSize < opusFrameSize) {
      xsem_wait(&encodeSem);
      // _mm_pause();
      // usleep(10000); // DEBUG: test value
      continue;
    }

    for (int i = 0; i < opusFrameSize; i++) {
      intptr_t outFrame = 0;
      ck_ring_dequeue_spsc(&encodeRing, encodeRingBuf, &outFrame);
      // https://gist.github.com/shafik/848ae25ee209f698763cffee272a58f8#how-do-we-type-pun-correctly
      // DEBUG: max 2 channels for 32-bit arch, max 4 channels for 64-bit
      memcpy(&opusFrameBuf[audioChannelCount * i], &outFrame, 2 * audioChannelCount);
    }

    int encodedLen = opus_encode(encoder, opusFrameBuf, opusFrameSize, opusEncodedBuf, opusMaxPacketSize);
    if (encodedLen < 0) {
      globals_add1ui(statsCh1Audio, codecErrorCount, 1);
      continue;
    }

    // SLIP encode opusEncodedBuf
    for (int i = 0; i < encodedLen; i++) {
      switch (opusEncodedBuf[i]) {
        case 0xc0:
          fecBlockBuf[fecBlockPos++] = 0xdb;
          fecBlockBuf[fecBlockPos++] = 0xdc;
          break;
        case 0xdb:
          fecBlockBuf[fecBlockPos++] = 0xdb;
          fecBlockBuf[fecBlockPos++] = 0xdd;
          break;
        default:
          fecBlockBuf[fecBlockPos++] = opusEncodedBuf[i];
      }
    }

    // Add a terminating byte and more if necessary to pad the block to keep the data rate somewhat constant.
    fecBlockBuf[fecBlockPos++] = 0xc0;
    for (int i = 0; i < opusMaxPacketSize/3 - encodedLen; i++) {
      fecBlockBuf[fecBlockPos++] = 0xc0;
    }

    if (fecBlockPos >= fecBlockBaseLen) {
      int fecEncodedLen = raptorq_encodeBlock(
        fecSBN++,
        fecBlockBuf,
        fecBlockBaseLen,
        sourceSymbolsPerBlock,
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
}

int sender_init () {
  #ifdef __ANDROID__
  printf("Sender not implemented on Android, exiting...\n");
  return -100;
  #else

  audioChannelCount = globals_get1i(audio, channelCount);
  opusFrameSize = globals_get1i(opus, frameSize);
  encodeRingLength = globals_get1i(opus, encodeRingLength);
  opusMaxPacketSize = globals_get1i(opus, maxPacketSize);
  opusFrameBuf = (int16_t*)malloc(4 * opusFrameSize);
  if (opusFrameBuf == NULL) return -1;
  opusEncodedBuf = (uint8_t*)malloc(opusMaxPacketSize);
  if (opusEncodedBuf == NULL) return -2;
  encodeRingBuf = (ck_ring_buffer_t*)malloc(sizeof(ck_ring_buffer_t) * encodeRingLength);
  if (encodeRingBuf == NULL) return -3;
  memset(encodeRingBuf, 0, sizeof(ck_ring_buffer_t) * encodeRingLength);

  if (mux_init() < 0) return -4;
  if (mux_initTransfer(&transfer) < 0) return -5;

  xsem_init(&encodeSem, 0);

  // DEBUG: 4096 is a magic number
  if (syncer_init(44100.0, 48000.0, 4096) < 0) return -6;

  int err = endpoint_init(false, NULL);
  if (err < 0) {
    printf("endpoint_init error: %d\n", err);
    return -7;
  }

  encoder = opus_encoder_create(globals_get1i(opus, sampleRate), audioChannelCount, OPUS_APPLICATION_AUDIO, &err);
  if (err < 0) {
    printf("opus_encoder_create failed: %s\n", opus_strerror(err));
    return -8;
  }

  err = opus_encoder_ctl(encoder, OPUS_SET_BITRATE(globals_get1i(opus, bitrate)));
  if (err < 0) {
    printf("opus_encoder_ctl failed: %s\n", opus_strerror(err));
    return -9;
  }

  // https://twitter.com/marcan42/status/1264844348933369858
  err = opus_encoder_ctl(encoder, OPUS_SET_PHASE_INVERSION_DISABLED(1));
  if (err < 0) {
    printf("opus_encoder_ctl failed: %s\n", opus_strerror(err));
    return -10;
  }

  pthread_t encodeThread;
  err = pthread_create(&encodeThread, NULL, startEncodeThread, NULL);
  if (err != 0) {
    printf("pthread_create failed: %d\n", err);
    return -11;
  }

  ck_ring_init(&encodeRing, encodeRingLength);

  PaError pErr = Pa_Initialize();
  if (pErr != paNoError) {
    printf("Pa_Initialize error: %d\n", pErr);
    return -12;
  }

  int deviceCount = Pa_GetDeviceCount();
  if (deviceCount < 0) {
    printf("Pa_GetDeviceCount error: %d\n", deviceCount);
    return -13;
  }

  if (deviceCount == 0) {
    printf("No audio devices.\n");
    return -14;
  }

  char audioDeviceName[100] = { 0 };
  globals_get1s(audio, deviceName, audioDeviceName, sizeof(audioDeviceName));

  const PaDeviceInfo *deviceInfo;
  int deviceIndex;
  for (deviceIndex = 0; deviceIndex < deviceCount; deviceIndex++) {
    deviceInfo = Pa_GetDeviceInfo(deviceIndex);
    printf("Available device: %s\n", deviceInfo->name);
    if (strcmp(deviceInfo->name, audioDeviceName) == 0) {
      printf("Input device: %s\n", deviceInfo->name);
      if (deviceInfo->maxInputChannels == 0) {
        printf("No input channels.\n");
        return -15;
      }
      printf("Channels: %d\n", deviceInfo->maxInputChannels);
      if (deviceInfo->maxInputChannels != audioChannelCount) {
        printf("Device must %d channels.\n", audioChannelCount);
        return -16;
      }
      break;
    }
  }

  if (deviceIndex == deviceCount) {
    printf("Audio device not found.\n");
    return -17;
  }

  PaStream *stream;
  const PaStreamInfo *streamInfo;
  PaStreamParameters params = { 0 };
  params.device = deviceIndex;
  params.channelCount = deviceInfo->maxInputChannels;
  params.sampleFormat = paInt16;
  // printf("Suggested Latency (ms): %f\n", 1000.0 * deviceInfo->defaultLowInputLatency);
  params.suggestedLatency = deviceInfo->defaultLowInputLatency;
  double ioSampleRate = globals_get1i(audio, ioSampleRate);
  pErr = Pa_OpenStream(&stream, &params, NULL, ioSampleRate, 0, 0, recordCallback, NULL);
  if (pErr != paNoError) {
    printf("Pa_OpenStream error: %d\n", pErr);
    return -18;
  }

  streamInfo = Pa_GetStreamInfo(stream);
  printf("Latency (ms): %f\n", 1000.0 * streamInfo->inputLatency);
  printf("Sample rate: %f\n", streamInfo->sampleRate);

  pErr = Pa_StartStream(stream);
  if (pErr != paNoError) {
    printf("Pa_StartStream error: %d\n", pErr);
    return -19;
  }

  return 0;
  #endif
}
