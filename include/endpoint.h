#ifndef _ENDPOINT_H
#define _ENDPOINT_H

#include <stdbool.h>
#include <stdint.h>

int endpoint_init (bool rx, int (*onPacket)(const uint8_t*, int));
int endpoint_send (const uint8_t *buf, int bufLen);
void endpoint_deinit ();

#endif
