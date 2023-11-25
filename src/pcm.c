// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <string.h>
#include "utils.h"
#include "pcm.h"

int pcm_encode (pcm_codec_t *codec, const double *inSampleBuf, int sampleCount, uint8_t *outData) {
  for (int i = 0; i < sampleCount; i++) {
    double sampleDouble = inSampleBuf[i];
    if (sampleDouble < -1.0) sampleDouble = -1.0;
    else if (sampleDouble > 1.0) sampleDouble = 1.0;
    // Convert double samples to 24-bit signed int
    // http://blog.bjornroche.com/2009/12/int-float-int-its-jungle-out-there.html
    // TODO: I don't think dithering is necessary here but I'm not 100% sure. I need to measure the waveform to check.
    int32_t sampleInt = sampleDouble > 0.0 ? 8388607.0 * sampleDouble : 8388608.0 * sampleDouble;
    memcpy(&outData[3*i], &sampleInt, 3);
  }

  codec->crc = utils_crc16(codec->crc, outData, 3 * sampleCount);
  utils_writeU16LE(&outData[3*sampleCount], codec->crc);

  return 3 * sampleCount + 2;
}

int pcm_decode (pcm_codec_t *codec, const uint8_t *inData, int inDataLen, const uint8_t **samples) {
  inDataLen -= 2; // exclude 2 byte CRC at the end
  if (inDataLen < 3) return -1; // at least 1 24-bit sample
  if (inDataLen % 3 != 0) return -2;

  uint16_t calculatedCRC = utils_crc16(codec->crc, inData, inDataLen);
  uint16_t receivedCRC = utils_readU16LE(&inData[inDataLen]);
  codec->crc = receivedCRC;
  if (calculatedCRC != receivedCRC) return -3;

  *samples = inData;
  return inDataLen / 3;
}
