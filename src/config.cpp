// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "globals.h"
// TODO: fix up the generated protobufs code so we can turn pedantic back on
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include "protobufs/init-config.pb.h"
#pragma GCC diagnostic pop
#include "config.h"
#if defined(__linux__) || defined(__ANDROID__)
#include "tinyalsa/mixer.h"
#endif

typedef struct {
  int id;
  int *values;
  int valueCount;
} mixerControl_t;

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

#if defined(__linux__) || defined(__ANDROID__)
static int applyMixerConfig (unsigned int cardId, const google::protobuf::RepeatedPtrField<InitConfigProto_MixerControl> *controls) {
  // Save the mixer state on the first call and restore it on the second call
  static unsigned int savedCardId = 0;
  static mixerControl_t *savedControls = NULL;
  static int savedControlsCount = 0;

  struct mixer_ctl *ctl;
  struct mixer *mixer;

  if ((controls == NULL && savedControls == NULL) || (controls != NULL && savedControls != NULL)) return -1;

  if (controls == NULL) {
    // Restore original config
    mixer = mixer_open(savedCardId);
    if (mixer == NULL) return -2;

    for (int i = 0; i < savedControlsCount; i++) {
      ctl = mixer_get_ctl(mixer, savedControls[i].id);
      if (ctl == NULL) {
        mixer_close(mixer);
        return -3;
      }
      for (int j = 0; j < savedControls[i].valueCount; j++) {
        mixer_ctl_set_value(ctl, j, savedControls[i].values[j]);
      }
      delete[] savedControls[i].values;
    }

    mixer_close(mixer);
    delete[] savedControls;
    savedControls = NULL;
    savedControlsCount = 0;
    return 0;
  }

  savedCardId = cardId;
  mixer = mixer_open(cardId);
  if (mixer == NULL) return -4;

  savedControls = new (std::nothrow) mixerControl_t[controls->size()];
  if (savedControls == NULL) return -5;
  savedControlsCount = controls->size();

  for (int i = 0; i < controls->size(); i++) {
    auto control = controls->Get(i);
    ctl = mixer_get_ctl(mixer, control.id());
    if (ctl == NULL) {
      mixer_close(mixer);
      return -6;
    }
    savedControls[i].id = control.id();
    if (control.has_enumvalue()) {
      savedControls[i].values = new (std::nothrow) int[1];
      if (savedControls[i].values == NULL) return -7;
      savedControls[i].valueCount = 1;
      savedControls[i].values[0] = mixer_ctl_get_value(ctl, 0);
      mixer_ctl_set_enum_by_string(ctl, control.enumvalue().c_str());
    } else if (control.has_intvalues()) {
      unsigned int intValueCount = control.intvalues().values_size();
      if (mixer_ctl_get_num_values(ctl) != intValueCount) return -8;
      savedControls[i].values = new (std::nothrow) int[intValueCount];
      if (savedControls[i].values == NULL) return -9;
      savedControls[i].valueCount = intValueCount;
      for (unsigned int j = 0; j < intValueCount; j++) {
        savedControls[i].values[j] = mixer_ctl_get_value(ctl, j);
        mixer_ctl_set_value(ctl, j, control.intvalues().values(j));
      }
    } else {
      mixer_close(mixer);
      return -10;
    }
  }

  mixer_close(mixer);
  return 0;
}
#endif

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

  int networkChannelCount = initConfig.audio().networkchannelcount();
  if (networkChannelCount > MAX_AUDIO_CHANNELS) {
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
    uint32_t addrVal = 0;
    memcpy(&addrVal, serverAddrBuf, 4);
    globals_set1ui(discovery, serverAddr, addrVal);
  } else if (serverAddrLen == 16) {
    // IPv6
    printf("Init config: IPv6 is not implemented!\n");
    return -3;
  }

  size_t udpAddrLen = initConfig.monitor().udpaddr().length();
  const uint8_t *udpAddrBuf = (const uint8_t*)initConfig.monitor().udpaddr().c_str();
  if (udpAddrLen == 4) {
    // IPv4
    uint32_t addrVal = 0;
    memcpy(&addrVal, udpAddrBuf, 4);
    globals_set1ui(monitor, udpAddr, addrVal);
  } else if (udpAddrLen == 16) {
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

  globals_set1ui(mux, maxPacketSize, initConfig.mux().maxpacketsize());

  globals_set1i(audio, networkChannelCount, networkChannelCount);
  globals_set1ff(audio, deviceSampleRate, initConfig.audio().devicesamplerate());
  globals_set1i(audio, decodeRingLength, initConfig.audio().decoderinglength());

  #if defined(__linux__) || defined(__ANDROID__)
  if (!initConfig.audio().has_linux()) return -4;
  int err = applyMixerConfig(initConfig.audio().linux().cardid(), &initConfig.audio().linux().controls());
  if (err < 0) return err - 4;
  globals_set1i(audio, deviceChannelCount, initConfig.audio().linux().devicechannelcount());
  globals_set1i(audio, cardId, initConfig.audio().linux().cardid());
  globals_set1i(audio, deviceId, initConfig.audio().linux().deviceid());
  globals_set1i(audio, bitsPerSample, initConfig.audio().linux().bitspersample());
  globals_set1i(audio, periodSize, initConfig.audio().linux().periodsize());
  globals_set1i(audio, periodCount, initConfig.audio().linux().periodcount());
  globals_set1i(audio, loopSleep, initConfig.audio().linux().loopsleep());
  #else

  if (!initConfig.audio().has_macos()) return -15;
  globals_set1s(audio, deviceName, initConfig.audio().macos().devicename().c_str());
  #endif

  globals_set1ff(audio, levelSlowAttack, initConfig.audio().levelslowattack());
  globals_set1ff(audio, levelSlowRelease, initConfig.audio().levelslowrelease());
  globals_set1ff(audio, levelFastAttack, initConfig.audio().levelfastattack());
  globals_set1ff(audio, levelFastRelease, initConfig.audio().levelfastrelease());

  if (initConfig.audio().has_opus()) {
    globals_set1ui(audio, encoding, AUDIO_ENCODING_OPUS);
    globals_set1i(opus, bitrate, initConfig.audio().opus().bitrate());
    globals_set1i(opus, frameSize, initConfig.audio().opus().framesize());
    globals_set1i(audio, networkSampleRate, 48000);

  } else if (initConfig.audio().has_pcm()) {
    globals_set1ui(audio, encoding, AUDIO_ENCODING_PCM);
    globals_set1i(pcm, frameSize, initConfig.audio().pcm().framesize());
    if (initConfig.audio().pcm().networksamplerate() > 0 && initConfig.mode() == 1) { // receiver
      globals_set1i(audio, networkSampleRate, initConfig.audio().pcm().networksamplerate());
    } else if (initConfig.audio().pcm().networksamplerate() == 0 && initConfig.mode() == 0) { // sender
      globals_set1i(audio, networkSampleRate, initConfig.audio().devicesamplerate());
    } else {
      return -16; // audio.pcm.networkSampleRate is a receiver-only field
    }
  }

  // DEBUG: info channel test
  globals_set1iv(fec, symbolLen, 0, 64);
  globals_set1iv(fec, sourceSymbolsPerBlock, 0, 1);
  globals_set1iv(fec, repairSymbolsPerBlock, 0, 1);

  // TODO
  globals_set1iv(fec, symbolLen, 1, initConfig.fec().symbollen());
  globals_set1iv(fec, sourceSymbolsPerBlock, 1, initConfig.fec().sourcesymbolsperblock());
  globals_set1iv(fec, repairSymbolsPerBlock, 1, initConfig.fec().repairsymbolsperblock());

  globals_set1i(monitor, wsPort, initConfig.monitor().wsport());
  globals_set1i(monitor, udpPort, initConfig.monitor().udpport());

  return 0;
}

int config_deinit (void) {
  #if defined(__linux__) || defined(__ANDROID__)
  return applyMixerConfig(0, NULL);
  #else
  return 0;
  #endif
}
