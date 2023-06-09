#include <string.h>
#include <stdlib.h>
#include "utils.h"
#include "globals.h"
#include "mux.h"

static uint8_t *packetBuf;
static int maxChannels, maxPacketSize;

int mux_init (void) {
  maxChannels = globals_get1i(mux, maxChannels);
  maxPacketSize = globals_get1i(mux, maxPacketSize);
  packetBuf = (uint8_t*)malloc(maxPacketSize);
  if (packetBuf == NULL) return -1;

  return 0;
}

int mux_initTransfer (mux_transfer_t *transfer) {
  transfer->flags = 0x00;
  transfer->seq = 0;
  transfer->chCount = 0;
  transfer->channels = (mux_channel_t*)malloc(sizeof(mux_channel_t) * maxChannels);
  if (transfer->channels == NULL) return -1;
  memset(transfer->channels, 0, sizeof(mux_channel_t) * maxChannels);

  return 0;
}

void mux_deinitTransfer (mux_transfer_t *transfer) {
  free(transfer->channels);
  transfer->channels = NULL;
}

void mux_resetTransfer (mux_transfer_t *transfer) {
  transfer->flags = 0x00;
  transfer->chCount = 0;
}

int mux_setChannel (mux_transfer_t *transfer, uint64_t chId, int chunksPerBlock, int chunkLen, uint8_t *blockBuf) {
  if (transfer->chCount == maxChannels) return -1;
  if (chunksPerBlock <= 0) return -2; // Prevents an infinite loop in mux_emitPackets

  mux_channel_t *ch = &transfer->channels[transfer->chCount];
  ch->chId = chId;
  ch->chunksPerBlock = chunksPerBlock;
  ch->chunkLen = chunkLen;
  ch->blockBuf = blockBuf;
  ch->_currentChunk = 0;
  return ++transfer->chCount;
}

// NOTE: this function is not thread safe because of packetBuf
int mux_emitPackets (mux_transfer_t *transfer, int (*onPacket)(const uint8_t *, int)) {
  int channelsRemaining = transfer->chCount;
  while (channelsRemaining > 0) {
    int pos = 0;
    int len = 0;
    packetBuf[pos++] = transfer->flags;
    pos += utils_writeU16LE(&packetBuf[pos], transfer->seq++);

    for (int i = 0; i < transfer->chCount; i++) {
      mux_channel_t *ch = &transfer->channels[i];
      if (ch->_currentChunk == ch->chunksPerBlock) continue;

      len = utils_encodeVarintU64(&packetBuf[pos], maxPacketSize - pos, ch->chId);
      if (len < 0) return len;
      pos += len;
      len = utils_encodeVarintU16(&packetBuf[pos], maxPacketSize - pos, ch->chunkLen);
      if (len < 0) return len - 2;
      pos += len;

      if (pos + ch->chunkLen > maxPacketSize) return -5;
      memcpy(&packetBuf[pos], &ch->blockBuf[ch->_currentChunk*ch->chunkLen], ch->chunkLen);
      pos += ch->chunkLen;
      if (++ch->_currentChunk == ch->chunksPerBlock) channelsRemaining--;
    }

    onPacket(packetBuf, pos);
  }

  transfer->flags = 0x00;
  transfer->chCount = 0;
  return 0;
}
