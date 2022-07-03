#include <string.h>
#include "utils.h"
#include "pcm.h"

int pcm_encode (pcm_codec_t *codec, const float *inSampleBuf, int sampleCount, uint8_t *outData) {
  for (int i = 0; i < sampleCount; i++) {
    // Convert float samples to 24-bit signed int
    // http://blog.bjornroche.com/2009/12/int-float-int-its-jungle-out-there.html
    // DEBUG: do we need dithering?
    int32_t sampleInt = inSampleBuf[i] > 0.0f ? 8388607.0f * inSampleBuf[i] : 8388608.0f * inSampleBuf[i];

    // The syncer detects clipping for stats but it does not actually clamp the samples, we need to do that.
    if (sampleInt > 8388607) {
      sampleInt = 8388607;
    } else if (sampleInt < -8388608) {
      sampleInt = -8388608;
    }

    memcpy(&outData[3*i], &sampleInt, 3);
  }

  codec->crc = utils_crc16(codec->crc, outData, 3 * sampleCount);
  utils_writeU16LE(&outData[3*sampleCount], codec->crc);

  return 3 * sampleCount + 2;
}

int pcm_decode (pcm_codec_t *codec, const uint8_t *inData, int inDataLen, float *outSampleBuf) {
  inDataLen -= 2; // exclude 2 byte CRC at the end
  if (inDataLen < 3) return -1; // at least 1 24-bit sample
  if (inDataLen % 3 != 0) return -2;

  uint16_t calculatedCRC = utils_crc16(codec->crc, inData, inDataLen);
  uint16_t receivedCRC = utils_readU16LE(&inData[inDataLen]);
  codec->crc = receivedCRC;
  if (calculatedCRC != receivedCRC) return -3;

  int sampleCount = inDataLen / 3;
  for (int i = 0; i < sampleCount; i++) {
    int32_t sampleInt = 0;
    // Leave the least significant byte of sampleInt empty and then shift back into it to sign extend.
    memcpy((uint8_t *)&sampleInt + 1, &inData[3*i], 3);
    sampleInt >>= 8;

    // http://blog.bjornroche.com/2009/12/int-float-int-its-jungle-out-there.html
    // DEBUG: do we need dithering?
    outSampleBuf[i] = sampleInt > 0 ? sampleInt/8388607.0f : sampleInt/8388608.0f;
  }

  return sampleCount;
}
