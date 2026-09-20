#include "http_client.h"
#include "instances/slice_uint8_t.h"
#include "instances/vec_slice_uint8_t.h"
#include "tcpcompatlayer.h"
#include "tcpcompatlayer_error.h"
#include "transport.h"
#include <asprintf/asprintf.h>
#include <ctype.h>
#include <stddefer.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USER_AGENT "zpk"

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
    case HTTP_CLIENT_ERR_TODO:
      return "unimplemented feature (file a bug report)";
    case HTTP_CLIENT_ERR_HEADER_MALFORMED:
      return "header is malformed";
    case HTTP_CLIENT_ERR_TRANSFER_ENCODING_UNSUPPORTED:
      return "transfer encoding is unsupported";
    case HTTP_CLIENT_ERR_RESPONSE_MALFORMED:
      return "response is malformed";
    case HTTP_CLIENT_ERR_RESPONSE_TRUNCATED:
      return "response is truncated";
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

// parses a number. Returns false on error
static bool http_client_parse_header_number(uint64_t *out, uint8_t *line, size_t linelen) {
  bool line_empty = true;
  bool finished_number = false;
  *out = 0;
  for (size_t i = 0; i < linelen; i++) {
    size_t digit = 0;
    switch (line[i]) {
      case '0' ... '9':
        if (finished_number) {
          // number restarted after whitespace, malformed
          return false;
        }
        digit = line[i] - '0';
        line_empty = false;
        break;
      case '\t':
      case ' ':
        if (!line_empty) {
          finished_number = true;
        }
        continue;
      default:
        // bad char
        return false;
    }

    if (*out > (UINT64_MAX - digit) / 10) {
      return false;
    }
    *out = *out * 10 + digit;
  }

  return !line_empty;
}

static bool istokenc(uint8_t c) {
  switch (c) {
    case '0' ... '9':
    case 'a' ... 'z':
    case 'A' ... 'Z':
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '\'':
    case '*':
    case '+':
    case '-':
    case '.':
    case '^':
    case '_':
    case '`':
    case '|':
    case '~':
      return true;
    default:
      return false;
  }
}

// parses a list of tokens, returning false on error
static HttpClientError
http_client_parse_header_token_list(vec_slice_uint8_t *l, uint8_t *line, size_t linelen) {
  typedef enum {
    LIST_BEFORE_TOKEN,
    LIST_IN_TOKEN,
    LIST_AFTER_TOKEN
  } ListState;

  ListState state = LIST_BEFORE_TOKEN;
  size_t starti = 0;
  for (size_t i = 0; i < linelen; i++) {
    switch (state) {
      case LIST_BEFORE_TOKEN:
        switch (line[i]) {
          case ' ':
          case '\t':
            break;
          case ',':
            // empty entry
            break;
          default:
            if (istokenc(line[i])) {
              starti = i;
              state = LIST_IN_TOKEN;
            } else {
              return HTTP_CLIENT_ERR_HEADER_MALFORMED;
            }
        }
        break;
      case LIST_IN_TOKEN:
        switch (line[i]) {
          case ',': {
            slice_uint8_t p = {.data = line + starti, .len = i - starti};
            if (vec_slice_uint8_t_push(l, &p) != 0) {
              return HTTP_CLIENT_ERR_OUT_OF_MEMORY;
            }
            state = LIST_BEFORE_TOKEN;
            break;
          }
          case ' ':
          case '\t': {
            slice_uint8_t p = {.data = line + starti, .len = i - starti};
            if (vec_slice_uint8_t_push(l, &p) != 0) {
              return HTTP_CLIENT_ERR_OUT_OF_MEMORY;
            }
            state = LIST_AFTER_TOKEN;
            break;
          }
          default:
            if (!istokenc(line[i])) {
              return HTTP_CLIENT_ERR_HEADER_MALFORMED;
            }
            break;
        }
        break;
      case LIST_AFTER_TOKEN:
        switch (line[i]) {
          case ',':
            state = LIST_BEFORE_TOKEN;
            break;
          case ' ':
          case '\t':
            break;
          default:
            return HTTP_CLIENT_ERR_HEADER_MALFORMED;
        }
        break;
    }
  }
  if (state == LIST_IN_TOKEN) {
    slice_uint8_t p = {.data = line + starti, .len = linelen - starti};
    if (vec_slice_uint8_t_push(l, &p) != 0) {
      return HTTP_CLIENT_ERR_OUT_OF_MEMORY;
    }
  }
  return HTTP_CLIENT_ERR_OK;
}

static HttpClientError
http_client_parse_header(HttpResponseHeaders *headers, uint8_t *line, size_t linelen) {
  uint8_t *key_end = memchr(line, ':', linelen);
  if (key_end == NULL) {
    return HTTP_CLIENT_ERR_HEADER_MALFORMED;
  }
  size_t keylen = (size_t)(key_end - line);
  size_t off = keylen + 1;

  // now we branch based on the key value
  if (case_insensitive_buf_str_eq(line, keylen, "content-length")) {
    if (headers->has_content_length) {
      return HTTP_CLIENT_ERR_HEADER_MALFORMED;
    }
    headers->has_content_length = true;
    if (!http_client_parse_header_number(&headers->content_length, line + off, linelen - off)) {
      return HTTP_CLIENT_ERR_HEADER_MALFORMED;
    }
  } else if (case_insensitive_buf_str_eq(line, keylen, "transfer-encoding")) {
    if (headers->transfer_encoding_chunked) {
      return HTTP_CLIENT_ERR_HEADER_MALFORMED;
    }
    vec_slice_uint8_t tokens;
    vec_slice_uint8_t_init(&tokens);
    defer vec_slice_uint8_t_delete(&tokens);
    HttpClientError e = http_client_parse_header_token_list(&tokens, line + off, linelen - off);
    if (e != HTTP_CLIENT_ERR_OK) {
      return e;
    }
    if (vec_slice_uint8_t_len(&tokens) != 1) {
      return HTTP_CLIENT_ERR_TRANSFER_ENCODING_UNSUPPORTED;
    }
    slice_uint8_t p = *vec_slice_uint8_t_at(&tokens, 0);
    if (!case_insensitive_buf_str_eq(p.data, p.len, "chunked")) {
      return HTTP_CLIENT_ERR_TRANSFER_ENCODING_UNSUPPORTED;
    }
    headers->transfer_encoding_chunked = true;
  }

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

typedef enum {
  CHUNK_SIZE,
  CHUNK_EXT_WS1,
  CHUNK_EXT,
  CHUNK_SIZE_LF,
  CHUNK_DATA,
  CHUNK_DATA_CR,
  CHUNK_DATA_LF,
  TRAILER,
  TRAILER_LF,
  DONE
} ChunkState;

typedef struct {
  ChunkState state;
  size_t chunk_size;
  size_t remaining_data;
  bool line_empty;
} ChunkParser;

static HttpClientError parse_chunk(
  ChunkParser *p,
  const uint8_t *buf,
  size_t buflen,
  void *context,
  HttpCallbackError (*callback)(void *context, const uint8_t *buf, size_t buflen)
) {
  for (size_t i = 0; i < buflen;) {
    switch (p->state) {
      case CHUNK_SIZE: {
        size_t digit = 0;
        ChunkState next_state = CHUNK_SIZE;
        switch (buf[i]) {
          case '0' ... '9':
            digit = buf[i] - '0';
            break;
          case 'a' ... 'f':
            digit = buf[i] - 'a' + 10;
            break;
          case 'A' ... 'F':
            digit = buf[i] - 'A' + 10;
            break;
          case '\r':
            next_state = CHUNK_SIZE_LF;
            break;
          case ' ':
          case '\t':
            next_state = CHUNK_EXT_WS1;
            break;
          case ';':
            next_state = CHUNK_EXT;
            break;
          default:
            return HTTP_CLIENT_ERR_RESPONSE_MALFORMED;
        }
        if (next_state == CHUNK_SIZE) {
          if (p->chunk_size > (SIZE_MAX - digit) / 16) {
            // it overflowed
            return HTTP_CLIENT_ERR_RESPONSE_MALFORMED;
          }
          p->chunk_size = p->chunk_size * 16 + digit;
          p->line_empty = false;
        } else {
          if (p->line_empty) {
            return HTTP_CLIENT_ERR_RESPONSE_MALFORMED;
          }
          p->state = next_state;
        }
        i++;
        break;
      }
      case CHUNK_EXT_WS1: {
        switch (buf[i]) {
          case ';':
            p->state = CHUNK_EXT;
            break;
          case ' ':
          case '\t':
            break;
          default:
            return HTTP_CLIENT_ERR_RESPONSE_MALFORMED;
        }
        i++;
        break;
      }
      case CHUNK_EXT:
        // TODO: properly parse the chunk flags
        switch (buf[i]) {
          case '\r':
            p->state = CHUNK_SIZE_LF;
            break;
          case '\n':
            return HTTP_CLIENT_ERR_RESPONSE_MALFORMED;
          default:
            break;
        }
        i++;
        break;

      case CHUNK_SIZE_LF:
        if (buf[i] == '\n') {
          if (p->chunk_size == 0) {
            p->state = TRAILER;
            p->line_empty = true;
          } else {
            p->state = CHUNK_DATA;
            p->remaining_data = p->chunk_size;
          }
        } else {
          return HTTP_CLIENT_ERR_RESPONSE_MALFORMED;
        }
        i++;
        break;
      case CHUNK_DATA: {
        size_t available = buflen - i;
        size_t n = p->remaining_data < available ? p->remaining_data : available;
        if (n != 0) {
          HttpCallbackError e = callback(context, buf + i, n);
          if (e != HTTP_CALLBACK_ERR_OK) {
            return convert_callback_err(e);
          }
          i += n;
          p->remaining_data -= n;
        }
        if (p->remaining_data == 0) {
          p->state = CHUNK_DATA_CR;
        }
        break;
      }
      case CHUNK_DATA_CR:
        if (buf[i] == '\r') {
          p->state = CHUNK_DATA_LF;
        } else {
          return HTTP_CLIENT_ERR_RESPONSE_MALFORMED;
        }
        i++;
        break;
      case CHUNK_DATA_LF:
        if (buf[i] == '\n') {
          p->state = CHUNK_SIZE;
          p->line_empty = true;
          p->chunk_size = 0;
        } else {
          return HTTP_CLIENT_ERR_RESPONSE_MALFORMED;
        }
        i++;
        break;
      case TRAILER:
        switch (buf[i]) {
          case '\r':
            p->state = TRAILER_LF;
            break;
          default:
            p->line_empty = false;
            break;
        }
        i++;
        break;
      case TRAILER_LF:
        if (buf[i] == '\n') {
          if (p->line_empty) {
            p->state = DONE;
          } else {
            p->state = TRAILER;
            p->line_empty = true;
          }
        } else {
          return HTTP_CLIENT_ERR_RESPONSE_MALFORMED;
        }
        i++;
        break;
      case DONE:
        // do nothing
        i++;
        break;
    }
  }
  return HTTP_CLIENT_ERR_OK;
}

#define STREAMBUFSIZE 8192

static HttpClientError http_client_parse_body_chunked_encoding(
  Transport *transport,
  uint8_t *bodystart,
  size_t bodystartlen,
  void *context,
  HttpCallbackError (*callback)(void *context, const uint8_t *buf, size_t buflen)
) {
  ChunkParser p = {.line_empty = true};
  HttpClientError bse = parse_chunk(&p, bodystart, bodystartlen, context, callback);
  if (bse != HTTP_CLIENT_ERR_OK) {
    return bse;
  }
  if (p.state == DONE) {
    return HTTP_CLIENT_ERR_OK;
  }
  uint8_t buf[STREAMBUFSIZE] = {};
  while (true) {
    size_t received;
    TransportError te = transport_recv(transport, buf, sizeof(buf), &received);
    if (te != TRANSPORT_ERR_OK) {
      return convert_transport_error(te);
    }
    HttpClientError e = parse_chunk(&p, buf, received, context, callback);
    if (e != HTTP_CLIENT_ERR_OK) {
      return e;
    }
    if (received == 0 || p.state == DONE) {
      // no more
      break;
    }
  }

  if (p.state != DONE) {
    return HTTP_CLIENT_ERR_RESPONSE_TRUNCATED;
  }
  return HTTP_CLIENT_ERR_OK;
}

static HttpClientError http_client_parse_body_content_length(
  Transport *transport,
  uint8_t *bodystart,
  size_t bodystartlen,
  void *context,
  HttpCallbackError (*callback)(void *context, const uint8_t *buf, size_t buflen),
  bool has_content_length,
  uint64_t content_length
) {
  if (has_content_length && bodystartlen >= content_length) {
    // we might have all the data we need already
    return convert_callback_err(callback(context, bodystart, content_length));
  }

  // otherwise we need to keep reading
  HttpCallbackError bse = callback(context, bodystart, bodystartlen);
  if (bse != HTTP_CALLBACK_ERR_OK) {
    return convert_callback_err(bse);
  }
  uint64_t content_seen = bodystartlen;
  uint8_t buf[STREAMBUFSIZE] = {};
  while (!has_content_length || content_seen < content_length) {
    size_t buflen = has_content_length && content_length - content_seen < sizeof(buf)
      ? content_length - content_seen
      : sizeof(buf);
    size_t received;
    TransportError te = transport_recv(transport, buf, buflen, &received);
    if (te != TRANSPORT_ERR_OK) {
      return convert_transport_error(te);
    }
    HttpCallbackError e = callback(context, buf, received);
    if (e != HTTP_CALLBACK_ERR_OK) {
      return convert_callback_err(e);
    }
    content_seen += received;
    if (received == 0) {
      // no more
      break;
    }
  }
  if (has_content_length && content_seen < content_length) {
    return HTTP_CLIENT_ERR_RESPONSE_TRUNCATED;
  }
  return HTTP_CLIENT_ERR_OK;
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

  Transport transport;
  switch (protocol) {
    case HTTP_PROTOCOL_HTTP:
      transport = inner;
      break;
    case HTTP_PROTOCOL_HTTPS: {
      TransportError te = transport_wrap_tls(&transport, inner, host, anchors, strict_ssl);
      if (te != TRANSPORT_ERR_OK) {
        transport_close(&inner);
        return convert_transport_error(te);
      }
      break;
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

  // most practical implementations will reject over 64k so we do too
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
