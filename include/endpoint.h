#ifndef _ENDPOINT_H
#define _ENDPOINT_H

#include <stdbool.h>
#include <stdint.h>

int endpoint_init (bool rx, uint16_t _port, uint32_t remoteAddr, uint32_t bindAddr, int (*_onPacket)(const uint8_t*, int));
int endpoint_send (const uint8_t *buf, int bufLen);

// DEBUG: hack for relay
int endpoint_sendRelay (const uint8_t *buf, int bufLen, uint32_t remoteAddr);

#endif
