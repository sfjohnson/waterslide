#ifndef _PCM_H
#define _PCM_H

#include <stdint.h>

typedef struct {
  uint16_t crc;
} pcm_codec_t;

// outData must be at least 3 * sampleCount + 2 bytes
// sampleCount = channelCount * frameCount
int pcm_encode (pcm_codec_t *codec, const float *inSampleBuf, int sampleCount, uint8_t *outData);

// outSampleBuf must be at least (inDataLen-2)/3 floats in length
int pcm_decode (pcm_codec_t *codec, const uint8_t *inData, int inDataLen, float *outSampleBuf);

#endif
