#include "tcpcompatlayer_error.h"

const char *tcpstrerror(TcpError error) {
  switch (error) {
    case TCP_ERR_OK:
      return "no error";
    case TCP_ERR_INVALID_ARGUMENT:
      return "invalid argument";
    case TCP_ERR_OUT_OF_MEMORY:
      return "out of memory";
    case TCP_ERR_HOST_NOT_FOUND:
      return "host not found";
    case TCP_ERR_DNS_TEMPORARY:
      return "temporary DNS resolution failure";
    case TCP_ERR_DNS_FAILURE:
      return "DNS resolution failed";
    case TCP_ERR_CONNECTION_REFUSED:
      return "connection refused";
    case TCP_ERR_CONNECTION_RESET:
      return "connection reset by peer";
    case TCP_ERR_CONNECTION_CLOSED:
      return "connection closed";
    case TCP_ERR_HOST_UNREACHABLE:
      return "host is unreachable";
    case TCP_ERR_NETWORK_UNREACHABLE:
      return "network is unreachable";
    case TCP_ERR_TIMED_OUT:
      return "operation timed out";
    case TCP_ERR_NOT_INITIALIZED:
      return "networking subsystem is not initialized";
    case TCP_ERR_ADDRESS_UNSUPPORTED:
      return "address family is not supported";
    case TCP_ERR_IO:
      return "network I/O failed";
    case TCP_ERR_UNKNOWN:
      return "unknown networking error";
  }
  return "unknown networking error";
}
