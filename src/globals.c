#include "globals.h"

globals_define1i(root, mode)

globals_define1uiv(endpoints, addr, MAX_ENDPOINTS)
globals_define1iv(endpoints, port, MAX_ENDPOINTS)

globals_define1i(mux, maxChannels)
globals_define1i(mux, maxPacketSize)

globals_define1i(audio, channelCount)
globals_define1i(audio, ioSampleRate)
globals_define1s(audio, deviceName, MAX_DEVICE_NAME_LEN)
globals_define1ff(audio, levelSlowAttack)
globals_define1ff(audio, levelSlowRelease)
globals_define1ff(audio, levelFastAttack)
globals_define1ff(audio, levelFastRelease)

globals_define1i(opus, bitrate)
globals_define1i(opus, frameSize)
globals_define1i(opus, maxPacketSize)
globals_define1i(opus, sampleRate)
globals_define1i(opus, encodeRingLength)
globals_define1i(opus, decodeRingLength)

globals_define1i(fec, symbolLen)
globals_define1i(fec, sourceSymbolsPerBlock)
globals_define1i(fec, repairSymbolsPerBlock)

globals_define1i(monitor, wsPort)

globals_define1i(stats, lastRingSize)
globals_define1i(stats, ringOverrunCount)
globals_define1i(stats, ringUnderrunCount)
globals_define1i(stats, dupBlockCount)
globals_define1i(stats, oooBlockCount)
globals_define1i(stats, lastBlockSbnDiff)
globals_define1i(stats, codecErrorCount)
