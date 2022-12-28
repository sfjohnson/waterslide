#include <stdio.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <string.h>
#include <unistd.h>
#include "boringtun/wireguard_ffi.h"
#include "globals.h"
#include "wsocket.h"

#define UNUSED __attribute__((unused))

static void handleRes (wsocket_t *sock, uint8_t *buf, ssize_t len) {
  enum wsocket_state currentState = sock->state;
  switch (currentState) {
    case Idle:
      if (len != 38) return;

      for (int i = 0; i < 32; i++) {
        if (buf[i] != sock->peerPubKey[i]) return;
      }

      for (int i = 0; i < 6; i++) {
        // XOR remote addr and port with myPubKey
        buf[32 + i] ^= sock->myPubKey[i];
      }

      memcpy(&sock->peerAddr, &buf[32], 4);
      memcpy(&sock->peerPort, &buf[36], 2);
      sock->state = GotPeerAddr;
      break;

    case GotPeerAddr:
      sock->onPacket(buf, len, sock->epIndex);
      break;
  }
}

static void *recvLoop (void *arg) {
  uint8_t recvBuf[1500] = { 0 };
  struct sockaddr_in recvAddr = { 0 };
  wsocket_t *sock = (wsocket_t *)arg;

  while (true) {
    socklen_t recvAddrLen = sizeof(recvAddr);
    ssize_t recvLen = recvfrom(sock->sock, recvBuf, sizeof(recvBuf), 0, (struct sockaddr*)&recvAddr, &recvAddrLen);

    // If recv failed, ignore it but wait a bit first
    // DEBUG: implement closing this socket and re-opening it on a timer
    if (recvLen < 0 || recvAddrLen != sizeof(recvAddr)) {
      usleep(10000);
      continue;
    }

    handleRes(sock, recvBuf, recvLen);
  }

  return NULL;
}

const char *wsocket_getErrorStr (int returnCode) {
  switch (returnCode) {
    case -1:
      return "wsocket_init: socket() failed";
    case -2:
      return "wsocket_init: interface bind failed. CAP_NET_RAW or root are required";
    case -3:
      return "wsocket_init: interface not found";
    case -4:
      return "wsocket_init: interface bind failed";
    case -5:
      return "wsocket_init: interface bind failed, OS not supported"; 
    case -6:
      return "wsocket_init: pthread_create() failed";
    case -7:
      return "wsocket_waitForPeerAddr: invalid wsocket state";
    case -8:
      return "wsocket_sendToPeer: invalid wsocket state";
    case -9:
      return "wsocket_sendToPeer: sendto() failed";
    default:
      return "wsocket_init: unknown error code";
  }
}

int wsocket_init (wsocket_t *sock, const uint8_t *myPubKey, const uint8_t *peerPubKey, int epIndex, const char *ifName, UNUSED int ifLen, int (*onPacket)(const uint8_t*, int, int)) {
  memset(sock, 0, sizeof(wsocket_t));
  memcpy(sock->myPubKey, myPubKey, 32);
  memcpy(sock->peerPubKey, peerPubKey, 32);
  sock->epIndex = epIndex;
  sock->state = Idle;
  sock->peerAddr = 0;
  sock->peerPort = 0;
  sock->onPacket = onPacket;

  sock->sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock->sock < 0) return -1;

  int err;
  // bind socket to interface
  #if defined(__ANDROID__) || defined(__linux__)
  err = setsockopt(sock->sock, SOL_SOCKET, SO_BINDTODEVICE, ifName, ifLen);
  if (err < 0) return -2;
  #elif defined(__MACH__)
  int ifIndex = if_nametoindex(ifName);
  if (ifIndex == 0) return -3;
  err = setsockopt(sock->sock, IPPROTO_IP, IP_BOUND_IF, &ifIndex, sizeof(ifIndex));
  if (err < 0) return -4;
  #else
  return -5;
  #endif

  // DEBUG: log
  printf("epIndex %d bound to interface %s\n", epIndex, ifName);

  err = pthread_create(&sock->recvThread, NULL, recvLoop, (void*)sock);
  if (err != 0) return -6;

  return 0;
}

int wsocket_waitForPeerAddr (wsocket_t *sock) {
  if (sock->state != Idle) return -7;

  uint8_t sendBuf[65];

  struct sockaddr_in serverAddr = { 0 };
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(globals_get1i(discovery, serverPort));
  serverAddr.sin_addr.s_addr = globals_get1ui(discovery, serverAddr);

  while (sock->state != GotPeerAddr) {
    memcpy(&sendBuf[0], sock->myPubKey, 32);
    memcpy(&sendBuf[32], sock->peerPubKey, 32);
    sendBuf[64] = sock->epIndex;
    sendto(sock->sock, sendBuf, sizeof(sendBuf), 0, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    usleep(500000);
  }

  return 0;
}

int wsocket_sendToPeer (const wsocket_t *sock, const uint8_t *buf, int bufLen) {
  if (sock->state != GotPeerAddr) return -8;

  struct sockaddr_in peerAddr = { 0 };
  peerAddr.sin_family = AF_INET;
  peerAddr.sin_addr.s_addr = sock->peerAddr;
  peerAddr.sin_port = sock->peerPort;
  ssize_t err = sendto(sock->sock, buf, bufLen, 0, (struct sockaddr*)&peerAddr, sizeof(peerAddr));
  if (err != bufLen) return -9;

  return 0;
}
