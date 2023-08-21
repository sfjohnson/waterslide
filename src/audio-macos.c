#include <stdio.h>
#include <math.h>
#include "portaudio/portaudio.h"
#include "globals.h"
#include "utils.h"
#include "syncer.h"
#include "audio.h"

static PaStream *stream = NULL;
static ck_ring_t *_ring;
static ck_ring_buffer_t *_ringBuf;
static int _fullRingSize;
static bool _receiver;
static int networkChannelCount, deviceChannelCount;
static unsigned int audioEncoding;
static double deviceLatency = 0.0;

// https://www.dsprelated.com/showarticle/814.php
// x: 5-element array { x[n], x[n-1], x[n-2], x[n-3], x[n-4] }
// static double differentiator (const double *x) {
//   return (3.0f/16.0f) * (x[4] - x[0]) + (31.0f/32.0f) * (x[1] - x[3]);
// }

// This is the high-priority audio thread for receiver
static int playCallback (UNUSED const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer, UNUSED const PaStreamCallbackTimeInfo* timeInfo, UNUSED PaStreamCallbackFlags statusFlags, UNUSED void *userData) {
  static bool underrun = true;
  float *outBufFloat = (float *)outputBuffer;
  int ringCurrentSize = utils_ringSize(_ring);
  // This condition is ensured in audio_init: outBufFloatCount >= ringFloatCount
  int outBufFrameCount = (int)framesPerBuffer;
  int outBufFloatCount = deviceChannelCount * outBufFrameCount;
  int ringFloatCount = networkChannelCount * outBufFrameCount;

  memset(outBufFloat, 0, 4 * outBufFloatCount);

  syncer_onAudio(framesPerBuffer);

  if (underrun) {
    // Let the ring fill up to about half-way before pulling from it again, while outputting silence.
    if (ringCurrentSize < _fullRingSize / 2) {
      return paContinue;
    } else {
      underrun = false;
    }
  }

  // Don't ever let the ring empty completely, that way the channels stay in order
  if (ringCurrentSize < ringFloatCount) {
    underrun = true;
    globals_add1ui(statsCh1Audio, bufferUnderrunCount, 1);
    return paContinue;
  }

  for (int i = 0; i < outBufFrameCount; i++) {
    for (int j = 0; j < networkChannelCount; j++) {
      // If networkChannelCount < deviceChannelCount, don't write to the remaining channels in outBufFloat,
      // they are already set to zero above.
      double outSampleDouble = utils_ringDequeueSample(_ring, _ringBuf);
      // Setting stats here instead of in syncer_enqueueBuf allows us to see silence from underruns on the audio level monitor.
      outBufFloat[deviceChannelCount*i + j] = outSampleDouble;
      utils_setAudioStats(outSampleDouble, j);
    }
  }

  return paContinue;
}

// This is the high-priority audio thread for sender
static int recordCallback (const void *inputBuffer, UNUSED void *outputBuffer, unsigned long framesPerBuffer, UNUSED const PaStreamCallbackTimeInfo* timeInfo, UNUSED PaStreamCallbackFlags statusFlags, UNUSED void *userData) {
  const float *inBufFloat = (const float *)inputBuffer;

  switch (audioEncoding) {
    case AUDIO_ENCODING_OPUS:
      syncer_enqueueBufF32(inBufFloat, framesPerBuffer, deviceChannelCount, true);
      break;

    case AUDIO_ENCODING_PCM:
      for (unsigned long i = 0; i < framesPerBuffer; i++) {
        for (int j = 0; j < networkChannelCount; j++) {
          double inSampleDouble = inBufFloat[deviceChannelCount*i + j];
          utils_ringEnqueueSample(_ring, _ringBuf, inSampleDouble);
          utils_setAudioStats(inSampleDouble, j);
        }
      }
      break;
  }

  return paContinue;
}

int audio_init (bool receiver) {
  // macOS is 64-bit only
  #ifndef W_64_BIT_POINTERS
  return -1;
  #endif

  _receiver = receiver;
  networkChannelCount = globals_get1i(audio, networkChannelCount);
  audioEncoding = globals_get1ui(audio, encoding);

  utils_setAudioLevelFilters();

  if (Pa_Initialize() != paNoError) return -2;

  int deviceCount = Pa_GetDeviceCount();
  if (deviceCount < 0) return -3;

  if (deviceCount == 0) {
    printf("No audio devices.\n");
    return -4;
  }

  const PaDeviceInfo *foundDeviceInfo;
  int foundDeviceIndex = -1;
  char audioDeviceName[MAX_DEVICE_NAME_LEN + 1] = { 0 };
  globals_get1s(audio, deviceName, audioDeviceName, sizeof(audioDeviceName));

  printf("\nAvailable audio devices:\n");
  for (int deviceIndex = 0; deviceIndex < deviceCount; deviceIndex++) {
    const PaDeviceInfo *deviceInfo = Pa_GetDeviceInfo(deviceIndex);
    if ((receiver && deviceInfo->maxOutputChannels == 0) || (!receiver && deviceInfo->maxInputChannels == 0)) {
      continue;
    }

    printf("* %s\n", deviceInfo->name);
    if (strcmp(deviceInfo->name, audioDeviceName) == 0) {
      foundDeviceInfo = deviceInfo;
      foundDeviceIndex = deviceIndex;
    }
  }
  printf("\n");

  if (foundDeviceIndex == -1) {
    printf("Audio device %s not available for %s.\n", audioDeviceName, receiver ? "output" : "input");
    return -5;
  }

  deviceChannelCount = receiver ? foundDeviceInfo->maxOutputChannels : foundDeviceInfo->maxInputChannels;
  globals_set1i(audio, deviceChannelCount, deviceChannelCount);

  printf("%s device: %s\n", receiver ? "Output" : "Input", audioDeviceName);
  printf("Device channels: %d\n", deviceChannelCount);
  printf("%s channels: %d\n", receiver ? "Receiver" : "Sender", networkChannelCount);
  if (deviceChannelCount < networkChannelCount) {
    printf("Device does not have enough output channels.\n");
    return -6;
  }

  int framesPerCallbackBuffer = 0;
  if (receiver) {
    if (audioEncoding == AUDIO_ENCODING_OPUS) {
      framesPerCallbackBuffer = globals_get1i(opus, frameSize);
    } else if (audioEncoding == AUDIO_ENCODING_PCM) {
      framesPerCallbackBuffer = globals_get1i(pcm, frameSize);
    } else {
      return -7;
    }
  }

  double requestedDeviceSampleRate;
  globals_get1ff(audio, deviceSampleRate, &requestedDeviceSampleRate);

  PaStreamParameters params = { 0 };
  params.device = foundDeviceIndex;
  params.channelCount = deviceChannelCount;
  params.sampleFormat = paFloat32;
  params.suggestedLatency = receiver ? foundDeviceInfo->defaultLowOutputLatency : foundDeviceInfo->defaultLowInputLatency;
  PaError pErr = Pa_OpenStream(
    &stream,
    receiver ? NULL : &params,
    receiver ? &params : NULL,
    requestedDeviceSampleRate,
    framesPerCallbackBuffer,
    0,
    receiver ? playCallback : recordCallback,
    NULL
  );
  if (pErr != paNoError) return -8;

  const PaStreamInfo *streamInfo = Pa_GetStreamInfo(stream);
  deviceLatency = receiver ? streamInfo->outputLatency : streamInfo->inputLatency; // seconds
  double actualDeviceSampleRate = streamInfo->sampleRate; // Hz

  if (audioEncoding == AUDIO_ENCODING_PCM && !receiver && requestedDeviceSampleRate != actualDeviceSampleRate) {
    printf("We requested %f Hz but the device requires %f Hz. This is only an issue when using PCM encoding.\n", requestedDeviceSampleRate, actualDeviceSampleRate);
    return -9;
  }

  // Set the deviceSampleRate global in case PortAudio gave a different sample rate to the one requested.
  globals_set1ff(audio, deviceSampleRate, actualDeviceSampleRate);

  printf("Device latency (ms): %f\n", 1000.0 * deviceLatency);
  printf("Device sample rate: %f\n", actualDeviceSampleRate);

  return 0;
}

double audio_getDeviceLatency (void) {
  return deviceLatency;
}

int audio_start (ck_ring_t *ring, ck_ring_buffer_t *ringBuf, unsigned int fullRingSize) {
  if (stream == NULL) return -1;

  _ring = ring;
  _ringBuf = ringBuf;
  _fullRingSize = fullRingSize;

  int err = 0;
  double networkSampleRate = globals_get1i(audio, networkSampleRate);
  double deviceSampleRate;
  globals_get1ff(audio, deviceSampleRate, &deviceSampleRate);

  if (!_receiver && audioEncoding == AUDIO_ENCODING_OPUS) {
    // Calculate the maximum value that framesPerBuffer could be in recordCallback, leaving plenty of spare room.
    int framesPerCallbackBuffer = 3.0 * deviceLatency * deviceSampleRate;
    err = syncer_init(deviceSampleRate, networkSampleRate, framesPerCallbackBuffer, ring, ringBuf, fullRingSize);
  } else if (_receiver) {
    int framesPerCallbackBuffer;
    if (audioEncoding == AUDIO_ENCODING_OPUS) {
      framesPerCallbackBuffer = globals_get1i(opus, frameSize);
    } else {
      framesPerCallbackBuffer = globals_get1i(pcm, frameSize);
    }
    err = syncer_init(networkSampleRate, deviceSampleRate, framesPerCallbackBuffer, ring, ringBuf, fullRingSize);
  } else {
    // PCM sender uses deviceSampleRate, no syncer required.
  }
  if (err < 0) return err - 1;

  if (Pa_StartStream(stream) != paNoError) return -3;

  return 0;
}

int audio_deinit (void) {
  // TODO
  return 0;
}
