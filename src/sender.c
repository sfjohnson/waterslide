#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <stdatomic.h>
#include <time.h>
#include "utils.h"
#include "opus/opus_multistream.h"
#include "ck/ck_ring.h"
#include "globals.h"
#include "endpoint-secure.h"
#include "mux.h"
#include "raptorq/raptorq.h"
#include "pcm.h"
#include "xsem.h"
#include "audio.h"
#include "sender.h"

#define UNUSED __attribute__((unused))

static ck_ring_t encodeRing;
static ck_ring_buffer_t *encodeRingBuf;
static mux_transfer_t transfer;
static int targetEncodeRingSize, encodeRingMaxSize;
static int audioFrameSize;
static int maxEncodedPacketSize;
static double networkSampleRate; // Hz
static xsem_t encodeLoopInitSem;
static atomic_int encodeLoopStatus = 0;

static int initOpusEncoder (OpusMSEncoder **encoder) {
  const int networkChannelCount = globals_get1i(audio, networkChannelCount);
  unsigned char mapping[networkChannelCount];
  for (int i = 0; i < networkChannelCount; i++) mapping[i] = i;

  int err;
  *encoder = opus_multistream_encoder_create(AUDIO_OPUS_SAMPLE_RATE, networkChannelCount, networkChannelCount, 0, mapping, OPUS_APPLICATION_AUDIO, &err);
  if (err < 0) {
    printf("Error: opus_multistream_encoder_create failed: %s\n", opus_strerror(err));
    return -1;
  }

  err = opus_multistream_encoder_ctl(*encoder, OPUS_SET_BITRATE(globals_get1i(opus, bitrate)));
  if (err < 0) {
    printf("Error: opus_multistream_encoder_ctl failed: %s\n", opus_strerror(err));
    return -2;
  }

  return 0;
}

static inline void setEncodeLoopStatus (int status) {
  encodeLoopStatus = status;
  xsem_post(&encodeLoopInitSem);
}

static void *startEncodeLoop (UNUSED void *arg) {
  const int symbolLen = globals_get1i(fec, symbolLen);
  const int sourceSymbolsPerBlock = globals_get1i(fec, sourceSymbolsPerBlock);
  const int repairSymbolsPerBlock = globals_get1i(fec, repairSymbolsPerBlock);
  const int fecBlockBaseLen = symbolLen * sourceSymbolsPerBlock;
  const int networkChannelCount = globals_get1i(audio, networkChannelCount);
  const unsigned int audioEncoding = globals_get1ui(audio, encoding);

  int fecBlockPos = 0;
  uint8_t fecSBN = 0;
  uint16_t audioPacketSeq = 0;

  int fecEncodedBufLen = (4+symbolLen) * (sourceSymbolsPerBlock+repairSymbolsPerBlock);
  // fecBlockBuf maximum possible length:
  //   fecBlockBaseLen - 1 (once the length is >= fecBlockBaseLen, block(s) are sent)
  // + 2*maxEncodedPacketSize (assuming worst-case scenario of every byte being a SLIP escape sequence)
  // + 1 (for 0xc0)
  uint8_t *fecBlockBuf = (uint8_t*)malloc(fecBlockBaseLen + 2*maxEncodedPacketSize);
  uint8_t *fecEncodedBuf = (uint8_t*)malloc(fecEncodedBufLen);
  float *sampleBufFloat = (float*)malloc(4 * networkChannelCount * audioFrameSize); // For Opus
  double *sampleBufDouble = (double*)malloc(8 * networkChannelCount * audioFrameSize); // For PCM
  uint8_t *audioEncodedBuf = (uint8_t*)malloc(maxEncodedPacketSize);

  OpusMSEncoder *opusEncoder = NULL;
  pcm_codec_t pcmEncoder = { 0 };

  if (fecBlockBuf == NULL || fecEncodedBuf == NULL || sampleBufFloat == NULL || sampleBufDouble == NULL || audioEncodedBuf == NULL) {
    setEncodeLoopStatus(-1);
    return NULL;
  }

  if (audioEncoding == AUDIO_ENCODING_OPUS && initOpusEncoder(&opusEncoder) < 0) {
    setEncodeLoopStatus(-2);
    return NULL;
  }

  // Sleep for 30% of our desired interval time to make sure encodeRingSize doesn't get too full despite OS jitter.
  double targetSizeNs = 300000000.0 * targetEncodeRingSize / (double)(networkChannelCount * networkSampleRate);
  struct timespec loopSleep;
  loopSleep.tv_nsec = targetSizeNs;
  loopSleep.tv_sec = 0;

  #if defined(__linux__) || defined(__ANDROID__)
  if (utils_setCallerThreadRealtime(98, 0) < 0) {
    setEncodeLoopStatus(-3);
    return NULL;
  }
  #elif defined(__APPLE__)
  if (utils_setCallerThreadPrioHigh() < 0) {
    setEncodeLoopStatus(-3);
    return NULL;
  }
  #endif

  // successfully initialised, tell the main thread
  setEncodeLoopStatus(1);
  while (encodeLoopStatus == 1) {
    int encodeRingSize = ck_ring_size(&encodeRing);
    int encodeRingSizeFrames = encodeRingSize / networkChannelCount;
    globals_add1uiv(statsCh1Audio, streamMeterBins, STATS_STREAM_METER_BINS * encodeRingSize / encodeRingMaxSize, 1);

    if (encodeRingSizeFrames < audioFrameSize) {
      nanosleep(&loopSleep, NULL);
      continue;
    }

    if (encodeRingSize > 2 * targetEncodeRingSize) {
      // encodeRing is fuller than it should be due to this thread being preempted by the OS for too long.
      globals_add1ui(statsCh1Audio, encodeThreadJitterCount, 1);
    }

    for (int i = 0; i < networkChannelCount * audioFrameSize; i++) {
      intptr_t outSample = 0;
      ck_ring_dequeue_spsc(&encodeRing, encodeRingBuf, &outSample);
      // https://gist.github.com/shafik/848ae25ee209f698763cffee272a58f8#how-do-we-type-pun-correctly
      memcpy(&sampleBufDouble[i], &outSample, 8);
      sampleBufFloat[i] = sampleBufDouble[i];
    }

    // Write sequence number to audioEncodedBuf
    utils_writeU16LE(audioEncodedBuf, audioPacketSeq++);

    int encodedLen = 0;
    switch (audioEncoding) {
      case AUDIO_ENCODING_OPUS:
        encodedLen = opus_multistream_encode_float(opusEncoder, sampleBufFloat, audioFrameSize, &audioEncodedBuf[2], maxEncodedPacketSize - 2);
        if (encodedLen < 0) {
          globals_add1ui(statsCh1AudioOpus, codecErrorCount, 1);
          continue;
        }
        break;

      case AUDIO_ENCODING_PCM:
        encodedLen = pcm_encode(&pcmEncoder, sampleBufDouble, networkChannelCount * audioFrameSize, &audioEncodedBuf[2]);
        break;
    }

    fecBlockPos += utils_slipEncode(audioEncodedBuf, encodedLen + 2, &fecBlockBuf[fecBlockPos]);
    fecBlockBuf[fecBlockPos++] = 0xc0;

    // Add more terminating bytes if necessary to pad the block to keep the data rate somewhat constant.
    // DEBUG: fix up the receiver threading code so this isn't necessary.
    if (audioEncoding == AUDIO_ENCODING_OPUS) {
      for (int i = 0; i < maxEncodedPacketSize/3 - (encodedLen+2); i++) {
        fecBlockBuf[fecBlockPos++] = 0xc0;
      }
    }

    while (fecBlockPos >= fecBlockBaseLen) {
      // DEBUG: raptorq_encodeBlock needs optimisation, it can be slow enough on Android (>9 ms) to cause encodeThreadJitterCount to increase
      int fecEncodedLen = raptorq_encodeBlock(
        fecSBN++,
        fecBlockBuf,
        fecBlockBaseLen,
        sourceSymbolsPerBlock,
        fecEncodedBuf,
        sourceSymbolsPerBlock + repairSymbolsPerBlock
      );
      if (fecEncodedLen != fecEncodedBufLen) {
        // DEBUG: mark encode error here
        fecBlockPos = 0;
        continue;
      }
      memmove(fecBlockBuf, &fecBlockBuf[fecBlockBaseLen], fecBlockPos - fecBlockBaseLen);
      fecBlockPos -= fecBlockBaseLen;

      mux_resetTransfer(&transfer);
      mux_setChannel(&transfer, 1, sourceSymbolsPerBlock + repairSymbolsPerBlock, 4 + symbolLen, fecEncodedBuf);
      mux_emitPackets(&transfer, endpointsec_send);
    }
  }

  return NULL;
}

int sender_init (void) {
  const int networkChannelCount = globals_get1i(audio, networkChannelCount);
  const unsigned int audioEncoding = globals_get1ui(audio, encoding);

  networkSampleRate = globals_get1i(audio, networkSampleRate);

  switch (audioEncoding) {
    case AUDIO_ENCODING_OPUS:
      audioFrameSize = globals_get1i(opus, frameSize);
      maxEncodedPacketSize = globals_get1i(opus, maxPacketSize);
      break;
    case AUDIO_ENCODING_PCM:
      audioFrameSize = globals_get1i(pcm, frameSize);
      // 24-bit samples + 2 bytes for CRC + 2 bytes for sequence number
      maxEncodedPacketSize = 3 * networkChannelCount * audioFrameSize + 4;
      break;
    default:
      printf("Error: Audio encoding %d not implemented.\n", audioEncoding);
      return -1;
  }

  if (mux_init() < 0) return -2;
  if (mux_initTransfer(&transfer) < 0) return -3;

  char privateKey[SEC_KEY_LENGTH + 1] = { 0 };
  char peerPublicKey[SEC_KEY_LENGTH + 1] = { 0 };
  globals_get1s(root, privateKey, privateKey, sizeof(privateKey));
  globals_get1s(root, peerPublicKey, peerPublicKey, sizeof(peerPublicKey));

  if (strlen(privateKey) != SEC_KEY_LENGTH || strlen(peerPublicKey) != SEC_KEY_LENGTH) {
    printf("Expected privateKey and peerPublicKey to be base64 x25519 keys.\n");
    return -4;
  }

  int err = endpointsec_init(NULL);
  if (err < 0) return err - 4;

  err = audio_init(false);
  if (err < 0) return err - 21;

  double deviceLatency = audio_getDeviceLatency();

  // We want there to be about targetEncodeRingSize values in the encodeRing each time the encodeThread
  // wakes from sleep. If the device latency is shorter than the encoding latency, the encodeThread can wait
  // longer for there to be a full frame in the encodeRing.
  // NOTE: Each frame in encodeRing is 1/networkSampleRate seconds, not 1/deviceSampleRate seconds.
  if (deviceLatency * networkSampleRate > audioFrameSize) {
    targetEncodeRingSize = deviceLatency * networkSampleRate;
  } else {
    targetEncodeRingSize = audioFrameSize;
  }
  targetEncodeRingSize *= networkChannelCount;

  // encodeRingMaxSize is the maximum number of float values that can be stored in encodeRing, with
  // networkChannelCount values per frame.
  // In theory the encode thread should loop often enough that the encodeRing never gets much larger than
  // targetEncodeRingSize, but we multiply by 4 to allow plenty of room in encodeRing
  // for timing jitter caused by the operating system's scheduler. Also ck requires a power of two size.
  encodeRingMaxSize = utils_roundUpPowerOfTwo(4 * targetEncodeRingSize);
  globals_set1i(statsCh1Audio, streamBufferSize, encodeRingMaxSize / networkChannelCount);

  encodeRingBuf = (ck_ring_buffer_t*)malloc(sizeof(ck_ring_buffer_t) * encodeRingMaxSize);
  if (encodeRingBuf == NULL) return -30;
  memset(encodeRingBuf, 0, sizeof(ck_ring_buffer_t) * encodeRingMaxSize);
  ck_ring_init(&encodeRing, encodeRingMaxSize);

  err = audio_start(&encodeRing, encodeRingBuf, encodeRingMaxSize);
  if (err < 0) return err - 30;

  pthread_t encodeLoopThread;
  if (xsem_init(&encodeLoopInitSem, 0) < 0) return -34;
  if (pthread_create(&encodeLoopThread, NULL, startEncodeLoop, NULL) != 0) return -35;

  // Wait for encodeLoop to initialise
  xsem_wait(&encodeLoopInitSem);
  if (encodeLoopStatus < 0) {
    if (pthread_join(encodeLoopThread, NULL) != 0) return -36;
    if (xsem_destroy(&encodeLoopInitSem) != 0) return -37;
    return encodeLoopStatus - 37;
  }

  return 0;
}

int sender_deinit (void) {
  return 0;
}
