// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#ifndef _PCM_H
#define _PCM_H

#include <stdint.h>

typedef struct {
  uint16_t crc;
} pcm_codec_t;

// outData must be at least 3 * sampleCount + 2 bytes
// sampleCount = channelCount * frameCount
int pcm_encode (pcm_codec_t *codec, const double *inSampleBuf, int sampleCount, uint8_t *outData);

// samples is set to a 24-bit LE packed buffer containing (inDataLen-2)/3 elements
// inData and samples reference the same memory, there is no extra malloc
int pcm_decode (pcm_codec_t *codec, const uint8_t *inData, int inDataLen, const uint8_t **samples);

#endif
