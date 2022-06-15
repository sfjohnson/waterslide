#ifndef _ENDPOINT_H
#define _ENDPOINT_H

#include <stdbool.h>
#include <stdint.h>

int endpoint_init (int (*onPacket)(const uint8_t*, int, int));
// endpoint_send returns an int for API compatibility with endpointsec_send. The return value is always 0.
int endpoint_send (const uint8_t *buf, int bufLen);
void endpoint_deinit ();

#endif
