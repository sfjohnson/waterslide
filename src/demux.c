// Copyright 2023 Sam Johnson
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <pthread.h>
#include "utils.h"
#include "globals.h"
#include "demux.h"
#include "raptorq/raptorq.h"

// DEBUG: test
// #include <stdio.h>
// #include <unistd.h>

static demux_channel_t *channels;
static int chCount = 0;
static int maxChannels, maxPacketSize;

int demux_init (void) {
  maxChannels = globals_get1i(mux, maxChannels);
  maxPacketSize = globals_get1i(mux, maxPacketSize);
  channels = (demux_channel_t*)malloc(sizeof(demux_channel_t) * maxChannels);
  if (channels == NULL) return -1;
  memset(channels, 0, sizeof(demux_channel_t) * maxChannels);
  return 0;
}

int demux_addChannel (demux_channel_t *channel) {
  if (chCount == maxChannels) return -1;

  channel->_blockBuf = (uint8_t *)malloc(channel->symbolsPerBlock * channel->symbolLen);
  if (channel->_blockBuf == NULL) return -2;

  memcpy(&channels[chCount], channel, sizeof(demux_channel_t));
  return ++chCount;
}

static void decodeChunk (const uint8_t *chunkBuf, int sbn, demux_channel_t *chan) {
  // DEBUG: throttling experiment

  // statics must only be accessed inside channel lock
  // static struct timespec tsp;
  // static int usTimeLast = -1;
  // static int dropBlockCount = 0;

  int result = raptorq_decodePacket(chunkBuf, 4 + chan->symbolLen, chan->_blockBuf, chan->symbolsPerBlock);
  if (result == chan->symbolsPerBlock * chan->symbolLen) {
    // clock_gettime(CLOCK_MONOTONIC_RAW, &tsp);
    // // us will be between 0 and 999_999_999 and will roll back to zero every 1000 seconds
    // int usTimeBefore = 1000000 * (tsp.tv_sec % 1000) + (tsp.tv_nsec / 1000);

    // if (dropBlockCount > 0) {
    //   dropBlockCount--;
    // } else {
      chan->onBlock(chan->_blockBuf, sbn);
    // }

    // clock_gettime(CLOCK_MONOTONIC_RAW, &tsp);
    // // us will be between 0 and 999_999_999 and will roll back to zero every 1000 seconds
    // int usTimeAfter = 1000000 * (tsp.tv_sec % 1000) + (tsp.tv_nsec / 1000);

    // if (usTimeLast != -1) {
    //   int intervalTime = usTimeAfter - usTimeLast;
    //   if (intervalTime < 0) intervalTime += 1000000000; // overflow
    //   int workTime = usTimeAfter - usTimeBefore;
    //   if (workTime < 0) workTime += 1000000000; // overflow

    //   // DEBUG: log
    //   globals_set1ff(statsCh1Audio, receiverSyncFilt, workTime / (double)intervalTime);

    //   if (intervalTime != 0 && workTime / (double)intervalTime > 0.7) {
    //     // dropBlockCount += 10;
    //     printf("throttling!\n"); // DEBUG: printf is not ok here
    //     sleep(2);
    //   }
    // }

    // usTimeLast = usTimeAfter;
  }
}

// NOTE: this is called by one realtime priority network thread
int demux_readPacket (const uint8_t *buf, int bufLen, int endpointIndex) {
  if (bufLen > maxPacketSize) return -1;
  if (bufLen < 3) return -2;
  int len;
  int pos = 1; // Skip flags (reserved)

  while (pos < bufLen) {
    uint64_t chId;
    uint16_t chunkLen;

    len = utils_decodeVarintU64(&buf[pos], bufLen - pos, &chId);
    if (len < 0) return len - 2;
    pos += len;
    len = utils_decodeVarintU16(&buf[pos], bufLen - pos, &chunkLen);
    if (len < 0) return len - 4;
    pos += len;

    if (pos + chunkLen > bufLen) return -7;

    for (int i = 0; i < chCount; i++) {
      demux_channel_t *chan = &channels[i];
      int sbn = buf[pos];
      if (chId == 1) {
        globals_set1iv(statsCh1Endpoints, lastSbn, endpointIndex, sbn);
      }
      if (chan->chId == chId) {
        if (chunkLen != 4 + chan->symbolLen) return -8;
        decodeChunk(&buf[pos], sbn, chan);
        break;
      }
    }

    pos += chunkLen;
  }

  return pos;
}
