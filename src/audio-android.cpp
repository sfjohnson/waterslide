#include <stdio.h>
#include "oboe/Oboe.h"
#include "syncer.h"
#include "globals.h"
#include "audio.h"

#define UNUSED __attribute__((unused))

static ck_ring_t *_ring;
static ck_ring_buffer_t *_ringBuf;
static int _fullRingSize;
static std::shared_ptr<oboe::AudioStream> stream = nullptr;
static int networkChannelCount, deviceChannelCount, ringMaxSize;

class : public oboe::AudioStreamDataCallback {
public:
  oboe::DataCallbackResult onAudioReady (UNUSED oboe::AudioStream *audioStream, void *audioData, int32_t numFrames) {
    // We requested AudioFormat::Float. So if the stream opens
    // we know we got the Float format.
    float *outBuf = static_cast<float *>(audioData);
    static bool underrun = false;
    int ringCurrentSize = ck_ring_size(_ring);
    int outBufFloatCount = deviceChannelCount * numFrames;
    int ringFloatCount = networkChannelCount * numFrames;

    memset(outBuf, 0, 4 * outBufFloatCount);

    if (underrun) {
      // Let the ring fill up to about half-way before pulling from it again, while outputting silence.
      if (ringCurrentSize < ringMaxSize / 2) {
        return oboe::DataCallbackResult::Continue;
      } else {
        underrun = false;
      }
    }

    if (ringCurrentSize < ringFloatCount) {
      underrun = true;
      globals_add1ui(statsCh1Audio, bufferUnderrunCount, 1);
      return oboe::DataCallbackResult::Continue;
    }

    for (int i = 0; i < numFrames; i++) {
      for (int j = 0; j < networkChannelCount; j++) {
        // If networkChannelCount > deviceChannelCount, dequeue the sample then discard it.
        // If networkChannelCount < deviceChannelCount, don't write to the remaining channels in outBuf,
        // they are already set to zero above.
        intptr_t outSample = 0;
        ck_ring_dequeue_spsc(_ring, _ringBuf, &outSample);
        if (j < deviceChannelCount) {
          // https://gist.github.com/shafik/848ae25ee209f698763cffee272a58f8#how-do-we-type-pun-correctly
          memcpy(&outBuf[deviceChannelCount*i + j], &outSample, 4);
        }
      }
    }

    return oboe::DataCallbackResult::Continue;
  }
} audioCallback;

int audio_init (ck_ring_t *ring, ck_ring_buffer_t *ringBuf, int fullRingSize) {
  _ring = ring;
  _ringBuf = ringBuf;
  _fullRingSize = fullRingSize;
  networkChannelCount = globals_get1i(audio, networkChannelCount);
  ringMaxSize = networkChannelCount * globals_get1i(opus, decodeRingLength);
  // NOTE: No multi-channel support on Android, deviceChannelCount global var is ignored.
  deviceChannelCount = 2;

  oboe::AudioStreamBuilder builder;
  builder.setDirection(oboe::Direction::Output);
  builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
  builder.setSharingMode(oboe::SharingMode::Exclusive);
  builder.setFormat(oboe::AudioFormat::Float);
  builder.setChannelCount(oboe::ChannelCount::Stereo);
  builder.setDataCallback(&audioCallback);

  oboe::Result result = builder.openStream(stream);

  if (result != oboe::Result::OK) {
    printf("Failed to create audio stream: %s\n", oboe::convertToText(result));
    return -1;
  }

  return 0;
}

int audio_start (UNUSED const char *audioDeviceName) {
  // NOTE: audioDeviceName is ignored for now, Oboe uses the default device.
  if (stream == nullptr) return -1;

  int32_t ioSampleRate = stream->getSampleRate();
  globals_set1i(audio, ioSampleRate, ioSampleRate);
  printf("Sample rate: %d\n", ioSampleRate);
  int err = syncer_init((double)AUDIO_OPUS_SAMPLE_RATE, (double)ioSampleRate, globals_get1i(opus, frameSize), _ring, _ringBuf, _fullRingSize);
  if (err < 0) {
    printf("syncer_init error: %d\n", err);
    return -2;
  }

  stream->requestStart();
  // stream->close();
  return 0;
}

int audio_enqueueBuf (const float *inBuf, int inFrameCount, int inChannelCount) {
  return syncer_enqueueBuf(inBuf, inFrameCount, inChannelCount);
}
