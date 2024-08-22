// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "xwait.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <pthread.h>
#include "raptorq/raptorq.h"
#include "ck/ck_ring.h"
#include "utils.h"
#include "globals.h"
#include "demux.h"

typedef struct {
  uint8_t chId;
  void (*onData)(const uint8_t *, int);
  ck_ring_t chunkRing;
  ck_ring_buffer_t *chunkRingBuf; // encoded block made of chunks
  uint8_t *blockBuf; // decoded block used in decode thread
  uint8_t *dataBuf; // final buf that is passed to callback in decode thread
  uint8_t *chunkBuf; // used to pass the chunks between the ring and the rust code
  int maxDataLen, dataBufPos, sbnLast;
  size_t chunkLen, chunkLenWords, chunkRingLenWords;
  int blockBufLen;
  pthread_t decodeThread;
  xwait_t waitHandle;
  void *raptorqHandle;
} demux_channel_t;

static demux_channel_t channels[MUX_CHANNEL_COUNT];
static atomic_uint_fast8_t chCount = 0;
static atomic_bool threadsRunning = true;

void demux_deinit (void) {
  atomic_store(&threadsRunning, false);
  uint8_t chCountLocal = atomic_load(&chCount);
  for (int i = 0; i < chCountLocal; i++) {
    xwait_notify(&channels[i].waitHandle);
    pthread_join(channels[i].decodeThread, NULL);
    xwait_destroy(&channels[i].waitHandle);
    raptorq_deinitDecoder(channels[i].raptorqHandle);
    free(channels[i].chunkRingBuf);
    free(channels[i].blockBuf);
    free(channels[i].dataBuf);
    free(channels[i].chunkBuf);
  }

  atomic_store(&chCount, 0);
}

static int decodeBlock (int sbn, demux_channel_t *chan) {
  // first update stats
  int us = utils_getCurrentUTime();

  unsigned int chRingPos = globals_get1uiv(statsDemux, blockTimingRingPos, chan->chId);
  unsigned int ringIndex = chan->chId * STATS_BLOCK_TIMING_RING_LEN + chRingPos;
  globals_set1uiv(statsDemux, blockTimingRing, ringIndex, (unsigned int)us);
  if (++chRingPos == STATS_BLOCK_TIMING_RING_LEN) chRingPos = 0;
  // NOTE: blockTimingRingPos must only be written to here
  globals_set1uiv(statsDemux, blockTimingRingPos, chan->chId, chRingPos);

  if (chan->sbnLast != -1) {
    int sbnDiff = sbn - chan->sbnLast;
    // Overflow
    if (sbnDiff < -128) {
      sbnDiff += 256;
    } else if (sbnDiff > 128) {
      sbnDiff -= 256;
    }

    if (sbnDiff == 0) {
      // duplicate block, don't decode, don't reset state
      globals_add1uiv(statsDemux, dupBlockCount, chan->chId, 1);
      return -1;
    } else if (sbnDiff < 0) {
      // out-of-order old block, don't decode, don't reset state
      globals_add1uiv(statsDemux, oooBlockCount, chan->chId, 1);
      return -2;
    } else if (sbnDiff > 1) {
      // out-of-order, sbnDiff - 1 previous block(s) were dropped, decode and reset state 
      globals_add1uiv(statsDemux, oooBlockCount, chan->chId, sbnDiff - 1);
      chan->dataBufPos = 0;
    }
  }
  chan->sbnLast = sbn;

  // then read data from the block
  // audio/video decoding happens in the chan->onData callback

  int32_t dataLen = 0; // int32 so there is no sign difference for comparsions
  memcpy(&dataLen, chan->blockBuf, 4);
  if (dataLen < 0) return -3;

  int blockPos = 4 + dataLen;
  if (blockPos > chan->blockBufLen) return -4;
  if (chan->dataBufPos + dataLen > chan->maxDataLen) return -5;

  memcpy(&chan->dataBuf[chan->dataBufPos], &chan->blockBuf[4], dataLen);
  if (chan->dataBufPos > 0) chan->onData(chan->dataBuf, chan->dataBufPos + dataLen);
  chan->dataBufPos = 0;

  if (chan->blockBufLen - blockPos < 5) {
    // done! nothing left to decode as we need at least a length field and 1 byte of data
    // there will be 0 to 4 bytes of ignored padding in chan->blockBuf
    return 0;
  }

  while (true) {
    memcpy(&dataLen, &chan->blockBuf[blockPos], 4);
    if (dataLen <= 0) return -6;
    blockPos += 4;

    int leftoverLen = chan->blockBufLen - blockPos;
    if (leftoverLen < dataLen) {
      // partial data
      memcpy(&chan->dataBuf[chan->dataBufPos], &chan->blockBuf[blockPos], leftoverLen);
      chan->dataBufPos += leftoverLen;
      return 1;
    }

    // full data
    memcpy(&chan->dataBuf[chan->dataBufPos], &chan->blockBuf[blockPos], dataLen);
    blockPos += dataLen;
    chan->onData(chan->dataBuf, chan->dataBufPos + dataLen);
    chan->dataBufPos = 0;

    if (chan->blockBufLen - blockPos < 5) return 2; // done, no partial, ignore padding
  }
}

// this is a realtime thread where all FEC and audio/video decoding happens
static void *startDecodeThread (void *arg) {
  intptr_t chId = (intptr_t)arg;
  demux_channel_t *chan = &channels[chId];

  // pin each channel decode thread to a different core, leaving core 0 for other stuff (Linux only)
  // DEBUG: check the CPU core count before calling this
  utils_setCallerThreadRealtime(98, chId + 1);

  while (atomic_load(&threadsRunning)) {
    xwait_wait(&chan->waitHandle);
    if (ck_ring_size(&chan->chunkRing) < chan->chunkLenWords) continue;

    // feed one chunk from any endpoint to raptorq_decodePacket
    for (size_t i = 0; i < chan->chunkLenWords; i++) {
      intptr_t chunkWord = 0;
      ck_ring_dequeue_spsc(&chan->chunkRing, chan->chunkRingBuf, (void*)&chunkWord);
      memcpy(&chan->chunkBuf[4*i], &chunkWord, 4);
    }

    int result = raptorq_decodePacket(chan->raptorqHandle, chan->chunkBuf, chan->blockBuf);
    if (result == chan->blockBufLen) decodeBlock(chan->chunkBuf[0], chan);
  }

  return NULL;
}

int demux_addChannel (int maxDataLen, int sourceSymbolsPerBlock, int repairSymbolsPerBlock, int symbolLen, void (*onData)(const uint8_t *, int)) {
  uint8_t chCountLocal = atomic_load(&chCount);
  if (chCountLocal == MUX_CHANNEL_COUNT) return -1;

  demux_channel_t *chan = &channels[chCountLocal];

  int endpointCount = globals_get1i(endpoints, endpointCount);

  chan->chunkLen = 4 + symbolLen;
  chan->chunkLenWords = chan->chunkLen / 4;
  // ring space for up to 2 encoded blocks (with Payload IDs and repair symbols)
  // with space for duplicate chunks from each endpoint
  // if the ring gets full it means there is not enough CPU for the decode thread
  // we don't make the ring larger as it would add latency; if the ring is
  // overflowing due to bunching due to poor network, the block size should be increased
  // assume 4 bytes per ring word to simplify this code (uses twice the memory on 64-bit arch)
  // TODO: can we reduce the ring size to 1 encoded block?
  chan->chunkRingLenWords = 2 * endpointCount * chan->chunkLen * (sourceSymbolsPerBlock+repairSymbolsPerBlock) / 4;

  // ck requires the size to be a power of two but we will pretend ring is chunkRingLenWords
  // in size, and ignore the rest
  int ringAllocSize = utils_roundUpPowerOfTwo(chan->chunkRingLenWords);
  chan->chunkRingBuf = (ck_ring_buffer_t*)malloc(sizeof(ck_ring_buffer_t) * ringAllocSize);
  if (chan->chunkRingBuf == NULL) return -2;
  memset(chan->chunkRingBuf, 0, sizeof(ck_ring_buffer_t) * ringAllocSize);
  ck_ring_init(&chan->chunkRing, ringAllocSize);

  chan->blockBufLen = symbolLen * sourceSymbolsPerBlock;
  chan->blockBuf = (uint8_t *)malloc(chan->blockBufLen);
  if (chan->blockBuf == NULL) return -3;

  chan->dataBufPos = 0;
  chan->maxDataLen = maxDataLen;
  chan->dataBuf = (uint8_t *)malloc(chan->maxDataLen);
  if (chan->dataBuf == NULL) return -4;

  chan->chunkBuf = (uint8_t *)malloc(chan->chunkLen);
  if (chan->chunkBuf == NULL) return -5;

  chan->sbnLast = -1;
  chan->chId = chCountLocal;
  chan->onData = onData;
  chan->raptorqHandle = raptorq_initDecoder(chan->chunkLen, sourceSymbolsPerBlock);
  xwait_init(&chan->waitHandle);

  intptr_t arg = chCountLocal;
  if (pthread_create(&chan->decodeThread, NULL, startDecodeThread, (void*)arg) != 0) return -6;

  return (int)atomic_fetch_add(&chCount, 1);
}

// this is called by a single realtime priority network thread
int demux_readPacket (const uint8_t *buf, size_t bufLen, int endpointIndex) {
  uint8_t chCountLocal = atomic_load(&chCount);
  size_t pos = 1; // skip first byte (reserved for flags)

  while (true) {
    if (pos == bufLen) return pos;
    if (pos + 69 > bufLen) return -1; // 1 (chId) + 68 (min chunkLen)

    uint8_t chId = buf[pos++];
    if (chId >= chCountLocal) return -2;

    demux_channel_t *chan = &channels[chId];

    if (bufLen < pos + chan->chunkLen) return -3;
    // check there is space for at least one chunk on the ring
    if (ck_ring_size(&chan->chunkRing) > chan->chunkRingLenWords - chan->chunkLenWords) {
      globals_add1uiv(statsDemux, ringOverrunCount, chId, 1);
      return -4;
    }

    int sbn = buf[pos];
    globals_set1iv(statsEndpoints, lastSbn, chan->chId * MAX_ENDPOINTS + endpointIndex, sbn);

    for (size_t i = 0; i < chan->chunkLenWords; i++) {
      intptr_t chunkWord = 0;
      memcpy(&chunkWord, &buf[pos + 4*i], 4);
      ck_ring_enqueue_spsc(&chan->chunkRing, chan->chunkRingBuf, (void*)chunkWord);
    }

    // tell decode thread another chunk is ready
    xwait_notify(&chan->waitHandle);

    pos += chan->chunkLen;
  }
}
