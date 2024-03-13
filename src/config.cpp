// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <atomic>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "globals.h"
#include "utils.h"
// the Abseil people don't "endorse" -Wpedantic *eyeroll*
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include "protobufs/init-config.pb.h"
#pragma GCC diagnostic pop
#include "config.h"
#if defined(__linux__) || defined(__ANDROID__)
#include "tinyalsa/mixer.h"
#endif

static atomic_bool decodedInitialConfig = false;
static uint8_t *initConfigBuf = NULL;
static size_t initConfigBufLen = 0;

typedef struct {
  int id;
  int *values;
  int valueCount;
} mixerControl_t;

#if defined(__linux__) || defined(__ANDROID__)
static int applyMixerConfig (unsigned int cardId, const google::protobuf::RepeatedPtrField<Audio_MixerControl> *controls) {
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

static int parseAddr (const std::string &addrStr, uint32_t *addrValOut) {
  const uint8_t *buf = (const uint8_t*)addrStr.c_str();
  size_t bufLen = addrStr.length();
  if (bufLen == 4) {
    memcpy(addrValOut, buf, 4);
    return 4;
  } else if (bufLen == 16) {
    // IPv6
    printf("Init config: IPv6 is not implemented!\n");
    return -1;
  } else {
    return -2;
  }
}

static int parseAudio (int mode, const Audio &audio, const Audio_SenderReceiver &senderReceiver) {
  int networkChannelCount = audio.networkchannelcount();
  if (networkChannelCount <= 0) {
    printf("Init config: networkChannelCount required.\n");
    return -1;
  }
  if (networkChannelCount > MAX_AUDIO_CHANNELS) {
    printf("Init config: Too many audio channels! Max is %d.\n", MAX_AUDIO_CHANNELS);
    return -2;
  }
  globals_set1i(audio, networkChannelCount, networkChannelCount);

  if (audio.has_opus()) {
    globals_set1ui(audio, encoding, AUDIO_ENCODING_OPUS);
    globals_set1i(opus, bitrate, audio.opus().bitrate());
    globals_set1i(opus, frameSize, audio.opus().framesize());
    globals_set1i(audio, networkSampleRate, 48000);
  } else if (audio.has_pcm()) {
    globals_set1ui(audio, encoding, AUDIO_ENCODING_PCM);
    globals_set1i(pcm, frameSize, audio.pcm().framesize());

    int networkSampleRate = audio.pcm().networksamplerate();
    if (mode == 0) { // receiver
      if (networkSampleRate > 0) globals_set1i(audio, networkSampleRate, networkSampleRate);
    } else { // sender, set networkSampleRate to deviceSampleRate
      globals_set1i(audio, networkSampleRate, senderReceiver.devicesamplerate());
    }
  } else {
    printf("Init config: audio: opus or pcm field required.\n");
    return -3;
  }

  globals_set1ff(audio, deviceSampleRate, senderReceiver.devicesamplerate());
  globals_set1i(audio, decodeRingLength, senderReceiver.decoderinglength());

  #if defined(__linux__) || defined(__ANDROID__)
  if (!senderReceiver.has_linux()) {
    printf("Init config: audio: linux field required.\n");
    return -4;
  }
  auto linux = senderReceiver.linux();
  int err = applyMixerConfig(linux.cardid(), &linux.controls());
  if (err < 0) return err - 4;
  globals_set1i(audio, deviceChannelCount, linux.devicechannelcount());
  globals_set1i(audio, cardId, linux.cardid());
  globals_set1i(audio, deviceId, linux.deviceid());
  globals_set1i(audio, bitsPerSample, linux.bitspersample());
  globals_set1i(audio, periodSize, linux.periodsize());
  globals_set1i(audio, periodCount, linux.periodcount());
  globals_set1i(audio, loopSleep, linux.loopsleep());
  #else
  if (!senderReceiver.has_macos()) {
    printf("Init config: audio: macos field required.\n");
    return -15;
  }
  globals_set1s(audio, deviceName, senderReceiver.macos().devicename().c_str());
  #endif

  globals_set1ff(audio, levelSlowAttack, senderReceiver.levelslowattack());
  globals_set1ff(audio, levelSlowRelease, senderReceiver.levelslowrelease());
  globals_set1ff(audio, levelFastAttack, senderReceiver.levelfastattack());
  globals_set1ff(audio, levelFastRelease, senderReceiver.levelfastrelease());
  return 0;
}

int config_parseBuf (const uint8_t *buf, size_t bufLen) {
  if (!decodedInitialConfig) return -1;

  InitConfigProto initConfig;
  initConfig.ParseFromArray(buf, bufLen);

  // Transfer protobuf to global store
  // unset is the same as 0, so when a proto is parsed from the config channel, mode will be set to receiver
  int mode = initConfig.mode();
  globals_set1i(root, mode, mode);

  int err;
  uint32_t serverAddr = 0;
  if (initConfig.has_discovery()) {
    globals_set1i(discovery, serverPort, initConfig.discovery().serverport());
    err = parseAddr(initConfig.discovery().serveraddr(), &serverAddr);
    if (err < 0) return err - 1;
    globals_set1ui(discovery, serverAddr, serverAddr);
  }

  int endpointCount = initConfig.endpoints_size();
  if (endpointCount > MAX_ENDPOINTS) {
    printf("Init config: Too many endpoints! Max is %d.\n", MAX_ENDPOINTS);
    return -4;
  }
  if (endpointCount > 0) {
    for (int i = 0; i < endpointCount; i++) {
      auto endpoint = initConfig.endpoints(i);
      globals_set1sv(endpoints, interface, i, endpoint.interface().c_str());
    }
    globals_set1i(endpoints, endpointCount, endpointCount);
  }

  if (initConfig.privatekey().length() == 44) {
    globals_set1s(root, privateKey, initConfig.privatekey().c_str());
  }
  if (initConfig.peerpublickey().length() == 44) {
    globals_set1s(root, peerPublicKey, initConfig.peerpublickey().c_str());
  }

  if (initConfig.has_mux()) {
    globals_set1ui(mux, maxPacketSize, initConfig.mux().maxpacketsize());
  }

  if (initConfig.has_audio()) {
    if (initConfig.audio().has_receiver() && mode == 0) {
      err = parseAudio(mode, initConfig.audio(), initConfig.audio().receiver());
    } else if (initConfig.audio().has_sender() && mode == 1) {
      err = parseAudio(mode, initConfig.audio(), initConfig.audio().sender());
    } else {
      printf("Init config: audio: sender or receiver field required.\n");
      return -5;
    }

    if (err < 0) return err - 5;
  }

  // TODO: parse video config

  int fecCount = initConfig.fec_size();
  if (fecCount == 0) {
    printf("Init config: fec field required\n");
    return -21;
  }
  for (int i = 0; i < fecCount; i++) {
    auto fec = initConfig.fec(i);
    globals_set1iv(fec, symbolLen, fec.chid(), fec.symbollen());
    globals_set1iv(fec, sourceSymbolsPerBlock, fec.chid(), fec.sourcesymbolsperblock());
    globals_set1iv(fec, repairSymbolsPerBlock, fec.chid(), fec.repairsymbolsperblock());
  }

  int wsPort = 0, udpPort = 0;
  uint32_t udpAddr = 0;

  if (mode == 1) { // mode == sender
    if (!initConfig.monitor().has_receiver() || !initConfig.monitor().has_sender()) {
      printf("Init config: monitor: sender and receiver fields required.\n");
      return -22;
    }
    wsPort = initConfig.monitor().sender().wsport();
    udpPort = initConfig.monitor().sender().udpport();
    err = parseAddr(initConfig.monitor().sender().udpaddr(), &udpAddr);
  } else if (initConfig.monitor().has_receiver()) { // mode == receiver
    wsPort = initConfig.monitor().receiver().wsport();
    udpPort = initConfig.monitor().receiver().udpport();
    err = parseAddr(initConfig.monitor().receiver().udpaddr(), &udpAddr);
  }

  if (udpPort > 0) { // monitor UDP mode
    if (err < 0) return err - 22; // failed to parse udpAddr
    globals_set1ui(monitor, udpAddr, udpAddr);
    globals_set1i(monitor, udpPort, udpPort);
  }

  globals_set1i(monitor, wsPort, wsPort);
  // we don't need to parse uiPort; it's just for the Frontend code

  return 0;
}

int config_encodeReceiverConfig (uint8_t **destBuf) {
  if (!decodedInitialConfig) return -1;

  InitConfigProto initConfig;
  initConfig.ParseFromArray(initConfigBuf, initConfigBufLen);

  initConfig.clear_mode();
  initConfig.clear_discovery();
  initConfig.clear_endpoints();
  initConfig.clear_privatekey(); // important code here!!
  initConfig.clear_peerpublickey();
  initConfig.clear_mux();
  initConfig.mutable_audio()->clear_sender();
  // TODO: video
  initConfig.mutable_monitor()->clear_sender();

  // erase FEC channel 0 (config channel)
  for (auto it = initConfig.fec().begin(); it != initConfig.fec().end(); it++) {
    if (it->chid() == 0) {
      initConfig.mutable_fec()->erase(it);
      break;
    }
  }

  std::string destStr;
  initConfig.SerializeToString(&destStr);

  *destBuf = (uint8_t*)malloc(destStr.length());
  if (*destBuf == NULL) return -2;
  memcpy(*destBuf, destStr.c_str(), destStr.length());
  return destStr.length();
}

/////////////////////
// init, deinit
/////////////////////

int config_init (const char *b64ConfigStr) {
  initConfigBuf = utils_base64Decode(
    (uint8_t *)b64ConfigStr,
    strlen(b64ConfigStr),
    &initConfigBufLen
  );
  if (initConfigBufLen == 0 || initConfigBuf == NULL) return -1;
  decodedInitialConfig = true;

  int err = config_parseBuf(initConfigBuf, initConfigBufLen);
  return err < 0 ? err - 1 : err;
}

int config_deinit (void) {
  if (!decodedInitialConfig) return 0;

  free(initConfigBuf);

  #if defined(__linux__) || defined(__ANDROID__)
  return applyMixerConfig(0, NULL);
  #else
  return 0;
  #endif
}
