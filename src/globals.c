#include "globals.h"

globals_define1i(root, mode)
globals_define1s(root, privateKey, SEC_KEY_LENGTH)
globals_define1s(root, peerPublicKey, SEC_KEY_LENGTH)

globals_define1ui(discovery, serverAddr)
globals_define1i(discovery, serverPort)

globals_define1i(endpoints, endpointCount)
globals_define1sv(endpoints, interface, MAX_ENDPOINTS, MAX_NET_IF_NAME_LEN)

globals_define1i(mux, maxChannels)
globals_define1i(mux, maxPacketSize)

globals_define1i(audio, networkChannelCount)
globals_define1i(audio, deviceChannelCount)
globals_define1i(audio, ioSampleRate)
globals_define1s(audio, deviceName, MAX_DEVICE_NAME_LEN)
globals_define1ff(audio, levelSlowAttack)
globals_define1ff(audio, levelSlowRelease)
globals_define1ff(audio, levelFastAttack)
globals_define1ff(audio, levelFastRelease)
globals_define1ui(audio, encoding)

globals_define1i(opus, bitrate)
globals_define1i(opus, frameSize)
globals_define1i(opus, maxPacketSize)
globals_define1i(opus, decodeRingLength)

globals_define1i(pcm, frameSize)
globals_define1i(pcm, sampleRate)
globals_define1i(pcm, decodeRingLength)

globals_define1i(fec, symbolLen)
globals_define1i(fec, sourceSymbolsPerBlock)
globals_define1i(fec, repairSymbolsPerBlock)

globals_define1i(monitor, wsPort)

globals_define1uiv(statsEndpoints, open, MAX_ENDPOINTS)
globals_define1uiv(statsEndpoints, dupPacketCount, MAX_ENDPOINTS)
globals_define1uiv(statsEndpoints, oooPacketCount, MAX_ENDPOINTS)
globals_define1uiv(statsEndpoints, bytesOut, MAX_ENDPOINTS)
globals_define1uiv(statsEndpoints, bytesIn, MAX_ENDPOINTS)
globals_define1iv(statsCh1Endpoints, lastSbn, MAX_ENDPOINTS)
globals_define1ui(statsCh1, dupBlockCount)
globals_define1ui(statsCh1, oooBlockCount)
globals_define1uiv(statsCh1Audio, clippingCounts, MAX_AUDIO_CHANNELS)
globals_define1ffv(statsCh1Audio, levelsFast, MAX_AUDIO_CHANNELS)
globals_define1ffv(statsCh1Audio, levelsSlow, MAX_AUDIO_CHANNELS)
globals_define1ui(statsCh1Audio, streamBufferPos)
globals_define1ui(statsCh1Audio, bufferOverrunCount)
globals_define1ui(statsCh1Audio, bufferUnderrunCount)
globals_define1ui(statsCh1Audio, encodeThreadJitterCount)
globals_define1ui(statsCh1AudioOpus, codecErrorCount)
globals_define1ui(statsCh1AudioPCM, crcFailCount)
