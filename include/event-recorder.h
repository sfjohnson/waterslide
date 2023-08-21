#ifndef _EVENTRECORDER_H
#define _EVENTRECORDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// NOTE: after calling eventrecorder_writeFile, you need to call eventrecorder_init again before eventrecorder_event1i

int eventrecorder_init (void);
int eventrecorder_event1i (int32_t id, int32_t val); // this is thread safe and realtime safe (no syscalls)
int eventrecorder_writeFile (const char *filename); // this is not

#ifdef __cplusplus
}
#endif

#endif
