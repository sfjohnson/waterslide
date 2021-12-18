#include <stdio.h>
#include "oboe/Oboe.h"
#include "stats.h"
#include "syncer.h"
#include "globals.h"
#include "audio.h"

static ck_ring_t *_ring;
static ck_ring_buffer_t *_ringBuf;
static std::shared_ptr<oboe::AudioStream> stream = nullptr;
static int audioChannelCount;

class : public oboe::AudioStreamDataCallback {
public:
  oboe::DataCallbackResult onAudioReady (oboe::AudioStream *audioStream, void *audioData, int32_t numFrames) {
    // We requested AudioFormat::I16. So if the stream opens
    // we know we got the I16 format.
    int16_t *outBuf = static_cast<int16_t *>(audioData);

    for (int i = 0; i < numFrames; i++) {
      intptr_t outFrame = 0;
      if (!ck_ring_dequeue_spsc(_ring, _ringBuf, &outFrame)) {
        stats_ch1.ringUnderrunCount++;
      }

      // DEBUG: max 2 channels for 32-bit arch, max 4 channels for 64-bit
      memcpy(&outBuf[audioChannelCount * i], &outFrame, 2 * audioChannelCount);
    }

    return oboe::DataCallbackResult::Continue;
  }
} audioCallback;

int audio_init (ck_ring_t *ring, ck_ring_buffer_t *ringBuf) {
  _ring = ring;
  _ringBuf = ringBuf;
  audioChannelCount = globals_get1i(audio, channelCount);

  oboe::AudioStreamBuilder builder;
  builder.setDirection(oboe::Direction::Output);
  builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
  builder.setSharingMode(oboe::SharingMode::Exclusive);
  builder.setFormat(oboe::AudioFormat::I16);
  builder.setChannelCount(oboe::ChannelCount::Stereo);
  builder.setDataCallback(&audioCallback);

  oboe::Result result = builder.openStream(stream);

  if (result != oboe::Result::OK) {
    printf("Failed to create audio stream: %s\n", oboe::convertToText(result));
    return -1;
  }

  return 0;
}

int audio_start (const char *audioDeviceName) {
  // audioDeviceName is ignored for now, Oboe uses the default device.
  if (stream == nullptr) return -1;

  printf("Sample rate: %d\n", stream->getSampleRate());
  syncer_init((double)globals_get1i(opus, sampleRate), (double)stream->getSampleRate(), globals_get1i(opus, frameSize));

  stream->requestStart();
  // stream->close();
  return 0;
}

int audio_enqueueBuf (const int16_t *inBuf, int inFrameCount) {
  return syncer_enqueueBuf(inBuf, inFrameCount, _ring, _ringBuf);
}
