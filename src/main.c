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

// DEBUG: test
// #define SYNC_RECORD_LENGTH 450 // 30 mins
// #include "syncer.h"
// #include "event-recorder.h"

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

  printf("Waterslide, build 77\n");

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
    printf("monitor_init failed: %d, continuing without monitor...\n", err);
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

  // DEBUG: test
  // FILE *syncDataFile = fopen("receiver-sync.data", "w+b");
  // uint8_t *syncDataBuf = (uint8_t *)malloc(8 * SYNC_RECORD_LENGTH);
  // printf("writing to receiver-sync.data...\n");
  // for (int i = 0; i < SYNC_RECORD_LENGTH; i++) {
  //   double receiverSync;
  //   globals_get1ff(statsCh1Audio, receiverSyncFilt, &receiverSync);
  //   memcpy(&syncDataBuf[8*i], &receiverSync, 8);

  //   if (i == 2) {
  //     printf("syncer_changeRate returned %d\n", syncer_changeRate(48004.17));
  //   }

  //   sleep(4);
  // }

  // printf("eventrecorder_writeFile returned %d\n", eventrecorder_writeFile("sender-events.data"));

  // fwrite(syncDataBuf, 8 * SYNC_RECORD_LENGTH, 1, syncDataFile);
  // fclose(syncDataFile);
  // printf("done!\n");

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
