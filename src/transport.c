#include "transport.h"
#include "tcpcompatlayer.h"
#include "tcpcompatlayer_error.h"
#include "tlsconfig.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct tls_data {
  Transport lower;

  br_ssl_client_context client;
  br_x509_minimal_context x509;
  br_sslio_context io;
  uint8_t iobuf[BR_SSL_BUFSIZE_BIDI];

  TransportError last_lower_error;
  bool lower_eof;
};

static TransportError convert_tcp_error(TcpError error) {
  switch (error) {
    case TCP_ERR_OK:
      return TRANSPORT_ERR_OK;
    case TCP_ERR_INVALID_ARGUMENT:
      return TRANSPORT_ERR_INVALID_ARGUMENT;
    case TCP_ERR_OUT_OF_MEMORY:
      return TRANSPORT_ERR_OUT_OF_MEMORY;
    case TCP_ERR_HOST_NOT_FOUND:
      return TRANSPORT_ERR_HOST_NOT_FOUND;
    case TCP_ERR_DNS_TEMPORARY:
      return TRANSPORT_ERR_DNS_TEMPORARY;
    case TCP_ERR_DNS_FAILURE:
      return TRANSPORT_ERR_DNS_FAILURE;
    case TCP_ERR_CONNECTION_REFUSED:
      return TRANSPORT_ERR_CONNECTION_REFUSED;
    case TCP_ERR_CONNECTION_RESET:
      return TRANSPORT_ERR_CONNECTION_RESET;
    case TCP_ERR_CONNECTION_CLOSED:
      return TRANSPORT_ERR_CONNECTION_CLOSED;
    case TCP_ERR_HOST_UNREACHABLE:
      return TRANSPORT_ERR_HOST_UNREACHABLE;
    case TCP_ERR_NETWORK_UNREACHABLE:
      return TRANSPORT_ERR_NETWORK_UNREACHABLE;
    case TCP_ERR_TIMED_OUT:
      return TRANSPORT_ERR_TIMED_OUT;
    case TCP_ERR_NOT_INITIALIZED:
      return TRANSPORT_ERR_TCP_NOT_INITIALIZED;
    case TCP_ERR_ADDRESS_UNSUPPORTED:
      return TRANSPORT_ERR_TCP_ADDRESS_UNSUPPORTED;
    case TCP_ERR_IO:
      return TRANSPORT_ERR_IO;
    case TCP_ERR_UNKNOWN:
      return TRANSPORT_ERR_UNKNOWN;
  }
}

static TransportError convert_tls_failure(const TlsData *tls) {
  assert(tls != NULL);

  /* br_sslio collapses lower-layer failures into BR_ERR_IO. */
  if (tls->last_lower_error != TRANSPORT_ERR_OK) {
    return tls->last_lower_error;
  }

  /* EOF before a TLS close_notify is a truncated TLS stream. */
  if (tls->lower_eof) {
    return TRANSPORT_ERR_TLS_TRUNCATED;
  }

  int error = br_ssl_engine_last_error(&tls->client.eng);
  switch (error) {
    case BR_ERR_OK:
      /* A closed engine with no error represents a clean close_notify. */
      return TRANSPORT_ERR_OK;
    case BR_ERR_BAD_PARAM:
      return TRANSPORT_ERR_INVALID_ARGUMENT;
    case BR_ERR_IO:
      return TRANSPORT_ERR_IO;
    case BR_ERR_X509_BAD_SERVER_NAME:
      return TRANSPORT_ERR_TLS_HOSTNAME_MISMATCH;
  }

  /* BearSSL reserves 32 through 63 for X.509 status codes. */
  if (error > BR_ERR_X509_OK && error < 64) {
    return TRANSPORT_ERR_TLS_CERTIFICATE;
  }

  /* Fatal alert values are a direction-specific base plus an 8-bit code. */
  if (error >= BR_ERR_RECV_FATAL_ALERT && error < BR_ERR_SEND_FATAL_ALERT + 256) {
    return TRANSPORT_ERR_TLS_ALERT;
  }

  return TRANSPORT_ERR_TLS_PROTOCOL;
}

static int tls_low_read(void *context, uint8_t *buf, size_t length) {
  TlsData *tls = context;
  size_t received = 0;

  TransportError error = transport_recv(&tls->lower, buf, length, &received);
  if (error != TRANSPORT_ERR_OK) {
    tls->last_lower_error = error;
    return -1;
  }

  if (received == 0) {
    tls->lower_eof = true;
    return -1;
  }

  return (int)received;
}

static int tls_low_write(void *context, const uint8_t *buf, size_t length) {
  TlsData *tls = context;

  TransportError error = transport_send(&tls->lower, buf, length);
  if (error != TRANSPORT_ERR_OK) {
    tls->last_lower_error = error;
    return -1;
  }

  return (int)length;
}

static TransportError tls_initialize(TlsData *tls, const char *host, TlsConfig *tlsconfig) {
  tls->last_lower_error = TRANSPORT_ERR_OK;
  tls->lower_eof = false;
  memset(tls->iobuf, 0, sizeof(tls->iobuf));

  br_ssl_client_init_full(
    &tls->client,
    &tls->x509,
    tlsconfig->trust_anchors,
    tlsconfig->trust_anchor_count
  );
  br_ssl_engine_set_buffer(&tls->client.eng, tls->iobuf, sizeof(tls->iobuf), 1);
  br_ssl_engine_set_versions(&tls->client.eng, BR_TLS12, BR_TLS12);
  br_sslio_init(&tls->io, &tls->client.eng, tls_low_read, tls, tls_low_write, tls);

  if (!br_ssl_client_reset(&tls->client, host, 0)) {
    return convert_tls_failure(tls);
  }
  if (br_sslio_flush(&tls->io) < 0) {
    return convert_tls_failure(tls);
  }
  return TRANSPORT_ERR_OK;
}

TransportError transport_from_tcp(Transport *transport, TcpSocket *socket) {
  transport->use_tls = false;
  transport->tcp = socket;
  return TRANSPORT_ERR_OK;
}

TransportError
transport_wrap_tls(Transport *transport, Transport inner, const char *host, TlsConfig *tlsconfig) {
  TlsData *tls = malloc(sizeof(*tls));
  if (tls == NULL) {
    return TRANSPORT_ERR_OUT_OF_MEMORY;
  }

  tls->lower = inner;
  TransportError error = tls_initialize(tls, host, tlsconfig);
  if (error != TRANSPORT_ERR_OK) {
    free(tls);
    return error;
  }

  transport->use_tls = true;
  transport->tls = tls;
  return TRANSPORT_ERR_OK;
}

TransportError transport_send(Transport *transport, const uint8_t *buf, size_t buflen) {
  if (buflen == 0) {
    return TRANSPORT_ERR_OK;
  }

  if (transport->use_tls) {
    size_t sent = 0;
    while (sent < buflen) {
      int result = br_sslio_write(&transport->tls->io, buf + sent, buflen - sent);
      if (result < 0) {
        return convert_tls_failure(transport->tls);
      }
      if (result == 0) {
        return TRANSPORT_ERR_CONNECTION_CLOSED;
      }
      sent += (size_t)result;
    }

    if (br_sslio_flush(&transport->tls->io) < 0) {
      return convert_tls_failure(transport->tls);
    }
    return TRANSPORT_ERR_OK;
  } else {
    size_t sent = 0;
    while (sent < buflen) {
      size_t sent_now = 0;
      TransportError error =
        convert_tcp_error(tcp_send_portable(transport->tcp, &sent_now, buf + sent, buflen - sent));
      if (error != TRANSPORT_ERR_OK) {
        return error;
      }
      if (sent_now == 0) {
        return TRANSPORT_ERR_CONNECTION_CLOSED;
      }
      sent += sent_now;
    }
    return TRANSPORT_ERR_OK;
  }
}

TransportError
transport_recv(Transport *transport, uint8_t *buf, size_t buflen, size_t *received) {
  *received = 0;
  if (buflen == 0) {
    return TRANSPORT_ERR_OK;
  }
  if (transport->use_tls) {
    int result = br_sslio_read(&transport->tls->io, buf, buflen);
    if (result < 0) {
      return convert_tls_failure(transport->tls);
    }
    *received = (size_t)result;
    return TRANSPORT_ERR_OK;
  } else {
    return convert_tcp_error(tcp_recv_portable(transport->tcp, received, buf, buflen));
  }
}

void transport_close(Transport *transport) {
  if (transport == NULL) {
    return;
  }

  if (transport->use_tls) {
    TlsData *tls = transport->tls;
    if (tls != NULL) {
      transport_close(&tls->lower);
      free(tls);
    }
  } else if (transport->tcp != NULL) {
    tcp_close_portable(transport->tcp);
  }

  transport->use_tls = false;
  transport->tcp = NULL;
}
