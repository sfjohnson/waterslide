// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "xwait.h"
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include "raptorq/raptorq.h"
#include "utils.h"
#include "globals.h"
#include "mux.h"

typedef struct {
  uint8_t chId, sbn;
  ck_ring_t chunkRing;
  ck_ring_buffer_t *chunkRingBuf; // encoded block made of chunks
  uint8_t *blockBuf, *encodedBlockBuf;
  int blockBufPos, blockBufLen, maxDataLen;
  size_t chunkLen, chunkLenWords, chunkRingLenWords, chunksPerBlock, encodedBlockBufLen;
  void *raptorqHandle;
} mux_channel_t;

static mux_channel_t channels[MUX_CHANNEL_COUNT];
static int chCount = 0;
static int anchorChId = -1;
static size_t maxPacketSize;
static uint8_t *packetBuf;
static int (*_onPacket)(const uint8_t *, size_t);
static pthread_t packetThread;
static atomic_bool packetThreadRunning;
static xwait_t waitHandle;

static int sendPackets (void) {
  size_t packetBufPos;

  // read one chunk from each of the available chunkRings, assemble and send a packet
  // repeat this until the anchor channel chunkRing is empty

  while (true) {
    packetBuf[0] = 0; // flags
    packetBufPos = 1;

    for (uint8_t chId = 0; chId < chCount; chId++) {
      mux_channel_t *chan = &channels[chId];
      size_t ringSizeWords = ck_ring_size(&chan->chunkRing);
      if (ringSizeWords < chan->chunkLenWords) {
        if (chId == anchorChId) {
          return 0;
        } else {
          continue;
        }
      }

      if (packetBufPos + 1 + 4*chan->chunkLenWords > maxPacketSize) return -1;

      packetBuf[packetBufPos] = chId;
      packetBufPos++;

      for (size_t i = 0; i < chan->chunkLenWords; i++) {
        intptr_t chunkWord = 0;
        ck_ring_dequeue_spsc(&chan->chunkRing, chan->chunkRingBuf, (void*)&chunkWord);
        memcpy(&packetBuf[packetBufPos], &chunkWord, 4);
        packetBufPos += 4;
      }
    }

    if (packetBufPos > 1) _onPacket(packetBuf, packetBufPos);
  }

  return 0;
}

static void *startPacketThread (UNUSED void *arg) {
  utils_setCallerThreadRealtime(98, 0);

  while (atomic_load(&packetThreadRunning)) {
    xwait_wait(&waitHandle);

    sendPackets(); // TODO: read sendPackets result and flag error
  }

  return NULL;
}

int mux_init (int (*onPacket)(const uint8_t *, size_t)) {
  _onPacket = onPacket;

  maxPacketSize = globals_get1ui(mux, maxPacketSize);
  packetBuf = (uint8_t*)malloc(maxPacketSize);
  if (packetBuf == NULL) return -1;

  xwait_init(&waitHandle);
  atomic_store(&packetThreadRunning, true);
  if (pthread_create(&packetThread, NULL, startPacketThread,  NULL) != 0) return -2;

  return 0;
}

void mux_deinit (void) {
  atomic_store(&packetThreadRunning, false);
  xwait_notify(&waitHandle);
  pthread_join(packetThread, NULL);
  xwait_destroy(&waitHandle);
  free(packetBuf);

  for (int i = 0; i < chCount; i++) {
    // raptorq_deinitDecoder(channels[i].raptorqHandle); // DEBUG: this causes a segfault
    free(channels[i].blockBuf);
    free(channels[i].encodedBlockBuf);
    free(channels[i].chunkRingBuf);
  }

  chCount = 0;
}

int mux_setAnchorChannel (uint8_t chId) {
  if (chId >= chCount) return -1;

  anchorChId = chId;
  return 0;
}

int mux_addChannel (int maxDataLen, int sourceSymbolsPerBlock, int repairSymbolsPerBlock, int symbolLen) {
  if (chCount == MUX_CHANNEL_COUNT) return -1;
  if (maxDataLen > symbolLen * sourceSymbolsPerBlock - 8) return -2;

  mux_channel_t *chan = &channels[chCount];

  chan->chunkLen = 4 + symbolLen;
  chan->chunkLenWords = chan->chunkLen / 4;
  chan->chunksPerBlock = sourceSymbolsPerBlock + repairSymbolsPerBlock;
  chan->encodedBlockBufLen = chan->chunkLen * chan->chunksPerBlock;
  // ring space for up to 2 encoded blocks (with Payload IDs and repair symbols)
  // block size should be large enough so there are not big bursts of raptorq_encodeBlock calls
  // that will overflow the ring
  // if the ring is not already empty by the time the next block is added, consider increasing
  // block size so that this ring does not contribute significantly to latency
  // assume 4 bytes per ring word to simplify this code (uses twice the memory on 64-bit arch)
  // TODO: can we reduce the ring size to 1 encoded block?
  chan->chunkRingLenWords = 2 * chan->encodedBlockBufLen / 4;

  // ck requires the size to be a power of two but we will pretend ring is chunkRingLenWords
  // in size, and ignore the rest
  int ringAllocSize = utils_roundUpPowerOfTwo(chan->chunkRingLenWords);
  chan->chunkRingBuf = (ck_ring_buffer_t*)malloc(sizeof(ck_ring_buffer_t) * ringAllocSize);
  if (chan->chunkRingBuf == NULL) return -3;
  memset(chan->chunkRingBuf, 0, sizeof(ck_ring_buffer_t) * ringAllocSize);
  ck_ring_init(&chan->chunkRing, ringAllocSize);

  chan->chId = chCount;
  chan->sbn = 0;
  chan->maxDataLen = maxDataLen;
  chan->blockBufPos = 0;
  chan->blockBufLen = symbolLen * sourceSymbolsPerBlock;
  chan->blockBuf = (uint8_t *)malloc(chan->blockBufLen);
  if (chan->blockBuf == NULL) return -4;
  chan->encodedBlockBuf = (uint8_t *)malloc(chan->encodedBlockBufLen);
  if (chan->encodedBlockBuf == NULL) return -5;

  chan->raptorqHandle = raptorq_initEncoder(chan->blockBufLen, sourceSymbolsPerBlock);

  return chCount++;
}

static int encodeAndEnqueueBlock (uint8_t chId) {
  mux_channel_t *chan = &channels[chId];
  chan->blockBufPos = 0;

  size_t result = raptorq_encodeBlock(
    chan->raptorqHandle,
    chan->sbn++,
    chan->blockBuf,
    chan->encodedBlockBuf,
    chan->chunksPerBlock
  );

  if (result != chan->encodedBlockBufLen) return -1;

  if (ck_ring_size(&chan->chunkRing) > chan->chunkRingLenWords/2) {
    globals_add1uiv(statsMux, ringOverrunCount, chId, 1);
    return -2;
  }

  // chan->chunkRingLenWords/2 == chan->encodedBlockBufLen in words
  for (size_t i = 0; i < chan->chunkRingLenWords/2; i++) {
    intptr_t chunkWord = 0;
    memcpy(&chunkWord, &chan->encodedBlockBuf[4*i], 4);
    ck_ring_enqueue_spsc(&chan->chunkRing, chan->chunkRingBuf, (void*)chunkWord);
  }

  if (chId == anchorChId) xwait_notify(&waitHandle);

  return 0;
}

// FEC encoding is done here
int mux_writeData (uint8_t chId, const uint8_t *dataBuf, int dataBufLen) {
  mux_channel_t *chan = &channels[chId];

  if (anchorChId < 0) return -1;
  if (dataBufLen > chan->maxDataLen) return -2;

  int32_t dataLenField;
  int leftoverLen = chan->blockBufLen - chan->blockBufPos;
  int err;

  if (leftoverLen < 5) {
    // all of dataBuf goes at the start of the next block
    memset(&chan->blockBuf[chan->blockBufPos], 0, leftoverLen); // padding
    err = encodeAndEnqueueBlock(chId); // sets blockBufPos to 0
    if (err < 0) return err - 2;
    memset(chan->blockBuf, 0, 4); // no partial data
    dataLenField = dataBufLen;
    memcpy(&chan->blockBuf[4], &dataLenField, 4);
    memcpy(&chan->blockBuf[8], dataBuf, dataBufLen);
    chan->blockBufPos = 8 + dataBufLen;
    return 0;
  }

  dataLenField = dataBufLen;
  memcpy(&chan->blockBuf[chan->blockBufPos], &dataLenField, 4);
  chan->blockBufPos += 4;

  if (leftoverLen < 4 + dataBufLen) {
    // dataBuf is split between current and next block
    memcpy(&chan->blockBuf[chan->blockBufPos], dataBuf, leftoverLen - 4);
    err = encodeAndEnqueueBlock(chId); // sets blockBufPos to 0
    if (err < 0) return err - 4;
    dataLenField = 4 + dataBufLen - leftoverLen;
    memcpy(chan->blockBuf, &dataLenField, 4);
    memcpy(&chan->blockBuf[4], &dataBuf[leftoverLen-4], dataLenField);
    chan->blockBufPos = 4 + dataLenField;
    return 1;
  }

  // all of dataBuf goes on the current block
  memcpy(&chan->blockBuf[chan->blockBufPos], dataBuf, dataBufLen);
  chan->blockBufPos += dataBufLen;
  return 2;
}
