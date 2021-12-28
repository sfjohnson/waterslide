#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <pthread.h>
#include "utils.h"
#include "globals.h"
#include "demux.h"
#include "raptorq/raptorq.h"

static demux_channel_t *channels;
static int chCount = 0;
static int maxChannels, maxPacketSize;

int demux_init () {
  maxChannels = globals_get1i(mux, maxChannels);
  maxPacketSize = globals_get1i(mux, maxPacketSize);
  channels = (demux_channel_t*)malloc(sizeof(demux_channel_t) * maxChannels);
  if (channels == NULL) return -1;
  memset(channels, 0, sizeof(demux_channel_t) * maxChannels);

  return 0;
}

int demux_addChannel (demux_channel_t *channel) {
  if (chCount == maxChannels) return -1;

  if (pthread_mutex_init(&channel->_lock, NULL) != 0) return -2;
  channel->_blockBuf = (uint8_t *)malloc(channel->symbolsPerBlock * channel->symbolLen);
  if (channel->_blockBuf == NULL) return -3;

  memcpy(&channels[chCount], channel, sizeof(demux_channel_t));
  return ++chCount;
}

int demux_readPacket (const uint8_t *buf, int bufLen) {
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
      if (chan->chId == chId) {
        if (chunkLen != 4 + chan->symbolLen) return -8;
        // Any network receive thread can call this function to add a chunk to any channel, so a lock is needed
        pthread_mutex_lock(&chan->_lock);
        int result = raptorq_decodePacket(&buf[pos], chunkLen, chan->_blockBuf, chan->symbolsPerBlock);
        if (result == chan->symbolsPerBlock * chan->symbolLen) {
          chan->onBlock(chan->_blockBuf, buf[pos]);
        }
        pthread_mutex_unlock(&chan->_lock);
        break;
      }
    }

    pos += chunkLen;
  }

  return pos;
}
