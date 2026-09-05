#ifndef transport_h_INCLUDED
#define transport_h_INCLUDED

#include "tcpcompatlayer.h"
#include "tlsconfig.h"
#include <bearssl/inc/bearssl.h>

typedef enum {
  TRANSPORT_ERR_OK = 0,
  TRANSPORT_ERR_INVALID_ARGUMENT,
  TRANSPORT_ERR_OUT_OF_MEMORY,

  TRANSPORT_ERR_HOST_NOT_FOUND,
  TRANSPORT_ERR_DNS_TEMPORARY,
  TRANSPORT_ERR_DNS_FAILURE,
  TRANSPORT_ERR_CONNECTION_REFUSED,
  TRANSPORT_ERR_CONNECTION_RESET,
  TRANSPORT_ERR_CONNECTION_CLOSED,
  TRANSPORT_ERR_HOST_UNREACHABLE,
  TRANSPORT_ERR_NETWORK_UNREACHABLE,
  TRANSPORT_ERR_TIMED_OUT,
  TRANSPORT_ERR_TCP_NOT_INITIALIZED,
  TRANSPORT_ERR_TCP_ADDRESS_UNSUPPORTED,

  TRANSPORT_ERR_TLS_CERTIFICATE,
  TRANSPORT_ERR_TLS_HOSTNAME_MISMATCH,
  TRANSPORT_ERR_TLS_PROTOCOL,
  TRANSPORT_ERR_TLS_ALERT,
  TRANSPORT_ERR_TLS_TRUNCATED,

  TRANSPORT_ERR_IO,
  TRANSPORT_ERR_UNKNOWN,
} TransportError;

typedef struct tls_data TlsData;

typedef struct {
  bool use_tls;

  union {
    TcpSocket *tcp;
    TlsData *tls;
  };
} Transport ;
// takes ownership of the socket if no error
TransportError transport_from_tcp(Transport *transport, TcpSocket *socket);

// take ownership of the inner transport if no error
TransportError
transport_wrap_tls(Transport *transport, Transport inner, const char *host, TlsConfig *tlsconfig);

TransportError
transport_send(Transport *transport, const unsigned char *buf, size_t buflen);

TransportError
transport_recv(Transport *transport, unsigned char *buf, size_t buflen, size_t *received);

void transport_close(Transport *transport);

#endif // transport_h_INCLUDED
