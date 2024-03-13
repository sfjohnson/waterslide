// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "xwait.h"
#include <stdio.h>
#include <stdlib.h>
#include "opus/opus_multistream.h"
#include "globals.h"
#include "demux.h"
#include "syncer.h"
#include "audio.h"
#include "utils.h"
#include "pcm.h"
#include "endpoint.h"
#include "config.h"
#include "receiver.h"

static OpusMSDecoder *decoder = NULL;
static pcm_codec_t pcmDecoder = { 0 };
static ck_ring_t decodeRing;
static ck_ring_buffer_t *decodeRingBuf;
static xwait_t configWaitHandle;
static uint8_t *receivedConfigData = NULL;
static int receivedConfigDataLen = 0;

static unsigned int audioEncoding;
static int networkChannelCount;
static int encodedPacketSize, audioFrameSize, decodeRingMaxSize;
static float *sampleBufFloat;

void onDataConfigChannel (const uint8_t *data, int dataLen) {
  // here we are in the realtime decode thread created by demux_addChannel, one thread per channel

  // DEBUG: I'm only allowing one shot at parsing the data. Could it be invalid during block loss?
  // If so, need to add a CRC
  if (receivedConfigData != NULL) return;

  receivedConfigData = (uint8_t *)malloc(dataLen);
  memcpy(receivedConfigData, data, dataLen);
  receivedConfigDataLen = dataLen;
  xwait_notify(&configWaitHandle);
}

void onDataAudioChannel (const uint8_t *buf, int len) {
  // static is OK here because onDataAudioChannel is only called from a single thread
  static bool overrun = false;

  if (len != encodedPacketSize) return;

  int seq = utils_readU16LE(buf);
  buf += 2;
  len -= 2;

  // update receiver sync
  syncer_onPacket(seq, audioFrameSize);

  int ringCurrentSize = utils_ringSize(&decodeRing);
  const uint8_t *pcmSamples;
  int result;

  if (audioEncoding == AUDIO_ENCODING_OPUS) {
    result = opus_multistream_decode_float(decoder, buf, len, sampleBufFloat, audioFrameSize, 0);
    if (result != audioFrameSize) {
      globals_add1ui(statsCh1AudioOpus, codecErrorCount, 1);
      return;
    }
  } else { // audioEncoding == AUDIO_ENCODING_PCM
    result = pcm_decode(&pcmDecoder, buf, len, &pcmSamples);
    if (result != networkChannelCount * audioFrameSize) {
      if (result == -3) globals_add1ui(statsCh1AudioPCM, crcFailCount, 1);
      return;
    }
  }

  if (overrun) {
    // Let the audio callback empty the ring to about half-way before pushing to it again.
    if (ringCurrentSize > decodeRingMaxSize / 2) {
      return;
    } else {
      overrun = false;
    }
  }

  if (audioEncoding == AUDIO_ENCODING_OPUS) {
    result = syncer_enqueueBufF32(sampleBufFloat, audioFrameSize, networkChannelCount, false);
  } else { // audioEncoding == AUDIO_ENCODING_PCM
    result = syncer_enqueueBufS24Packed(pcmSamples, audioFrameSize, networkChannelCount, false);
  }

  if (result == -1) {
    globals_add1ui(statsCh1Audio, bufferOverrunCount, 1);
    overrun = true;
  }
}

int receiver_waitForConfig (void) {
  xwait_wait(&configWaitHandle);

  int err = config_parseBuf(receivedConfigData, receivedConfigDataLen);
  if (err < 0) {
    // DEBUG: log
    printf("config_parseBuf error: %d\n", err);
    return -1;
  }

  networkChannelCount = globals_get1i(audio, networkChannelCount);
  audioEncoding = globals_get1ui(audio, encoding);

  unsigned char mapping[networkChannelCount]; // only for opus but can't put this inside switch case
  switch (audioEncoding) {
    case AUDIO_ENCODING_OPUS:
      audioFrameSize = globals_get1i(opus, frameSize);
      // CBR + 2 bytes for sequence number
      encodedPacketSize = globals_get1i(opus, bitrate) * globals_get1i(opus, frameSize) / (8 * AUDIO_OPUS_SAMPLE_RATE) + 2;

      for (int i = 0; i < networkChannelCount; i++) mapping[i] = i;
      decoder = opus_multistream_decoder_create(AUDIO_OPUS_SAMPLE_RATE, networkChannelCount, networkChannelCount, 0, mapping, &err);
      if (err < 0) return -2;
      break;

    case AUDIO_ENCODING_PCM:
      audioFrameSize = globals_get1i(pcm, frameSize);
      // 24-bit samples + 2 bytes for CRC + 2 bytes for sequence number
      encodedPacketSize = 3 * networkChannelCount * audioFrameSize + 4;
      break;

    default:
      printf("Error: Audio encoding %d not implemented.\n", audioEncoding);
      return -3;
  }

  int decodeRingLength = globals_get1i(audio, decodeRingLength);
  decodeRingMaxSize = networkChannelCount * decodeRingLength;
  globals_set1i(statsCh1Audio, streamBufferSize, decodeRingLength);

  sampleBufFloat = (float *)malloc(4 * networkChannelCount * audioFrameSize);
  if (sampleBufFloat == NULL) return -4;

  if (utils_ringInit(&decodeRing, &decodeRingBuf, decodeRingMaxSize) < 0) return -5;

  err = audio_init(true);
  if (err < 0) return err - 5;

  // start audio before demux_addChannel so that we don't call syncer_enqueueBuf before
  // audio module has called syncer_init
  err = audio_start(&decodeRing, decodeRingBuf, decodeRingMaxSize);
  if (err < 0) return err - 100;

  err = demux_addChannel(
    encodedPacketSize,
    globals_get1iv(fec, sourceSymbolsPerBlock, 1),
    globals_get1iv(fec, repairSymbolsPerBlock, 1),
    globals_get1iv(fec, symbolLen, 1),
    onDataAudioChannel
  );
  if (err < 0) return -200;

  return 0;
}

int receiver_init (void) {
  xwait_init(&configWaitHandle);

  int err = demux_addChannel(
    500, // leave plenty of room for ALSA mixer control config
    globals_get1iv(fec, sourceSymbolsPerBlock, 0),
    globals_get1iv(fec, repairSymbolsPerBlock, 0),
    globals_get1iv(fec, symbolLen, 0),
    onDataConfigChannel
  );
  if (err < 0) return -1;

  char privateKey[SEC_KEY_LENGTH + 1] = { 0 };
  char peerPublicKey[SEC_KEY_LENGTH + 1] = { 0 };
  globals_get1s(root, privateKey, privateKey, sizeof(privateKey));
  globals_get1s(root, peerPublicKey, peerPublicKey, sizeof(peerPublicKey));

  if (strlen(privateKey) != SEC_KEY_LENGTH || strlen(peerPublicKey) != SEC_KEY_LENGTH) {
    printf("Expected privateKey and peerPublicKey to be base64 x25519 keys.\n");
    return -2;
  }

  // NOTE: endpoint_init will block until network discovery is completed
  err = endpoint_init(demux_readPacket);
  if (err < 0) return err - 2;

  return 0;
}

int receiver_deinit (void) {
  xwait_destroy(&configWaitHandle);
  demux_deinit();
  if (receivedConfigData != NULL) free(receivedConfigData);
  return audio_deinit();
}
