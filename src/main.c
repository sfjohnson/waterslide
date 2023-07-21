#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include "config.h"
#include "globals.h"
#include "sender.h"
#include "receiver.h"
#include "monitor.h"
#include "audio.h"

static bool archChecks (void) {
  // We are going to use macros to test for pointer size, so make sure they are consistent with our runtime test.
  #if defined(W_32_BIT_POINTERS)
    if (sizeof(intptr_t) != 4) return false;
  #elif defined(W_64_BIT_POINTERS)
    if (sizeof(intptr_t) != 8) return false;
  #else
    return false;
  #endif

  bool lockFree = ATOMIC_LLONG_LOCK_FREE == 2 && ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_BOOL_LOCK_FREE == 2;
  return lockFree && sizeof(double) == 8 && ((-1) >> 1) < 0;
}

int main (int argc, char *argv[]) {
  // Disable full buffering when executed outside of a terminal (e.g. NodeJS spawn)
  setbuf(stdout, NULL);

  printf("Waterslide, build 70\n");

  if (argc < 2) {
    printf("First argument must be base64 encoded init config.\n");
    return EXIT_FAILURE;
  }

  if (!archChecks()) {
    printf("Architecture and compiler checks failed. The following were expected: lock-free 64-bit atomic integers, 64-bit double-precision floats, 32 or 64-bit pointers, and arithmetic right shift for negative numbers.\n");
    return EXIT_FAILURE;
  }

  int err = 0;
  if ((err = config_init(argv[1])) < 0) {
    printf("config_init failed: %d\n", err);
    return EXIT_FAILURE;
  }

  // if ((err = eventrecorder_init()) < 0) {
  //   printf("eventrecorder_init failed: %d\n", err);
  //   return EXIT_FAILURE;
  // }

  if ((err = monitor_init()) < 0) {
    printf("monitor_init failed: %d\n", err);
    return EXIT_FAILURE;
  }

  // TODO: allow graceful deinit if signal happens during network discovery

  int mode = globals_get1i(root, mode);
  if (mode == 0) {
    printf("Mode: sender\n");
    if ((err = sender_init()) < 0) {
      printf("sender_init failed: %d\n", err);
      return EXIT_FAILURE;
    }
  } else if (mode == 1) {
    printf("Mode: receiver\n");
    if ((err = receiver_init()) < 0) {
      printf("receiver_init failed: %d\n", err);
      return EXIT_FAILURE;
    }
  } else {
    printf("Invalid mode %d\n", mode);
    return EXIT_FAILURE;
  }

  sigset_t sigset;
  int sig;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGINT);
  sigaddset(&sigset, SIGHUP);
  sigaddset(&sigset, SIGQUIT);
  sigaddset(&sigset, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &sigset, NULL);
  sigwait(&sigset, &sig);

  if ((err = config_deinit()) < 0) {
    printf("config_deinit failed: %d\n", err);
    return EXIT_FAILURE;
  }
  if ((err = audio_deinit()) < 0) {
    printf("audio_deinit failed: %d\n", err);
    return EXIT_FAILURE;
  }

  printf("\ndeinit successful.\n");
  return EXIT_SUCCESS;
}
