#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "event-recorder.h"

#define RING_SIZE 1048576

typedef struct {
  int32_t ts;
  int32_t id;
  int32_t val;
} event_t;

static atomic_bool running = false;
static event_t *eventBuf = NULL;
static atomic_int eventBufPos = 0;

int eventrecorder_init (void) {
  eventBuf = (event_t*)malloc(sizeof(event_t) * RING_SIZE);
  if (eventBuf == NULL) return -2;
  memset(eventBuf, 0, sizeof(event_t) * RING_SIZE);

  running = true;
  return 0;
}

// DEBUG: I don't know if this is correct thread-safe code so don't use this in production
int eventrecorder_event1i (int32_t id, int32_t val) {
  if (!running) return -1;

  struct timespec tsp;
  if (clock_gettime(CLOCK_MONOTONIC, &tsp) != 0) return -2;

  // Atomically increments eventBufPos and saves the non-incremented value locally.
  // If we get preemted immediately after this operation the two threads won't have
  // the same eventBufPos value.
  int eventBufPosLocal = atomic_fetch_add_explicit(&eventBufPos, 1, memory_order_relaxed);
  if (eventBufPosLocal == RING_SIZE) return -3;

  // tv_nsec will fit into a 32-bit signed value as it's between 0 and 999,999,999
  event_t *event = &eventBuf[eventBufPosLocal];
  event->ts = tsp.tv_nsec;
  event->id = id;
  event->val = val;
  return 0;
}

int eventrecorder_writeFile (const char *filename) {
  running = false;

  FILE *file = fopen(filename, "wb");
  if (file == NULL) return -1;

  for (int i = 0; i < eventBufPos; i++) {
    fwrite(&eventBuf[i].ts, 4, 1, file);
    fwrite(&eventBuf[i].id, 4, 1, file);
    fwrite(&eventBuf[i].val, 4, 1, file);
  }

  fclose(file);
  free(eventBuf);
  eventBuf = NULL;
  // you can call eventrecorder_init() again now
  return 0;
}
