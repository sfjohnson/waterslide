#include <stdio.h>
#include <math.h>
#include "portaudio/portaudio.h"
#include "syncer.h"
#include "globals.h"
#include "audio.h"

#define UNUSED __attribute__((unused))

static ck_ring_t *_ring;
static ck_ring_buffer_t *_ringBuf;
static int _fullRingSize;
static int networkChannelCount, deviceChannelCount, ringMaxSize, audioFrameSize;
static double encodedSampleRate; // Hz
static double levelFastAttack, levelFastRelease;
static double levelSlowAttack, levelSlowRelease;

// DEBUG: change to float not double?
// NOTE: this must be audio callback safe.
static void setAudioStats (double sample, int channel) {
  if (sample >= 1.0 || sample <= -1.0) {
    globals_add1uiv(statsCh1Audio, clippingCounts, channel, 1);
  }

  double levelFast, levelSlow;
  globals_get1ffv(statsCh1Audio, levelsFast, channel, &levelFast);
  globals_get1ffv(statsCh1Audio, levelsSlow, channel, &levelSlow);

  double levelFastDiff = fabs(sample) - levelFast;
  double levelSlowDiff = fabs(sample) - levelSlow;

  if (levelFastDiff > 0) {
    levelFast += levelFastAttack * levelFastDiff;
  } else {
    levelFast += levelFastRelease * levelFastDiff;
  }
  if (levelSlowDiff > 0) {
    levelSlow += levelSlowAttack * levelSlowDiff;
  } else {
    levelSlow += levelSlowRelease * levelSlowDiff;
  }

  globals_set1ffv(statsCh1Audio, levelsFast, channel, levelFast);
  globals_set1ffv(statsCh1Audio, levelsSlow, channel, levelSlow);
}

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

  // Don't ever let the ring empty completely, that way the channels stay in order
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
      float outSampleFloat;
      ck_ring_dequeue_spsc(_ring, _ringBuf, &outSample);
      // https://gist.github.com/shafik/848ae25ee209f698763cffee272a58f8#how-do-we-type-pun-correctly
      memcpy(&outSampleFloat, &outSample, 4);
      outBuf[deviceChannelCount*i + j] = outSampleFloat;

      // setAudioStats will detect clipping and tell the user
      setAudioStats(outSampleFloat, j);
    }
  }

  return paContinue;
}

int audio_init (ck_ring_t *ring, ck_ring_buffer_t *ringBuf, int fullRingSize) {
  unsigned int audioEncoding = globals_get1ui(audio, encoding);
  int decodeRingLength;

  switch (audioEncoding) {
    case AUDIO_ENCODING_OPUS:
      audioFrameSize = globals_get1i(opus, frameSize);
      encodedSampleRate = AUDIO_OPUS_SAMPLE_RATE;
      decodeRingLength = globals_get1i(opus, decodeRingLength);
      break;
    case AUDIO_ENCODING_PCM:
      audioFrameSize = globals_get1i(pcm, frameSize);
      encodedSampleRate = globals_get1i(pcm, sampleRate);
      decodeRingLength = globals_get1i(pcm, decodeRingLength);
      break;
    default:
      return -1;
  }

  _ring = ring;
  _ringBuf = ringBuf;
  _fullRingSize = fullRingSize;
  networkChannelCount = globals_get1i(audio, networkChannelCount);
  ringMaxSize = networkChannelCount * decodeRingLength;

  globals_get1ff(audio, levelFastAttack, &levelFastAttack);
  globals_get1ff(audio, levelFastRelease, &levelFastRelease);
  globals_get1ff(audio, levelSlowAttack, &levelSlowAttack);
  globals_get1ff(audio, levelSlowRelease, &levelSlowRelease);

  PaError pErr = Pa_Initialize();
  if (pErr != paNoError) {
    printf("Pa_Initialize error: %d\n", pErr);
    return -2;
  }

  int deviceCount = Pa_GetDeviceCount();
  if (deviceCount < 0) {
    printf("Pa_GetDeviceCount error: %d\n", deviceCount);
    return -3;
  }

  if (deviceCount == 0) {
    printf("No audio devices.\n");
    return -4;
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
  PaError pErr = Pa_OpenStream(&stream, NULL, &params, globals_get1i(audio, ioSampleRate), audioFrameSize, 0, playCallback, NULL);
  if (pErr != paNoError) {
    printf("Pa_OpenStream error: %d, %s\n", pErr, Pa_GetErrorText(pErr));
    return -5;
  }

  const PaStreamInfo *streamInfo = Pa_GetStreamInfo(stream);
  double deviceLatency = streamInfo->outputLatency; // seconds
  double deviceSampleRate = streamInfo->sampleRate; // Hz
  // Set the ioSampleRate global in case PortAudio gave a different sample rate to the one requested.
  globals_set1i(audio, ioSampleRate, (int)deviceSampleRate);
  printf("Device latency (ms): %f\n", 1000.0 * deviceLatency);
  printf("Sample rate: %f\n", deviceSampleRate);

  int err = syncer_init(encodedSampleRate, deviceSampleRate, audioFrameSize, _ring, _ringBuf, _fullRingSize);
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
