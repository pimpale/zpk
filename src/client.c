#include "client.h"
#include "tcpcompatlayer.h"
#include "tcpcompatlayer_error.h"
#include "tlsconfig.h"
#include "transport.h"
#include <asprintf/asprintf.h>
#include <ctype.h>
#include <stddefer.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
      return HTTP_CLIENT_ERR_OK;
    case TRANSPORT_ERR_INVALID_ARGUMENT:
      return HTTP_CLIENT_ERR_TCP_INVALID_ARGUMENT;
    case TRANSPORT_ERR_OUT_OF_MEMORY:
      return HTTP_CLIENT_ERR_OUT_OF_MEMORY;
    case TRANSPORT_ERR_HOST_NOT_FOUND:
      return HTTP_CLIENT_ERR_TCP_HOST_NOT_FOUND;
    case TRANSPORT_ERR_DNS_TEMPORARY:
      return HTTP_CLIENT_ERR_TCP_DNS_TEMPORARY;
    case TRANSPORT_ERR_DNS_FAILURE:
      return HTTP_CLIENT_ERR_TCP_DNS_FAILURE;
    case TRANSPORT_ERR_CONNECTION_REFUSED:
      return HTTP_CLIENT_ERR_TCP_CONNECTION_REFUSED;
    case TRANSPORT_ERR_CONNECTION_RESET:
      return HTTP_CLIENT_ERR_TCP_CONNECTION_RESET;
    case TRANSPORT_ERR_CONNECTION_CLOSED:
      return HTTP_CLIENT_ERR_TCP_CONNECTION_CLOSED;
    case TRANSPORT_ERR_HOST_UNREACHABLE:
      return HTTP_CLIENT_ERR_TCP_HOST_UNREACHABLE;
    case TRANSPORT_ERR_NETWORK_UNREACHABLE:
      return HTTP_CLIENT_ERR_TCP_NETWORK_UNREACHABLE;
    case TRANSPORT_ERR_TIMED_OUT:
      return HTTP_CLIENT_ERR_TCP_TIMED_OUT;
    case TRANSPORT_ERR_TCP_NOT_INITIALIZED:
      return HTTP_CLIENT_ERR_TCP_NOT_INITIALIZED;
    case TRANSPORT_ERR_TCP_ADDRESS_UNSUPPORTED:
      return HTTP_CLIENT_ERR_TCP_ADDRESS_UNSUPPORTED;
    case TRANSPORT_ERR_TLS_CERTIFICATE:
      return HTTP_CLIENT_ERR_TLS_CERTIFICATE;
    case TRANSPORT_ERR_TLS_HOSTNAME_MISMATCH:
      return HTTP_CLIENT_ERR_TLS_HOSTNAME_MISMATCH;
    case TRANSPORT_ERR_TLS_PROTOCOL:
      return HTTP_CLIENT_ERR_TLS_PROTOCOL;
    case TRANSPORT_ERR_TLS_ALERT:
      return HTTP_CLIENT_ERR_TLS_ALERT;
    case TRANSPORT_ERR_TLS_TRUNCATED:
      return HTTP_CLIENT_ERR_TLS_TRUNCATED;
    case TRANSPORT_ERR_IO:
      return HTTP_CLIENT_ERR_TCP_IO;
    case TRANSPORT_ERR_UNKNOWN:
      return HTTP_CLIENT_ERR_TCP_UNKNOWN;
  }
}

const char *httpstrerror(HttpClientError error) {
  switch (error) {
    case HTTP_CLIENT_ERR_OK:
      return "no error";
    case HTTP_CLIENT_ERR_OUT_OF_MEMORY:
      return "out of memory";
    case HTTP_CLIENT_ERR_CALLBACK_IO:
      return "response output failed";
    case HTTP_CLIENT_ERR_TCP_INVALID_ARGUMENT:
      return "invalid networking argument";
    case HTTP_CLIENT_ERR_TCP_HOST_NOT_FOUND:
      return "host not found";
    case HTTP_CLIENT_ERR_TCP_DNS_TEMPORARY:
      return "temporary DNS resolution failure";
    case HTTP_CLIENT_ERR_TCP_DNS_FAILURE:
      return "DNS resolution failed";
    case HTTP_CLIENT_ERR_TCP_CONNECTION_REFUSED:
      return "connection refused";
    case HTTP_CLIENT_ERR_TCP_CONNECTION_RESET:
      return "connection reset by peer";
    case HTTP_CLIENT_ERR_TCP_CONNECTION_CLOSED:
      return "connection closed";
    case HTTP_CLIENT_ERR_TCP_HOST_UNREACHABLE:
      return "host is unreachable";
    case HTTP_CLIENT_ERR_TCP_NETWORK_UNREACHABLE:
      return "network is unreachable";
    case HTTP_CLIENT_ERR_TCP_TIMED_OUT:
      return "operation timed out";
    case HTTP_CLIENT_ERR_TCP_NOT_INITIALIZED:
      return "networking subsystem is not initialized";
    case HTTP_CLIENT_ERR_TCP_ADDRESS_UNSUPPORTED:
      return "address family is not supported";
    case HTTP_CLIENT_ERR_TCP_IO:
      return "network I/O failed";
    case HTTP_CLIENT_ERR_TCP_UNKNOWN:
      return "unknown networking error";
    case HTTP_CLIENT_ERR_TLS_CERTIFICATE:
      return "TLS certificate validation failed";
    case HTTP_CLIENT_ERR_TLS_HOSTNAME_MISMATCH:
      return "TLS certificate hostname mismatch";
    case HTTP_CLIENT_ERR_TLS_PROTOCOL:
      return "TLS protocol error";
    case HTTP_CLIENT_ERR_TLS_ALERT:
      return "TLS peer sent a fatal alert";
    case HTTP_CLIENT_ERR_TLS_TRUNCATED:
      return "TLS connection was truncated";
  }
}

typedef struct {
  uint32_t status;
  bool transfer_encoding_chunked;
  bool has_content_length;
  uint64_t content_length;
  size_t headers_len;
} HttpResponseHeaders;

static uint8_t ascii_lower(uint8_t c) {
  if (c >= 'A' && c <= 'Z') {
    return c - 'A' + 'a';
  }
  return c;
}

// s2 must be lower
static bool case_insensitive_buf_str_eq(const uint8_t *buf, size_t buflen, const char *s2) {
  if (buflen != strlen(s2)) {
    return false;
  }
  for (size_t i = 0; i < buflen; i++) {
    if (ascii_lower(buf[i]) != s2[i]) {
      return false;
    }
  }
  return true;
}

static HttpClientError
http_client_parse_statusline(HttpResponseHeaders *headers, uint8_t *line, size_t linelen) {
  const char *v = "HTTP/1.1 ";
  size_t vl = strlen(v);
  if (linelen < vl) {
    return HTTP_CLIENT_ERR_HEADER_MALFORMED;
  }

  if (memcmp(line, v, strlen(v)) != 0) {
    return HTTP_CLIENT_ERR_HEADER_MALFORMED;
  }

  size_t off = vl;
  if (off + 3 > linelen) {
    return HTTP_CLIENT_ERR_HEADER_MALFORMED;
  }

  uint8_t c1 = line[off + 0];
  uint8_t c2 = line[off + 1];
  uint8_t c3 = line[off + 2];
  if (!isdigit(c1) || !isdigit(c2) || !isdigit(c3)) {
    return HTTP_CLIENT_ERR_HEADER_MALFORMED;
  }
  headers->status = (c1 - '0') * 100 + (c2 - '0') * 10 + (c3 - '0');
  // ignore reason phrase
  return HTTP_CLIENT_ERR_OK;
}

// parses a number starting at buf. Returns false on error
static bool http_client_parse_uint64_t(uint64_t *out, uint8_t *buf, size_t buflen) {
}

static HttpClientError
http_client_parse_header(HttpResponseHeaders *headers, uint8_t *line, size_t linelen) {
  uint8_t *key_end = memchr(line, ':', linelen);
  if (key_end == NULL) {
    return HTTP_CLIENT_ERR_HEADER_MALFORMED;
  }
  size_t keylen = (size_t)(key_end - line);

  // skip whitespace post-colon
  size_t off = keylen + 1;
  while (off < linelen && (line[off] == ' ' || line[off] == '\t')) {
    off++;
  }

  // now we branch based on the key value

  if (case_insensitive_buf_str_eq(line, keylen, "content-length")) {
    headers->has_content_length = true;
    if (!http_client_parse_uint64_t(&headers->content_length, line + off, linelen - off)) {
      return HTTP_CLIENT_ERR_HEADER_MALFORMED;
    }
  } else if (case_insensitive_buf_str_eq(line, keylen, "transfer-encoding")) {
    if (!case_insensitive_buf_str_eq(line + off, linelen - off, "chunked")) {
      return HTTP_CLIENT_ERR_TRANSFER_METHOD_UNSUPPORTED;
    }
    headers->transfer_encoding_chunked = true;
  }

  // otherwise we just ignore everything
  return HTTP_CLIENT_ERR_OK;
}

static bool linelen(size_t *linelen, uint8_t *buf, size_t buflen) {
  uint8_t *p = memchr(buf, '\n', buflen);
  if (p == NULL) {
    *linelen = 0;
    return false;
  }
  size_t l = (size_t)(p - buf);
  if (l == 0) {
    // we need space for the \r
    *linelen = 0;
    return false;
  }
  if (buf[l - 1] != '\r') {
    *linelen = 0;
    return false;
  }
  *linelen = l + 1;
  return true;
}

static HttpClientError
http_client_parse_response_headers(HttpResponseHeaders *headers, uint8_t *buf, size_t buflen) {
  size_t off = 0;
  size_t sll;
  if (!linelen(&sll, buf + off, buflen - off)) {
    return HTTP_CLIENT_ERR_HEADER_MALFORMED;
  }
  HttpClientError sle = http_client_parse_statusline(headers, buf + off, sll - 2);
  if (sle != HTTP_CLIENT_ERR_OK) {
    return sle;
  }
  off += sll;

  while (true) {
    size_t ll;
    if (!linelen(&ll, buf + off, buflen - off)) {
      return HTTP_CLIENT_ERR_HEADER_MALFORMED;
    }
    if (ll == 2) {
      headers->headers_len = off + ll;
      return HTTP_CLIENT_ERR_OK;
    }
    HttpClientError he = http_client_parse_header(headers, buf + off, ll - 2);
    if (he != HTTP_CLIENT_ERR_OK) {
      return he;
    }
    off += ll;
  }
}

static HttpClientError http_client_parse_body_chunked_encoding(
  Transport *transport,
  uint8_t *bodystart,
  size_t bodystartlen,
  void *context,
  HttpCallbackError (*callback)(void *context, const uint8_t *buf, size_t buflen)
) {
  
}

static HttpClientError http_client_parse_body_content_length(
  Transport *transport,
  uint8_t *bodystart,
  size_t bodystartlen,
  void *context,
  HttpCallbackError (*callback)(void *context, const uint8_t *buf, size_t buflen),
  bool has_content_length,
  size_t content_length
) {
}

static bool has_memstr(const uint8_t *buf, size_t buflen, const char *needle) {
  size_t needlelen = strlen(needle);
  if (buflen < needlelen) {
    return false;
  }
  for (size_t i = 0; i <= buflen - needlelen; i++) {
    bool found = true;
    for (size_t j = 0; j < needlelen; j++) {
      if (buf[i + j] != needle[j]) {
        found = false;
        break;
      }
    }
    if (found) {
      return true;
    }
  }
  return false;
}

static HttpClientError http_client_get_wcallback(
  const char *host,
  const char *port,
  const char *path,
  TlsConfig *tls,
  void *context,
  HttpCallbackError (*callback)(void *context, const uint8_t *buf, size_t buflen)
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
    TransportError te = transport_wrap_tls(&transport, inner, host, tls);
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
  TransportError se = transport_send(&transport, (const uint8_t *)req, (size_t)r);
  if (se != TRANSPORT_ERR_OK) {
    return convert_transport_error(se);
  }

  uint8_t respb[65536];
  size_t respb_len = 0;
  while (respb_len < sizeof(respb)) {
    size_t received;
    TransportError hge =
      transport_recv(&transport, respb + respb_len, sizeof(respb) - respb_len, &received);
    if (hge != TRANSPORT_ERR_OK) {
      return convert_transport_error(hge);
    }
    if (received == 0) {
      break;
    }
    respb_len += received;
    if (has_memstr(respb, respb_len, "\r\n\r\n")) {
      break;
    }
  }

  // parse the headers
  HttpResponseHeaders hrh = {0};
  HttpClientError re = http_client_parse_response_headers(&hrh, respb, respb_len);
  if (re != HTTP_CLIENT_ERR_OK) {
    return re;
  }

  if (hrh.status != 200) {
    return HTTP_CLIENT_ERR_TODO;
  }

  uint8_t *bodystart = respb + hrh.headers_len;
  size_t bodystartlen = respb_len - hrh.headers_len;

  if (hrh.transfer_encoding_chunked) {
    return http_client_parse_body_chunked_encoding(
      &transport,
      bodystart,
      bodystartlen,
      context,
      callback
    );
  } else {
    return http_client_parse_body_content_length(
      &transport,
      bodystart,
      bodystartlen,
      context,
      callback,
      hrh.has_content_length,
      hrh.content_length
    );
  }
}

static HttpCallbackError tomem_callback(void *context, const uint8_t *buf, size_t buflen) {
  vec_uint8_t *ctx = context;
  vec_uint8_t_pushv(ctx, buf, buflen);
  return HTTP_CALLBACK_ERR_OK;
}

static HttpCallbackError tofile_callback(void *context, const uint8_t *buf, size_t buflen) {
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
  vec_uint8_t *mem
) {
  return http_client_get_wcallback(host, port, path, tls, mem, tomem_callback);
}

HttpClientError http_client_get_tofile(
  const char *host,
  const char *port,
  const char *path,
  TlsConfig *tls,
  FILE *out
) {
  return http_client_get_wcallback(host, port, path, tls, out, tofile_callback);
}
