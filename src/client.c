#include "client.h"
#include "tcpcompatlayer.h"
#include "tcpcompatlayer_error.h"
#include "tlsconfig.h"
#include "transport.h"
#include <asprintf/asprintf.h>
#include <stddefer.h>
#include <stdio.h>
#include <stdlib.h>

#define USER_AGENT "zpk"

typedef enum {
  HTTP_CALLBACK_ERR_OK = 0,
  HTTP_CALLBACK_ERR_OUT_OF_MEMORY,
  HTTP_CALLBACK_ERR_IO
} HttpCallbackError;

static HttpClientError convert_callback_err(HttpCallbackError e) {
  switch (e) {
    case HTTP_CALLBACK_ERR_OK:
      return HTTP_CLIENT_ERR_OK;
    case HTTP_CALLBACK_ERR_OUT_OF_MEMORY:
      return HTTP_CLIENT_ERR_OUT_OF_MEMORY;
    case HTTP_CALLBACK_ERR_IO:
      return HTTP_CLIENT_ERR_CALLBACK_IO;
  }
}

static HttpClientError convert_tcp_error(TcpError e) {
  switch (e) {
    case TCP_ERR_OK:
      return HTTP_CLIENT_ERR_OK;
    case TCP_ERR_INVALID_ARGUMENT:
      return HTTP_CLIENT_ERR_TCP_INVALID_ARGUMENT;
    case TCP_ERR_OUT_OF_MEMORY:
      return HTTP_CLIENT_ERR_OUT_OF_MEMORY;
    case TCP_ERR_HOST_NOT_FOUND:
      return HTTP_CLIENT_ERR_TCP_HOST_NOT_FOUND;
    case TCP_ERR_DNS_TEMPORARY:
      return HTTP_CLIENT_ERR_TCP_DNS_TEMPORARY;
    case TCP_ERR_DNS_FAILURE:
      return HTTP_CLIENT_ERR_TCP_DNS_FAILURE;
    case TCP_ERR_CONNECTION_REFUSED:
      return HTTP_CLIENT_ERR_TCP_CONNECTION_REFUSED;
    case TCP_ERR_CONNECTION_RESET:
      return HTTP_CLIENT_ERR_TCP_CONNECTION_RESET;
    case TCP_ERR_CONNECTION_CLOSED:
      return HTTP_CLIENT_ERR_TCP_CONNECTION_CLOSED;
    case TCP_ERR_HOST_UNREACHABLE:
      return HTTP_CLIENT_ERR_TCP_HOST_UNREACHABLE;
    case TCP_ERR_NETWORK_UNREACHABLE:
      return HTTP_CLIENT_ERR_TCP_NETWORK_UNREACHABLE;
    case TCP_ERR_TIMED_OUT:
      return HTTP_CLIENT_ERR_TCP_TIMED_OUT;
    case TCP_ERR_NOT_INITIALIZED:
      return HTTP_CLIENT_ERR_TCP_NOT_INITIALIZED;
    case TCP_ERR_ADDRESS_UNSUPPORTED:
      return HTTP_CLIENT_ERR_TCP_ADDRESS_UNSUPPORTED;
    case TCP_ERR_IO:
      return HTTP_CLIENT_ERR_TCP_IO;
    case TCP_ERR_UNKNOWN:
      return HTTP_CLIENT_ERR_TCP_UNKNOWN;
  }
}

static HttpClientError convert_transport_error(TransportError e) {
  switch (e) {

    case TRANSPORT_ERR_OK:
    case TRANSPORT_ERR_INVALID_ARGUMENT:
    case TRANSPORT_ERR_OUT_OF_MEMORY:
    case TRANSPORT_ERR_HOST_NOT_FOUND:
    case TRANSPORT_ERR_DNS_TEMPORARY:
    case TRANSPORT_ERR_DNS_FAILURE:
    case TRANSPORT_ERR_CONNECTION_REFUSED:
    case TRANSPORT_ERR_CONNECTION_RESET:
    case TRANSPORT_ERR_CONNECTION_CLOSED:
    case TRANSPORT_ERR_HOST_UNREACHABLE:
    case TRANSPORT_ERR_NETWORK_UNREACHABLE:
    case TRANSPORT_ERR_TIMED_OUT:
    case TRANSPORT_ERR_TCP_NOT_INITIALIZED:
    case TRANSPORT_ERR_TCP_ADDRESS_UNSUPPORTED:
    case TRANSPORT_ERR_TLS_CERTIFICATE:
    case TRANSPORT_ERR_TLS_HOSTNAME_MISMATCH:
    case TRANSPORT_ERR_TLS_PROTOCOL:
    case TRANSPORT_ERR_TLS_ALERT:
    case TRANSPORT_ERR_TLS_TRUNCATED:
    case TRANSPORT_ERR_IO:
    case TRANSPORT_ERR_UNKNOWN:
      break;
  }
}

static HttpClientError http_client_get_wcallback(
  const char *host,
  const char *port,
  const char *path,
  TlsConfig *tls,
  void *context,
  HttpCallbackError (*callback)(void *context, const unsigned char *buf, size_t buflen)
) {
  // open tcp
  TcpSocket *socket;
  TcpError tce = tcp_connect_portable(&socket, host, port);
  if (tce != TCP_ERR_OK) {
    return convert_tcp_error(tce);
  }
  Transport inner;
  TransportError ite = transport_from_tcp(&inner, socket);
  if (ite != TRANSPORT_ERR_OK) {
    tcp_close_portable(socket);
    return convert_transport_error(ite);
  }

  // maybe add TLS
  Transport transport;
  if (tls == NULL) {
    transport = inner;
  } else {
    TransportError te = transport_wrap_tls(&transport, &inner, host, tls);
    if (te != TRANSPORT_ERR_OK) {
      transport_close(&inner);
      return convert_transport_error(te);
    }
  }
  defer transport_close(&transport);

  // send HTTP 1.1 GET request
  char *req;
  int r = asprintf(
    &req,
    "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nConnection: close\r\n\r\n",
    path,
    host,
    USER_AGENT
  );
  if (r < 0) {
    return HTTP_CLIENT_ERR_OUT_OF_MEMORY;
  }
  defer free(req);
  TransportError se = transport_send(&transport, (const unsigned char *)req, (size_t)r);
  if (se != TRANSPORT_ERR_OK) {
    return convert_transport_error(se);
  }

  return HTTP_CLIENT_ERR_OK;
}

typedef struct {
  unsigned char *out;
  size_t outlen;
  size_t outcap;
} ToMemContext;

static HttpCallbackError tomem_callback(void *context, const unsigned char *buf, size_t buflen) {
  ToMemContext *tmc = context;
  if (tmc->outlen + buflen > tmc->outcap) {
    tmc->outcap *= 2;
    tmc->out = realloc(tmc->out, tmc->outcap);
    if (tmc->out == NULL) {
      return HTTP_CALLBACK_ERR_OUT_OF_MEMORY;
    }
  }
  memcpy(tmc->out + tmc->outlen, buf, buflen);
  tmc->outlen += buflen;
  return HTTP_CALLBACK_ERR_OK;
}

static HttpCallbackError tofile_callback(void *context, const unsigned char *buf, size_t buflen) {
  FILE *f = context;
  size_t written = fwrite(buf, 1, buflen, f);
  // error if written < buflen
  return written == buflen ? HTTP_CALLBACK_ERR_OK : HTTP_CALLBACK_ERR_IO;
}

HttpClientError http_client_get_tomem(
  const char *host,
  const char *port,
  const char *path,
  TlsConfig *tls,
  unsigned char **out,
  size_t *outlen
) {
  ToMemContext c = {.out = malloc(64), .outlen = 0, .outcap = 64};
  HttpClientError e = http_client_get_wcallback(host, port, path, tls, &c, tomem_callback);
  if (e == HTTP_CLIENT_ERR_OK) {
    *out = c.out;
    *outlen = c.outlen;
  } else {
    *out = NULL;
    *outlen = 0;
  }
  return e;
}

HttpClientError
http_client_get_tofile(const char *host, const char *port, const char *path, TlsConfig *tls, FILE *out) {
  return http_client_get_wcallback(host, port, path, tls, out, tofile_callback);
}
