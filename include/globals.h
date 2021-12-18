#ifndef _GLOBALS_H
#define _GLOBALS_H

#include "globals-defs.h"

#define MAX_ENDPOINTS 10
#define MAX_DEVICE_NAME_LEN 100

globals_declare1i(root, mode)

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

globals_declare1i(stats, lastRingSize)
globals_declare1i(stats, ringOverrunCount)
globals_declare1i(stats, ringUnderrunCount)
globals_declare1i(stats, dupBlockCount)
globals_declare1i(stats, oooBlockCount)
globals_declare1i(stats, lastBlockSbnDiff)
globals_declare1i(stats, codecErrorCount)
// globals_declare1iv(stats, audioClippingCount)
// globals_declare1ffv(stats, audioLevelsFast)
// globals_declare1ffv(stats, audioLevelsSlow)

#endif
