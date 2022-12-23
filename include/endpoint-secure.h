#ifndef _ENDPOINTSEC_H
#define _ENDPOINTSEC_H

#include <stdbool.h>
#include <stdint.h>

int endpointsec_init (int (*onPacket)(const uint8_t*, int, int));
int endpointsec_send (const uint8_t *buf, int bufLen); // NOTE: this is not thread safe
void endpointsec_deinit ();

#endif
