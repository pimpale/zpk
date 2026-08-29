#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>

#include "tcpcompatlayer.h"

#include <limits.h>
#include <stdlib.h>

struct tcp_socket {
  SOCKET handle;
};

static TcpError error_from_winsock(int error) {
  switch (error) {
    case WSAEINVAL:
    case WSAEFAULT:
      return TCP_ERR_INVALID_ARGUMENT;
    case WSA_NOT_ENOUGH_MEMORY:
    case WSAENOBUFS:
      return TCP_ERR_OUT_OF_MEMORY;
    case WSAHOST_NOT_FOUND:
    case WSANO_DATA:
      return TCP_ERR_HOST_NOT_FOUND;
    case WSATRY_AGAIN:
      return TCP_ERR_DNS_TEMPORARY;
    case WSANO_RECOVERY:
    case WSATYPE_NOT_FOUND:
      return TCP_ERR_DNS_FAILURE;
    case WSAECONNREFUSED:
      return TCP_ERR_CONNECTION_REFUSED;
    case WSAECONNRESET:
    case WSAECONNABORTED:
      return TCP_ERR_CONNECTION_RESET;
    case WSAESHUTDOWN:
    case WSAENOTCONN:
      return TCP_ERR_CONNECTION_CLOSED;
    case WSAEHOSTUNREACH:
    case WSAEADDRNOTAVAIL:
      return TCP_ERR_HOST_UNREACHABLE;
    case WSAENETDOWN:
    case WSAENETUNREACH:
      return TCP_ERR_NETWORK_UNREACHABLE;
    case WSAETIMEDOUT:
      return TCP_ERR_TIMED_OUT;
    case WSANOTINITIALISED:
    case WSASYSNOTREADY:
    case WSAVERNOTSUPPORTED:
      return TCP_ERR_NOT_INITIALIZED;
    case WSAEAFNOSUPPORT:
    case WSAEPFNOSUPPORT:
      return TCP_ERR_ADDRESS_UNSUPPORTED;
    default:
      return TCP_ERR_IO;
  }
}

TcpError tcp_init_portable(void) {
  WSADATA data;
  int result = WSAStartup(MAKEWORD(2, 2), &data);
  if (result != 0) {
    return error_from_winsock(result);
  }
  if (LOBYTE(data.wVersion) != 2 || HIBYTE(data.wVersion) != 2) {
    (void)WSACleanup();
    return TCP_ERR_NOT_INITIALIZED;
  }
  return TCP_ERR_OK;
}

void tcp_cleanup_portable(void) {
  (void)WSACleanup();
}

TcpError tcp_connect_portable(TcpSocket **out, const char *hostname,
                              const char *port) {
  *out = NULL;

  struct addrinfo hints = {0};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;

  struct addrinfo *addresses = NULL;
  int gai_error = getaddrinfo(hostname, port, &hints, &addresses);
  if (gai_error != 0) {
    return error_from_winsock(gai_error);
  }

  SOCKET connected = INVALID_SOCKET;
  int last_error = 0;

  for (struct addrinfo *address = addresses; address != NULL;
       address = address->ai_next) {
    SOCKET candidate = socket(address->ai_family, address->ai_socktype,
                              address->ai_protocol);
    if (candidate == INVALID_SOCKET) {
      last_error = WSAGetLastError();
      continue;
    }

    if (connect(candidate, address->ai_addr, (int)address->ai_addrlen) == 0) {
      connected = candidate;
      break;
    }

    last_error = WSAGetLastError();
    (void)closesocket(candidate);
  }

  freeaddrinfo(addresses);

  if (connected == INVALID_SOCKET) {
    return last_error == 0 ? TCP_ERR_UNKNOWN
                           : error_from_winsock(last_error);
  }

  TcpSocket *socket = malloc(sizeof(*socket));
  if (socket == NULL) {
    (void)closesocket(connected);
    return TCP_ERR_OUT_OF_MEMORY;
  }

  socket->handle = connected;
  *out = socket;
  return TCP_ERR_OK;
}

void tcp_close_portable(TcpSocket *socket) {
  if (socket == NULL) {
    return;
  }
  (void)closesocket(socket->handle);
  free(socket);
}

TcpError tcp_recv_portable(TcpSocket *socket, size_t *recvd, unsigned char *buf,
                           size_t buflen) {
  if (socket == NULL || recvd == NULL || (buf == NULL && buflen != 0)) {
    return TCP_ERR_INVALID_ARGUMENT;
  }

  *recvd = 0;
  if (buflen == 0) {
    return TCP_ERR_OK;
  }

  int length = buflen > (size_t)INT_MAX ? INT_MAX : (int)buflen;
  while(true) {
    int result = recv(socket->handle, (char*)buf, length, 0);
    if (result > 0) {
      *recvd = (size_t)result;
      return TCP_ERR_OK;
    }
    if (result == 0) {
      return TCP_ERR_OK;
    }

    int error = WSAGetLastError();
    if (error == WSAEINTR) {
      continue;
    }
    return error_from_winsock(error);
  }
}

TcpError tcp_send_portable(TcpSocket *socket, size_t *sent, const unsigned  char *buf,
                           size_t buflen) {
  *sent = 0;
  while (*sent < buflen) {
    size_t remaining = buflen - *sent;
    int length = remaining > (size_t)INT_MAX ? INT_MAX : (int)remaining;
    int result = send(socket->handle, (const char*)buf + *sent, length, 0);

    if (result > 0) {
      *sent += (size_t)result;
      continue;
    }
    if (result == 0) {
      return TCP_ERR_CONNECTION_CLOSED;
    }

    int error = WSAGetLastError();
    if (error == WSAEINTR) {
      continue;
    }
    return error_from_winsock(error);
  }

  return TCP_ERR_OK;
}
