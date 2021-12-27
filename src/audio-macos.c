#include <stdio.h>
#include "portaudio/portaudio.h"
#include "syncer.h"
#include "globals.h"
#include "audio.h"

static ck_ring_t *_ring;
static ck_ring_buffer_t *_ringBuf;
static int audioChannelCount;

static int playCallback (const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void *userData) {
  int16_t *outBuf = (int16_t *)outputBuffer;
  for (unsigned long i = 0; i < framesPerBuffer; i++) {
    intptr_t outFrame = 0;
    if (!ck_ring_dequeue_spsc(_ring, _ringBuf, &outFrame)) {
      globals_add1ui(statsCh1Audio, bufferUnderrunCount, 1);
    }

    // DEBUG: max 2 channels for 32-bit arch, max 4 channels for 64-bit
    memcpy(&outBuf[audioChannelCount * i], &outFrame, 2 * audioChannelCount);
  }

  return paContinue;
}

int audio_init (ck_ring_t *ring, ck_ring_buffer_t *ringBuf) {
  _ring = ring;
  _ringBuf = ringBuf;
  audioChannelCount = globals_get1i(audio, channelCount);

  PaError pErr = Pa_Initialize();
  if (pErr != paNoError) {
    printf("Pa_Initialize error: %d\n", pErr);
    return -1;
  }

  int deviceCount = Pa_GetDeviceCount();
  if (deviceCount < 0) {
    printf("Pa_GetDeviceCount error: %d\n", deviceCount);
    return -2;
  }

  if (deviceCount == 0) {
    printf("No audio devices.\n");
    return -3;
  }

  for (int deviceIndex = 0; deviceIndex < deviceCount; deviceIndex++) {
    const PaDeviceInfo *deviceInfo = Pa_GetDeviceInfo(deviceIndex);
    printf("Available device: %s\n", deviceInfo->name);
  }

  return 0;
}

int audio_start (const char *audioDeviceName) {
  int opusFrameSize = globals_get1i(opus, frameSize);

  int deviceCount = Pa_GetDeviceCount();
  if (deviceCount < 0) {
    printf("Pa_GetDeviceCount error: %d\n", deviceCount);
    return -1;
  }

  const PaDeviceInfo *deviceInfo;
  int deviceIndex;
  for (deviceIndex = 0; deviceIndex < deviceCount; deviceIndex++) {
    deviceInfo = Pa_GetDeviceInfo(deviceIndex);
    if (strcmp(deviceInfo->name, audioDeviceName) == 0) {
      printf("Output device: %s\n", deviceInfo->name);
      if (deviceInfo->maxOutputChannels == 0) {
        printf("No output channels.\n");
        return -2;
      }
      printf("Channels: %d\n", deviceInfo->maxOutputChannels);
      if (deviceInfo->maxOutputChannels != audioChannelCount) {
        printf("Device must %d channels.\n", audioChannelCount);
        return -3;
      }
      break;
    }
  }

  if (deviceIndex == deviceCount) {
    printf("Audio device \"%s\" not found.\n", audioDeviceName);
    return -4;
  }

  PaStream *stream;
  const PaStreamInfo *streamInfo;
  PaStreamParameters params = { 0 };
  params.device = deviceIndex;
  params.channelCount = deviceInfo->maxOutputChannels;
  params.sampleFormat = paInt16;
  params.suggestedLatency = deviceInfo->defaultLowOutputLatency;
  double ioSampleRate = globals_get1i(audio, ioSampleRate);
  PaError pErr = Pa_OpenStream(&stream, NULL, &params, ioSampleRate, opusFrameSize, 0, playCallback, NULL);
  if (pErr != paNoError) {
    printf("Pa_OpenStream error: %d, %s\n", pErr, Pa_GetErrorText(pErr));
    return -5;
  }

  streamInfo = Pa_GetStreamInfo(stream);
  printf("Latency (ms): %f\n", 1000.0 * streamInfo->outputLatency);
  printf("Sample rate: %f\n", streamInfo->sampleRate);

  int err = syncer_init((double)globals_get1i(opus, sampleRate), streamInfo->sampleRate, opusFrameSize);
  if (err < 0) {
    printf("syncer_init error: %d\n", err);
    return -6;
  }

  pErr = Pa_StartStream(stream);
  if (pErr != paNoError) {
    printf("Pa_StartStream error: %d, %s\n", pErr, Pa_GetErrorText(pErr));
    return -7;
  }

  return 0;
}

int audio_enqueueBuf (const int16_t *inBuf, int inFrameCount) {
  return syncer_enqueueBuf(inBuf, inFrameCount, _ring, _ringBuf);
}
