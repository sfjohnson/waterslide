#ifndef _GLOBALS_H
#define _GLOBALS_H

#include "globals-defs.h"

#define MAX_ENDPOINTS 16
#define MAX_DEVICE_NAME_LEN 100
#define MAX_NET_IF_NAME_LEN 20
#define MAX_AUDIO_CHANNELS 64

globals_declare1i(root, mode)

globals_declare1i(endpoints, endpointCount)
globals_declare1sv(endpoints, interface)
globals_declare1uiv(endpoints, addr)
globals_declare1iv(endpoints, port)

globals_declare1i(mux, maxChannels)
globals_declare1i(mux, maxPacketSize)

globals_declare1i(audio, channelCount)
globals_declare1i(audio, ioSampleRate)
globals_declare1s(audio, deviceName)
globals_declare1ff(audio, levelSlowAttack)
globals_declare1ff(audio, levelSlowRelease)
globals_declare1ff(audio, levelFastAttack)
globals_declare1ff(audio, levelFastRelease)

globals_declare1i(opus, bitrate)
globals_declare1i(opus, frameSize)
globals_declare1i(opus, maxPacketSize)
globals_declare1i(opus, sampleRate)
globals_declare1i(opus, encodeRingLength)
globals_declare1i(opus, decodeRingLength)

globals_declare1i(fec, symbolLen)
globals_declare1i(fec, sourceSymbolsPerBlock)
globals_declare1i(fec, repairSymbolsPerBlock)

globals_declare1i(monitor, wsPort)

globals_declare1ui(statsCh1, dupBlockCount)
globals_declare1ui(statsCh1, oooBlockCount)
globals_declare1i(statsCh1, lastBlockSbnDiff)
globals_declare1ui(statsCh1, dupPacketCount)
globals_declare1ui(statsCh1, oooPacketCount)

globals_declare1uiv(statsCh1Audio, clippingCounts)
globals_declare1ffv(statsCh1Audio, levelsFast)
globals_declare1ffv(statsCh1Audio, levelsSlow)
globals_declare1ui(statsCh1Audio, streamBufferPos)
globals_declare1ui(statsCh1Audio, bufferOverrunCount)
globals_declare1ui(statsCh1Audio, bufferUnderrunCount)
globals_declare1ui(statsCh1Audio, codecErrorCount)

#endif
