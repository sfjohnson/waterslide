#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include "sender.h"
#include "utils.h"
#include "opus/opus_multistream.h"
#include "portaudio/portaudio.h"
#include "ck/ck_ring.h"
#include "globals.h"
#include "endpoint-secure.h"
#include "mux.h"
#include "raptorq/raptorq.h"
#include "syncer.h"
#include "pcm.h"

#define UNUSED __attribute__((unused))

static ck_ring_t encodeRing;
static ck_ring_buffer_t *encodeRingBuf;
static mux_transfer_t transfer;
static int deviceChannelCount;
static int targetEncodeRingSize;
static int audioFrameSize;
static int maxEncodedPacketSize;
static double encodedSampleRate; // Hz

// DEBUG: implement for Android
#ifndef __ANDROID__
static int recordCallback (const void *inputBuffer, UNUSED void *outputBuffer, unsigned long framesPerBuffer, UNUSED const PaStreamCallbackTimeInfo* timeInfo, UNUSED PaStreamCallbackFlags statusFlags, UNUSED void *userData) {
  // NOTE: the resampling is happening in the high-priority audio thread.
  syncer_enqueueBuf(inputBuffer, framesPerBuffer, deviceChannelCount);
  return paContinue;
}
#endif

static int initOpusEncoder (OpusMSEncoder **encoder) {
  const int networkChannelCount = globals_get1i(audio, networkChannelCount);
  unsigned char mapping[networkChannelCount];
  for (int i = 0; i < networkChannelCount; i++) mapping[i] = i;

  int err;
  *encoder = opus_multistream_encoder_create(AUDIO_OPUS_SAMPLE_RATE, networkChannelCount, networkChannelCount, 0, mapping, OPUS_APPLICATION_AUDIO, &err);
  if (err < 0) {
    printf("Error: opus_multistream_encoder_create failed: %s\n", opus_strerror(err));
    return -1;
  }

  err = opus_multistream_encoder_ctl(*encoder, OPUS_SET_BITRATE(globals_get1i(opus, bitrate)));
  if (err < 0) {
    printf("Error: opus_multistream_encoder_ctl failed: %s\n", opus_strerror(err));
    return -2;
  }

  return 0;
}

static void *startEncodeThread (UNUSED void *arg) {
  const int symbolLen = globals_get1i(fec, symbolLen);
  const int sourceSymbolsPerBlock = globals_get1i(fec, sourceSymbolsPerBlock);
  const int repairSymbolsPerBlock = globals_get1i(fec, repairSymbolsPerBlock);
  const int fecBlockBaseLen = symbolLen * sourceSymbolsPerBlock;
  const int networkChannelCount = globals_get1i(audio, networkChannelCount);
  const unsigned int audioEncoding = globals_get1ui(audio, encoding);

  int fecBlockPos = 0;
  uint8_t fecSBN = 0;

  int fecEncodedBufLen = (4+symbolLen) * (sourceSymbolsPerBlock+repairSymbolsPerBlock);
  uint8_t *fecBlockBuf = (uint8_t*)malloc(fecBlockBaseLen + 2*maxEncodedPacketSize);
  uint8_t *fecEncodedBuf = (uint8_t*)malloc(fecEncodedBufLen);
  float *sampleBuf = (float*)malloc(4 * networkChannelCount * audioFrameSize);
  uint8_t *audioEncodedBuf = (uint8_t*)malloc(maxEncodedPacketSize);

  OpusMSEncoder *opusEncoder = NULL;
  pcm_codec_t pcmEncoder = { 0 };

  if (fecBlockBuf == NULL || fecEncodedBuf == NULL || sampleBuf == NULL || audioEncodedBuf == NULL) {
    printf("Error: encode thread malloc failed.\n");
    return NULL;
  }

  if (audioEncoding == AUDIO_ENCODING_OPUS) {
    if (initOpusEncoder(&opusEncoder) < 0) return NULL;
  }

  double targetSizeSeconds = (double)targetEncodeRingSize / (double)(networkChannelCount * encodedSampleRate);
  // Sleep for shorter than targetSizeSeconds to make sure encodeRingSize doesn't get too full despite OS jitter.
  double sleepUs = 1000000.0 * 0.5 * targetSizeSeconds;

  if (utils_setCallerThreadPrioHigh() < 0) {
    printf("Warning: failed to set encode thread to high priority.\n");
  }

  while (true) {
    int encodeRingSize = ck_ring_size(&encodeRing);
    int encodeRingSizeFrames = encodeRingSize / networkChannelCount;
    globals_set1ui(statsCh1Audio, streamBufferPos, encodeRingSizeFrames);

    if (encodeRingSizeFrames < audioFrameSize) {
      usleep((useconds_t)sleepUs);
      continue;
    }

    if (encodeRingSize > 2 * targetEncodeRingSize) {
      // encodeRing is fuller than it should be due to this thread being preempted by the OS for too long.
      globals_add1ui(statsCh1Audio, encodeThreadJitterCount, 1);
    }

    for (int i = 0; i < networkChannelCount * audioFrameSize; i++) {
      intptr_t outSample = 0;
      ck_ring_dequeue_spsc(&encodeRing, encodeRingBuf, &outSample);
      // https://gist.github.com/shafik/848ae25ee209f698763cffee272a58f8#how-do-we-type-pun-correctly
      memcpy(&sampleBuf[i], &outSample, 4);
    }

    int encodedLen;
    switch (audioEncoding) {
      case AUDIO_ENCODING_OPUS:
        encodedLen = opus_multistream_encode_float(opusEncoder, sampleBuf, audioFrameSize, audioEncodedBuf, maxEncodedPacketSize);
        if (encodedLen < 0) {
          globals_add1ui(statsCh1AudioOpus, codecErrorCount, 1);
          continue;
        }
        break;

      case AUDIO_ENCODING_PCM:
        encodedLen = pcm_encode(&pcmEncoder, sampleBuf, networkChannelCount * audioFrameSize, audioEncodedBuf);
        break;
    }

    fecBlockPos += utils_slipEncode(audioEncodedBuf, encodedLen, &fecBlockBuf[fecBlockPos]);
    fecBlockBuf[fecBlockPos++] = 0xc0;

    // Add more terminating bytes if necessary to pad the block to keep the data rate somewhat constant.
    // DEBUG: fix up the receiver threading code so this isn't necessary.
    if (audioEncoding == AUDIO_ENCODING_OPUS) {
      for (int i = 0; i < maxEncodedPacketSize/3 - encodedLen; i++) {
        fecBlockBuf[fecBlockPos++] = 0xc0;
      }
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
      mux_emitPackets(&transfer, endpointsec_send);
    }
  }
}

int sender_init () {
  #ifdef __ANDROID__
  printf("Error: sender not implemented on Android, exiting...\n");
  return -100;
  #else

  const int networkChannelCount = globals_get1i(audio, networkChannelCount);
  const unsigned int audioEncoding = globals_get1ui(audio, encoding);

  switch (audioEncoding) {
    case AUDIO_ENCODING_OPUS:
      audioFrameSize = globals_get1i(opus, frameSize);
      encodedSampleRate = AUDIO_OPUS_SAMPLE_RATE;
      maxEncodedPacketSize = globals_get1i(opus, maxPacketSize);
      break;
    case AUDIO_ENCODING_PCM:
      audioFrameSize = globals_get1i(pcm, frameSize);
      encodedSampleRate = globals_get1i(pcm, sampleRate);
      // 24-bit samples plus 2 bytes for CRC
      maxEncodedPacketSize = 3 * networkChannelCount * audioFrameSize + 2;
      break;
    default:
      printf("Error: Audio encoding %d not implemented.\n", audioEncoding);
      return -1;
  }

  if (mux_init() < 0) return -4;
  if (mux_initTransfer(&transfer) < 0) return -5;

  char privateKey[SEC_KEY_LENGTH + 1] = { 0 };
  char peerPublicKey[SEC_KEY_LENGTH + 1] = { 0 };
  globals_get1s(root, privateKey, privateKey, sizeof(privateKey));
  globals_get1s(root, peerPublicKey, peerPublicKey, sizeof(peerPublicKey));

  if (strlen(privateKey) != SEC_KEY_LENGTH || strlen(peerPublicKey) != SEC_KEY_LENGTH) {
    printf("Expected privateKey and peerPublicKey to be base64 x25519 keys.\n");
    return -6;
  }

  int err = endpointsec_init(NULL);
  if (err < 0) {
    printf("endpointsec_init error: %d\n", err);
    return -7;
  }

  // DEBUG: move PortAudio stuff to audio module.

  PaError pErr = Pa_Initialize();
  if (pErr != paNoError) {
    printf("Pa_Initialize error: %d\n", pErr);
    return -8;
  }

  int deviceCount = Pa_GetDeviceCount();
  if (deviceCount < 0) {
    printf("Pa_GetDeviceCount error: %d\n", deviceCount);
    return -9;
  }

  if (deviceCount == 0) {
    printf("No audio devices.\n");
    return -10;
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
        return -11;
      }
      printf("Device channels: %d\n", deviceInfo->maxInputChannels);
      printf("Sender channels: %d\n", networkChannelCount);
      if (deviceInfo->maxInputChannels < networkChannelCount) {
        printf("Device does not have enough input channels.\n");
        return -12;
      }
      break;
    }
  }

  if (deviceIndex == deviceCount) {
    printf("Audio device not found.\n");
    return -13;
  }

  deviceChannelCount = deviceInfo->maxInputChannels;
  globals_set1i(audio, deviceChannelCount, deviceChannelCount);

  PaStream *stream;
  PaStreamParameters params = { 0 };
  params.device = deviceIndex;
  params.channelCount = deviceChannelCount;
  params.sampleFormat = paFloat32;
  params.suggestedLatency = deviceInfo->defaultLowInputLatency;
  pErr = Pa_OpenStream(&stream, &params, NULL, globals_get1i(audio, ioSampleRate), 0, 0, recordCallback, NULL);
  if (pErr != paNoError) {
    printf("Pa_OpenStream error: %d\n", pErr);
    return -14;
  }

  const PaStreamInfo *streamInfo = Pa_GetStreamInfo(stream);
  double deviceLatency = streamInfo->inputLatency; // seconds
  double deviceSampleRate = streamInfo->sampleRate; // Hz
  // Set the ioSampleRate global in case PortAudio gave a different sample rate to the one requested.
  globals_set1i(audio, ioSampleRate, (int)deviceSampleRate);
  printf("Device latency (ms): %f\n", 1000.0 * deviceLatency);
  printf("Device sample rate: %f\n", deviceSampleRate);

  // We want there to be about targetEncodeRingSize values in the encodeRing each time the encodeThread
  // wakes from sleep. If the device latency is shorter than the encoding latency, the encodeThread can wait
  // longer for there to be a full frame in the encodeRing.
  // NOTE: Each frame in encodeRing is 1/encodedSampleRate seconds, not 1/deviceSampleRate seconds.
  if (deviceLatency * encodedSampleRate > audioFrameSize) {
    targetEncodeRingSize = deviceLatency * encodedSampleRate;
  } else {
    targetEncodeRingSize = audioFrameSize;
  }
  targetEncodeRingSize *= networkChannelCount;

  // encodeRingMaxSize is the maximum number of float values that can be stored in encodeRing, with
  // networkChannelCount values per frame.
  // In theory the encode thread should loop often enough that the encodeRing never gets much larger than
  // targetEncodeRingSize, but we multiply by 4 to allow plenty of room in encodeRing
  // for timing jitter caused by the operating system's scheduler. Also ck requires a power of two size.
  int encodeRingMaxSize = utils_roundUpPowerOfTwo(4 * targetEncodeRingSize);

  encodeRingBuf = (ck_ring_buffer_t*)malloc(sizeof(ck_ring_buffer_t) * encodeRingMaxSize);
  if (encodeRingBuf == NULL) return -15;
  memset(encodeRingBuf, 0, sizeof(ck_ring_buffer_t) * encodeRingMaxSize);
  ck_ring_init(&encodeRing, encodeRingMaxSize);

  // Calculate the maximum value that framesPerBuffer could be in recordCallback, leaving plenty of spare room.
  int maxFramesPerBuffer = 3.0 * deviceLatency * deviceSampleRate;
  if (syncer_init(deviceSampleRate, encodedSampleRate, maxFramesPerBuffer, &encodeRing, encodeRingBuf, encodeRingMaxSize) < 0) return -16;

  pErr = Pa_StartStream(stream);
  if (pErr != paNoError) {
    printf("Pa_StartStream error: %d\n", pErr);
    return -17;
  }

  pthread_t encodeThread;
  err = pthread_create(&encodeThread, NULL, startEncodeThread, NULL);
  if (err != 0) {
    printf("pthread_create failed: %d\n", err);
    return -18;
  }

  return 0;
  #endif
}
