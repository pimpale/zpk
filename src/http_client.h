#ifndef client_h_INCLUDED
#define client_h_INCLUDED

#include <stdio.h>

#include "instances/vec_br_x509_trust_anchor.h"

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
  HTTP_CLIENT_ERR_TLS_CERTIFICATE,
  HTTP_CLIENT_ERR_TLS_HOSTNAME_MISMATCH,
  HTTP_CLIENT_ERR_TLS_PROTOCOL,
  HTTP_CLIENT_ERR_TLS_ALERT,
  HTTP_CLIENT_ERR_TLS_TRUNCATED,

  // missing functionality
  HTTP_CLIENT_ERR_TODO,

  // header parsing issues
  HTTP_CLIENT_ERR_HEADER_MALFORMED,
  HTTP_CLIENT_ERR_TRANSFER_ENCODING_UNSUPPORTED,

  // response parsing issues
  HTTP_CLIENT_ERR_RESPONSE_MALFORMED,
  HTTP_CLIENT_ERR_RESPONSE_TRUNCATED
} HttpClientError;

typedef enum {
  HTTP_CALLBACK_ERR_OK = 0,
  HTTP_CALLBACK_ERR_OUT_OF_MEMORY,
  HTTP_CALLBACK_ERR_IO
} HttpCallbackError;

const char *httpstrerror(HttpClientError error);


typedef enum {
  HTTP_PROTOCOL_HTTP,
  HTTP_PROTOCOL_HTTPS
} HttpProtocol;

HttpClientError http_client_get(
  HttpProtocol protocol,
  const char *host,
  const char *port,
  const char *path,
  // https-specific
  vec_br_x509_trust_anchor *anchors,
  bool strict_ssl,
  // callback
  void *context,
  HttpCallbackError (*callback)(void *context, const uint8_t *buf, size_t buflen)
);

#endif // client_h_INCLUDED
