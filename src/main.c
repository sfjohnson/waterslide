#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include "config.h"
#include "globals.h"
#include "sender.h"
#include "receiver.h"
#include "monitor.h"

static bool archChecks () {
  return sizeof(double) == 8 &&
    sizeof(float) == 4 &&
    sizeof(intptr_t) >= 4 &&
    ((-1) >> 1) < 0;
}

int main (int argc, char *argv[]) {
  // Disable full buffering when executed outside of a terminal (e.g. NodeJS spawn)
  setbuf(stdout, NULL);

  printf("Waterslide, build 48\n");

  if (argc < 2) {
    printf("First argument must be base64 encoded init config.\n");
    return EXIT_FAILURE;
  }

  if (!archChecks()) {
    printf("Architecture and compiler checks failed: expected 64-bit double, 32-bit float, at least 32-bit pointers, and arithmetic right shift for negative numbers.\n");
    return EXIT_FAILURE;
  }

  int err = 0;

  if ((err = config_init(argv[1])) < 0) {
    printf("Parsing init config failed: %d\n", err);
    return EXIT_FAILURE;
  }

  if ((err = monitor_init()) < 0) {
    printf("Monitor init failed: %d\n", err);
    return EXIT_FAILURE;
  }

  int mode = globals_get1i(root, mode);
  if (mode == 0) {
    printf("Mode: sender\n");
    if ((err = sender_init()) < 0) {
      printf("Sender init failed: %d\n", err);
      return EXIT_FAILURE;
    }
  } else if (mode == 1) {
    printf("Mode: receiver\n");
    if ((err = receiver_init()) < 0) {
      printf("Receiver init failed: %d\n", err);
      return EXIT_FAILURE;
    }
  } else {
    printf("Invalid mode %d\n", mode);
    return EXIT_FAILURE;
  }

  pause();
  return EXIT_SUCCESS;
}
