#ifndef _GLOBALS_H
#define _GLOBALS_H

#include "globals-defs.h"

#define UNUSED __attribute__((unused))

#if INTPTR_MAX == INT32_MAX
  #define W_32_BIT_POINTERS
#elif INTPTR_MAX == INT64_MAX
  #define W_64_BIT_POINTERS
#else
  #error "Could not determine if pointers are 32-bit or 64-bit!"
#endif

#define AUDIO_ENCODING_OPUS 0
#define AUDIO_ENCODING_PCM 1
#define AUDIO_OPUS_SAMPLE_RATE 48000
// Linearly change the mix this much for every audio frame e.g. 0.01 means it takes 100 frames or
// ~2.1 ms @ 48 kHz for the sample rate to fully change.
#define SYNCER_SWITCH_SPEED 0.01
// While syncer is mixing between two resamplers to seamlessly change sample rate, their output frame
// count may not match, in that case any overflow frames are stored in a buffer with length
// SYNCER_AB_MIX_OVERFLOW_MAX_FRAMES. This buffer should never get very full because if the two
// resamplers are getting significantly out of phase it will cause artifacts. To avoid this, only change
// the sample rate by a small amount each time.
#define SYNCER_AB_MIX_OVERFLOW_MAX_FRAMES 64
// In percent. For example: for 44100 Hz and 8% the transition frequency is (1 - 8/100) * (44100 / 2) = 20286 Hz
#define SYNCER_TRANSITION_BAND 8.0
#define MAX_ENDPOINTS 16
#define MAX_DEVICE_NAME_LEN 100
#define MAX_NET_IF_NAME_LEN 20
#define MAX_AUDIO_CHANNELS 64
#define SEC_KEY_LENGTH 44 // Length of base 64 encoded key string in chars, not including null terminator.
#define SEC_KEEP_ALIVE_INTERVAL 1 // in seconds
#define ENDPOINT_TICK_INTERVAL 100000 // in microseconds
#define ENDPOINT_REOPEN_INTERVAL 20 // in ticks (1 tick = 100 ms)
#define STATS_STREAM_METER_BINS 512
#define STATS_BLOCK_TIMING_RING_LEN 512

globals_declare1i(root, mode)
globals_declare1s(root, privateKey)
globals_declare1s(root, peerPublicKey)

globals_declare1ui(discovery, serverAddr)
globals_declare1i(discovery, serverPort)

globals_declare1i(endpoints, endpointCount)
globals_declare1sv(endpoints, interface)

globals_declare1i(mux, maxChannels)
globals_declare1i(mux, maxPacketSize)

globals_declare1i(audio, networkChannelCount) // Number of audio channels sent and received over network. Must be <= deviceChannelCount
globals_declare1i(audio, deviceChannelCount) // Number of audio channels supported by device (soundcard). Pulled from driver for macOS and pulled from config for Linux.
globals_declare1i(audio, networkSampleRate)
globals_declare1i(audio, deviceSampleRate)
globals_declare1i(audio, decodeRingLength) // In samples. Must be larger than frameSize (Opus or PCM). Affects receive latency.
globals_declare1s(audio, deviceName) // macOS only
globals_declare1i(audio, cardId) // Linux only
globals_declare1i(audio, deviceId) // Linux only
globals_declare1i(audio, bitsPerSample) // Linux only
globals_declare1i(audio, periodSize) // Linux only, in samples. Audio callback latency is periodSize*periodCount/2
globals_declare1i(audio, periodCount) // Linux only, in samples. Setting this higher improves DMA pointer resolution on some systems. Set periodSize lower to compensate for this.
globals_declare1i(audio, loopSleep) // Linux only. RT loop sleeps for this many microseconds after running the audio callback. Set low enough for no xruns, but high enough for reasonable CPU usage.

globals_declare1ff(audio, levelSlowAttack) // Meter filtering for monitor
globals_declare1ff(audio, levelSlowRelease)
globals_declare1ff(audio, levelFastAttack)
globals_declare1ff(audio, levelFastRelease)
globals_declare1ui(audio, encoding)

globals_declare1i(opus, bitrate) // In bits per second
globals_declare1i(opus, frameSize) // Normally 240 samples = 5 ms @ 48 kHz
globals_declare1i(opus, maxPacketSize) // Max size of encoded packet in bytes, containing frameSize samples

globals_declare1i(pcm, frameSize) // In samples. Packet size in bytes is 3 * channelCount * frameSize + 2
globals_declare1i(pcm, sampleRate)

globals_declare1i(fec, symbolLen)
globals_declare1i(fec, sourceSymbolsPerBlock)
globals_declare1i(fec, repairSymbolsPerBlock)

globals_declare1i(monitor, wsPort)

globals_declare1uiv(statsEndpoints, open)
globals_declare1uiv(statsEndpoints, bytesOut)
globals_declare1uiv(statsEndpoints, bytesIn)
globals_declare1uiv(statsEndpoints, sendCongestion)
globals_declare1iv(statsCh1Endpoints, lastSbn)
globals_declare1ui(statsCh1, dupBlockCount)
globals_declare1ui(statsCh1, oooBlockCount)
globals_declare1ui(statsCh1, blockTimingRingPos) // NOTE: blockTimingRingPos must only be written to in one place by one thread
globals_declare1uiv(statsCh1, blockTimingRing)
globals_declare1uiv(statsCh1Audio, clippingCounts)
globals_declare1ffv(statsCh1Audio, levelsFast)
globals_declare1ffv(statsCh1Audio, levelsSlow)
globals_declare1i(statsCh1Audio, streamBufferSize)
globals_declare1uiv(statsCh1Audio, streamMeterBins)
globals_declare1ui(statsCh1Audio, bufferOverrunCount)
globals_declare1ui(statsCh1Audio, bufferUnderrunCount)
globals_declare1ui(statsCh1Audio, encodeThreadJitterCount)
globals_declare1ui(statsCh1Audio, audioLoopXrunCount)
globals_declare1i(statsCh1Audio, receiverSync) // Tracks drift between sender and receiver audio callbacks
globals_declare1ff(statsCh1Audio, receiverSyncFilt)
globals_declare1ui(statsCh1AudioOpus, codecErrorCount)
globals_declare1ui(statsCh1AudioPCM, crcFailCount)
globals_declare1ui(statsCh1Audio, codecRingActive) // Is set to true once both sides of the ring are enqueuing/dequeuing samples (decodeRing for receiver or encodeRing for sender)

#endif
