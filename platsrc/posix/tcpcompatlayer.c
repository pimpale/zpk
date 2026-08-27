#define _POSIX_C_SOURCE 200809L

#include "tcpcompatlayer.h"

#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

struct tcp_socket {
  int sockfd;
};

static TcpError error_from_errno(int error) {
  switch (error) {
    case ECONNREFUSED:
      return TCP_ERR_CONNECTION_REFUSED;
    case ECONNRESET:
      return TCP_ERR_CONNECTION_RESET;
    case ETIMEDOUT:
      return TCP_ERR_TIMED_OUT;
    case EHOSTUNREACH:
      return TCP_ERR_HOST_UNREACHABLE;
    case ENETUNREACH:
      return TCP_ERR_NETWORK_UNREACHABLE;
    case EPIPE:
    case ENOTCONN:
      return TCP_ERR_CONNECTION_CLOSED;
    default:
      return TCP_ERR_IO;
  }
}

static TcpError error_from_gai(int error, int saved_errno) {
  switch (error) {
    case EAI_NONAME:
      return TCP_ERR_HOST_NOT_FOUND;
    case EAI_AGAIN:
      return TCP_ERR_DNS_TEMPORARY;
    case EAI_MEMORY:
      return TCP_ERR_OUT_OF_MEMORY;
    case EAI_FAMILY:
      return TCP_ERR_ADDRESS_UNSUPPORTED;
    case EAI_SYSTEM:
      return error_from_errno(saved_errno);
    default:
      return TCP_ERR_DNS_FAILURE;
  }
}

// global initialize and uninitialize hooks
TcpError tcp_init_portable(void) {
  return TCP_ERR_OK;
}

void tcp_cleanup_portable(void) {
}

TcpError tcp_connect_portable(TcpSocket **out, const char *hostname, const char *port) {
  *out = NULL;
  struct addrinfo hints = {0};

  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;

  struct addrinfo *addresses = NULL;

  int gair = getaddrinfo(hostname, port, &hints, &addresses);
  if (gair != 0) {
    return error_from_gai(gair, errno);
  }

  int sockfd = -1;
  int lasterrno = 0;
  for (struct addrinfo *address = addresses; address != NULL; address = address->ai_next) {
    sockfd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
    if (sockfd == -1) {
      lasterrno = errno;
      continue;
    }

    if (connect(sockfd, address->ai_addr, address->ai_addrlen) != 0) {
      lasterrno = errno;
      close(sockfd);
    } else {
      // no error, use
      break;
    }
  }

  freeaddrinfo(addresses);

  if (lasterrno != 0) {
    return error_from_errno(lasterrno);
  }

  *out = malloc(sizeof(TcpSocket));
  if (*out == NULL) {
    close(sockfd);
    return TCP_ERR_OUT_OF_MEMORY;
  }
  (*out)->sockfd = sockfd;
  return TCP_ERR_OK;
}

void tcp_close_portable(TcpSocket *socket) {
  close(socket->sockfd);
  free(socket);
}

TcpError tcp_recv_portable(TcpSocket *socket, size_t *recvd, char *buf, size_t buflen) {
  if (socket == NULL || recvd == NULL || (buf == NULL && buflen != 0)) {
    return TCP_ERR_INVALID_ARGUMENT;
  }

  *recvd = 0;
  if (buflen == 0) {
    return TCP_ERR_OK;
  }

  while (true) {
    ssize_t result = recv(socket->sockfd, buf, buflen, 0);
    if (result > 0) {
      *recvd = (size_t)result;
      return TCP_ERR_OK;
    }
    if (result == 0) {
      return TCP_ERR_OK;
    }
    if (errno == EINTR) {
      continue;
    }
    return error_from_errno(errno);
  }
}

TcpError tcp_send_portable(TcpSocket *socket, size_t *sent, const char *buf, size_t buflen) {
  if (socket == NULL || sent == NULL || (buf == NULL && buflen != 0)) {
    return TCP_ERR_INVALID_ARGUMENT;
  }
  *sent = 0;

  while (*sent < buflen) {
    ssize_t result = send(socket->sockfd, buf + *sent, buflen - *sent, 0);

    if (result > 0) {
      *sent += (size_t)result;
      continue;
    }
    if (result == 0) {
      // avoid looping forever
      return TCP_ERR_CONNECTION_CLOSED;
    }
    if (errno == EINTR) {
      continue;
    }
    return error_from_errno(errno);
  }

  return TCP_ERR_OK;
}
