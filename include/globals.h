#ifndef _GLOBALS_H
#define _GLOBALS_H

#include "globals-defs.h"

#define MAX_ENDPOINTS 16
#define MAX_DEVICE_NAME_LEN 100
#define MAX_NET_IF_NAME_LEN 20
#define MAX_AUDIO_CHANNELS 64
#define SEC_KEY_LENGTH 44 // Length of base 64 encoded key string in chars, not including null terminator.
#define SEC_KEEP_ALIVE_INTERVAL 25 // in seconds
#define ENDPOINT_TICK_INTERVAL 100000 // in microseconds
#define ENDPOINT_REOPEN_INTERVAL 20 // in ticks (1 tick = 100 ms)

globals_declare1i(root, mode)

globals_declare1s(endpoints, privateKey)
globals_declare1s(endpoints, peerPublicKey)
globals_declare1i(endpoints, endpointCount)
globals_declare1sv(endpoints, interface)
globals_declare1uiv(endpoints, addr)
globals_declare1iv(endpoints, port)

globals_declare1i(mux, maxChannels)
globals_declare1i(mux, maxPacketSize)

globals_declare1i(audio, networkChannelCount) // Number of audio channels sent and received over network. Must be <= deviceChannelCount
globals_declare1i(audio, deviceChannelCount) // Number of audio channels supported by device (soundcard)
globals_declare1i(audio, ioSampleRate) // Sample rate of device (soundcard)
globals_declare1s(audio, deviceName)
globals_declare1ff(audio, levelSlowAttack) // Meter filtering for monitor
globals_declare1ff(audio, levelSlowRelease)
globals_declare1ff(audio, levelFastAttack)
globals_declare1ff(audio, levelFastRelease)

globals_declare1i(opus, bitrate) // In bits per second
globals_declare1i(opus, frameSize) // Normally 240 samples = 5 ms @ 48 kHz
globals_declare1i(opus, maxPacketSize) // Max size of encoded packet in bytes, containing frameSize samples
globals_declare1i(opus, sampleRate) // Normally 48000 kHz
globals_declare1i(opus, decodeRingLength) // In samples. Must be larger than frameSize

globals_declare1i(fec, symbolLen)
globals_declare1i(fec, sourceSymbolsPerBlock)
globals_declare1i(fec, repairSymbolsPerBlock)

globals_declare1i(monitor, wsPort)

globals_declare1uiv(statsEndpoints, open)
globals_declare1uiv(statsEndpoints, dupPacketCount)
globals_declare1uiv(statsEndpoints, oooPacketCount)
globals_declare1uiv(statsEndpoints, bytesOut)
globals_declare1uiv(statsEndpoints, bytesIn)
globals_declare1iv(statsCh1Endpoints, lastSbn)
globals_declare1ui(statsCh1, dupBlockCount)
globals_declare1ui(statsCh1, oooBlockCount)
globals_declare1uiv(statsCh1Audio, clippingCounts)
globals_declare1ffv(statsCh1Audio, levelsFast)
globals_declare1ffv(statsCh1Audio, levelsSlow)
globals_declare1ui(statsCh1Audio, streamBufferPos)
globals_declare1ui(statsCh1Audio, bufferOverrunCount)
globals_declare1ui(statsCh1Audio, bufferUnderrunCount)
globals_declare1ui(statsCh1Audio, encodeThreadJitterCount)
globals_declare1ui(statsCh1Audio, codecErrorCount)

#endif
