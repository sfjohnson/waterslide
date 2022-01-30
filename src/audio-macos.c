#include <stdio.h>
#include "portaudio/portaudio.h"
#include "syncer.h"
#include "globals.h"
#include "audio.h"

#define UNUSED __attribute__((unused))

static ck_ring_t *_ring;
static ck_ring_buffer_t *_ringBuf;
static int _fullRingSize;
static int networkChannelCount, deviceChannelCount, ringMaxSize;

static int playCallback (UNUSED const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer, UNUSED const PaStreamCallbackTimeInfo* timeInfo, UNUSED PaStreamCallbackFlags statusFlags, UNUSED void *userData) {
  static bool underrun = false;
  float *outBuf = (float *)outputBuffer;
  int ringCurrentSize = ck_ring_size(_ring);
  // This condition is ensured in audio_start: outBufFloatCount >= ringFloatCount
  int outBufFrameCount = (int)framesPerBuffer;
  int outBufFloatCount = deviceChannelCount * outBufFrameCount;
  int ringFloatCount = networkChannelCount * outBufFrameCount;

  memset(outBuf, 0, 4 * outBufFloatCount);

  if (underrun) {
    // Let the ring fill up to about half-way before pulling from it again, while outputting silence.
    if (ringCurrentSize < ringMaxSize / 2) {
      return paContinue;
    } else {
      underrun = false;
    }
  }

  if (ringCurrentSize < ringFloatCount) {
    underrun = true;
    globals_add1ui(statsCh1Audio, bufferUnderrunCount, 1);
    return paContinue;
  }

  for (int i = 0; i < outBufFrameCount; i++) {
    for (int j = 0; j < networkChannelCount; j++) {
      // If networkChannelCount < deviceChannelCount, don't write to the remaining channels in outBuf,
      // they are already set to zero above.
      intptr_t outSample = 0;
      ck_ring_dequeue_spsc(_ring, _ringBuf, &outSample);
      // https://gist.github.com/shafik/848ae25ee209f698763cffee272a58f8#how-do-we-type-pun-correctly
      memcpy(&outBuf[deviceChannelCount*i + j], &outSample, 4);
    }
  }

  return paContinue;
}

int audio_init (ck_ring_t *ring, ck_ring_buffer_t *ringBuf, int fullRingSize) {
  _ring = ring;
  _ringBuf = ringBuf;
  _fullRingSize = fullRingSize;
  networkChannelCount = globals_get1i(audio, networkChannelCount);
  ringMaxSize = networkChannelCount * globals_get1i(opus, decodeRingLength);

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

  printf("\nAvailable audio devices:\n");
  for (int deviceIndex = 0; deviceIndex < deviceCount; deviceIndex++) {
    const PaDeviceInfo *deviceInfo = Pa_GetDeviceInfo(deviceIndex);
    printf("* %s\n", deviceInfo->name);
  }
  printf("\n");

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
      printf("Device channels: %d\n", deviceInfo->maxOutputChannels);
      printf("Receiver channels: %d\n", networkChannelCount);
      if (deviceInfo->maxOutputChannels < networkChannelCount) {
        printf("Device does not have enough output channels.\n");
        return -3;
      }
      break;
    }
  }

  if (deviceIndex == deviceCount) {
    printf("Audio device \"%s\" not found.\n", audioDeviceName);
    return -4;
  }

  deviceChannelCount = deviceInfo->maxOutputChannels;
  globals_set1i(audio, deviceChannelCount, deviceChannelCount);

  PaStream *stream;
  PaStreamParameters params = { 0 };
  params.device = deviceIndex;
  params.channelCount = deviceInfo->maxOutputChannels;
  params.sampleFormat = paFloat32;
  params.suggestedLatency = deviceInfo->defaultLowOutputLatency;
  PaError pErr = Pa_OpenStream(&stream, NULL, &params, globals_get1i(audio, ioSampleRate), opusFrameSize, 0, playCallback, NULL);
  if (pErr != paNoError) {
    printf("Pa_OpenStream error: %d, %s\n", pErr, Pa_GetErrorText(pErr));
    return -5;
  }

  const PaStreamInfo *streamInfo = Pa_GetStreamInfo(stream);
  double deviceLatency = streamInfo->outputLatency; // seconds
  double deviceSampleRate = streamInfo->sampleRate; // Hz
  double opusSampleRate = globals_get1i(opus, sampleRate); // Hz
  // Set the ioSampleRate global in case PortAudio gave a different sample rate to the one requested.
  globals_set1i(audio, ioSampleRate, (int)deviceSampleRate);
  printf("Device latency (ms): %f\n", 1000.0 * deviceLatency);
  printf("Sample rate: %f\n", deviceSampleRate);

  int err = syncer_init(opusSampleRate, deviceSampleRate, opusFrameSize, _ring, _ringBuf, _fullRingSize);
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

int audio_enqueueBuf (const float *inBuf, int inFrameCount, int inChannelCount) {
  return syncer_enqueueBuf(inBuf, inFrameCount, inChannelCount);
}
