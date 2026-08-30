#ifndef client_h_INCLUDED
#define client_h_INCLUDED

#include <stdio.h>

#include "tlsconfig.h"
#include "instances/vec_char.h"

typedef enum {
  HTTP_CLIENT_ERR_OK = 0,
  HTTP_CLIENT_ERR_OUT_OF_MEMORY,
  HTTP_CLIENT_ERR_CALLBACK_IO,
  HTTP_CLIENT_ERR_TCP_INVALID_ARGUMENT,
  HTTP_CLIENT_ERR_TCP_HOST_NOT_FOUND,
  HTTP_CLIENT_ERR_TCP_DNS_TEMPORARY,
  HTTP_CLIENT_ERR_TCP_DNS_FAILURE,
  HTTP_CLIENT_ERR_TCP_CONNECTION_REFUSED,
  HTTP_CLIENT_ERR_TCP_CONNECTION_RESET,
  HTTP_CLIENT_ERR_TCP_CONNECTION_CLOSED,
  HTTP_CLIENT_ERR_TCP_HOST_UNREACHABLE,
  HTTP_CLIENT_ERR_TCP_NETWORK_UNREACHABLE,
  HTTP_CLIENT_ERR_TCP_TIMED_OUT,
  HTTP_CLIENT_ERR_TCP_NOT_INITIALIZED,
  HTTP_CLIENT_ERR_TCP_ADDRESS_UNSUPPORTED,
  HTTP_CLIENT_ERR_TCP_IO,
  HTTP_CLIENT_ERR_TCP_UNKNOWN,

} HttpClientError;

HttpClientError http_client_get_tomem(
  const char *host,
  const char *port,
  const char *path,
  TlsConfig *tls,
  vec_char* mem
);
HttpClientError http_client_get_tofile(const char *host, const char *port, const char *path, TlsConfig *tls, FILE *out);

#endif // client_h_INCLUDED
