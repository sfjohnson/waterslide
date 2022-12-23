#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "globals.h"
#include "protobufs/init-config.pb.h"
#include "config.h"

/*
* Base64 encoding/decoding (RFC1341)
* Copyright (c) 2005-2011, Jouni Malinen <j@w1.fi>
*
* This software may be distributed under the terms of the BSD license.
* See README for more details.
*/

static const uint8_t base64_table[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/**
 * base64_decode - Base64 decode
 * @src: Data to be decoded
 * @len: Length of the data to be decoded
 * @out_len: Pointer to output length variable
 * Returns: Allocated buffer of out_len bytes of decoded data,
 * or %NULL on failure
 *
 * Caller is responsible for freeing the returned buffer.
 */
static uint8_t *base64Decode (const uint8_t *src, size_t len, size_t *out_len) {
  uint8_t dtable[256], *out, *pos, in[4], block[4], tmp;
  size_t i, count, olen;
  memset(dtable, 0x80, 256);
  for (i = 0; i < sizeof(base64_table) - 1; i++) {
    dtable[base64_table[i]] = (uint8_t) i;
  }

  dtable[(unsigned char)'='] = 0;
  count = 0;
  for (i = 0; i < len; i++) {
    if (dtable[src[i]] != 0x80)
      count++;
  }

  if (count == 0 || count % 4) {
    return NULL;
  }

  olen = count / 4 * 3;
  pos = out = (uint8_t*)malloc(olen);
  if (out == NULL) {
    return NULL;
  }

  count = 0;
  for (i = 0; i < len; i++) {
    tmp = dtable[src[i]];
    if (tmp == 0x80)
      continue;
    in[count] = src[i];
    block[count] = tmp;
    count++;
    if (count == 4) {
      *pos++ = (block[0] << 2) | (block[1] >> 4);
      *pos++ = (block[1] << 4) | (block[2] >> 2);
      *pos++ = (block[2] << 6) | block[3];
      count = 0;
    }
  }

  if (pos > out) {
    if (in[2] == '=') {
      pos -= 2;
    } else if (in[3] == '=') {
      pos--;
    }
  }

  *out_len = pos - out;
  return out;
}

int config_init (const char *b64ConfigStr) {
  size_t bufLen = 0;
  uint8_t *buf = base64Decode((uint8_t *)b64ConfigStr, strlen(b64ConfigStr), &bufLen);

  InitConfigProto initConfig;
  initConfig.ParseFromArray(buf, bufLen);
  free(buf);

  // Validate fields with static limits

  int endpointCount = initConfig.endpoints_size();
  if (endpointCount > MAX_ENDPOINTS) {
    printf("Init config: Too many endpoints! Max is %d.\n", MAX_ENDPOINTS);
    return -1;
  }

  int audioChannelCount = initConfig.audio().channelcount();
  if (audioChannelCount > MAX_AUDIO_CHANNELS) {
    printf("Init config: Too many audio channels! Max is %d.\n", MAX_AUDIO_CHANNELS);
    return -2;
  }

  // Transfer protobuf to global store
  globals_set1i(root, mode, initConfig.mode());
  globals_set1s(root, privateKey, initConfig.privatekey().c_str());
  globals_set1s(root, peerPublicKey, initConfig.peerpublickey().c_str());

  size_t serverAddrLen = initConfig.discovery().serveraddr().length();
  const uint8_t *serverAddrBuf = (const uint8_t*)initConfig.discovery().serveraddr().c_str();
  if (serverAddrLen == 4) {
    // IPv4
    uint32_t serverAddrVal = 0;
    memcpy(&serverAddrVal, serverAddrBuf, 4);
    globals_set1ui(discovery, serverAddr, serverAddrVal);
  } else if (serverAddrLen == 16) {
    // IPv6
    printf("Init config: IPv6 is not implemented!\n");
    return -3;
  }

  globals_set1i(discovery, serverPort, initConfig.discovery().serverport());

  for (int i = 0; i < endpointCount; i++) {
    const InitConfigProto_Endpoint &endpoint = initConfig.endpoints(i);
    globals_set1sv(endpoints, interface, i, endpoint.interface().c_str());
  }

  globals_set1i(endpoints, endpointCount, endpointCount);

  globals_set1i(mux, maxChannels, initConfig.mux().maxchannels());
  globals_set1i(mux, maxPacketSize, initConfig.mux().maxpacketsize());

  globals_set1i(audio, networkChannelCount, audioChannelCount);
  globals_set1i(audio, ioSampleRate, initConfig.audio().iosamplerate());
  globals_set1s(audio, deviceName, initConfig.audio().devicename().c_str());
  globals_set1ff(audio, levelSlowAttack, initConfig.audio().levelslowattack());
  globals_set1ff(audio, levelSlowRelease, initConfig.audio().levelslowrelease());
  globals_set1ff(audio, levelFastAttack, initConfig.audio().levelfastattack());
  globals_set1ff(audio, levelFastRelease, initConfig.audio().levelfastrelease());

  if (initConfig.audio().has_opus()) {
    globals_set1ui(audio, encoding, AUDIO_ENCODING_OPUS);
    globals_set1i(opus, bitrate, initConfig.audio().opus().bitrate());
    globals_set1i(opus, frameSize, initConfig.audio().opus().framesize());
    globals_set1i(opus, maxPacketSize, initConfig.audio().opus().maxpacketsize());
    globals_set1i(opus, decodeRingLength, initConfig.audio().opus().decoderinglength());
  } else if (initConfig.audio().has_pcm()) {
    globals_set1ui(audio, encoding, AUDIO_ENCODING_PCM);
    globals_set1i(pcm, frameSize, initConfig.audio().pcm().framesize());
    globals_set1i(pcm, sampleRate, initConfig.audio().pcm().samplerate());
    globals_set1i(pcm, decodeRingLength, initConfig.audio().pcm().decoderinglength());
  }

  globals_set1i(fec, symbolLen, initConfig.fec().symbollen());
  globals_set1i(fec, sourceSymbolsPerBlock, initConfig.fec().sourcesymbolsperblock());
  globals_set1i(fec, repairSymbolsPerBlock, initConfig.fec().repairsymbolsperblock());

  globals_set1i(monitor, wsPort, initConfig.monitor().wsport());

  return 0;
}
