#ifndef transport_h_INCLUDED
#define transport_h_INCLUDED

#include "tlsconfig.h"
#include "tcpcompatlayer.h"

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
    // if false
    TcpSocket *socket;
    // if true
    TlsData* tls;
} Transport;


TransportError transport_connect(
    Transport* transport,
    const char* host,
    const char* port,
    bool use_tls,
    TlsConfig *tlsconfig
);

TransportError transport_send(
    Transport* transport,
    const char* data,
    size_t length
);

TransportError transport_recv(
    Transport* transport,
    char* buffer,
    size_t length,
    size_t* received
);

void transport_close(Transport* transport);

#endif // transport_h_INCLUDED
