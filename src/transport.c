#include "transport.h"
#include "tcpcompatlayer.h"
#include "tcpcompatlayer_error.h"
#include "tlsconfig.h"

#include <assert.h>
#include <stdlib.h>

#include <bearssl/inc/bearssl.h>

struct tls_data {
  br_ssl_client_context client;
  br_x509_minimal_context x509;
  br_sslio_context io;

  unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];

  TcpSocket *socket;
  TcpError last_tcp_error;
  bool tcp_eof;
};

static TransportError convert_tcp_error(TcpError e) {
  switch (e) {
    case TCP_ERR_OK:
      return TRANSPORT_ERR_OK;
    case TCP_ERR_INVALID_ARGUMENT:
      return TRANSPORT_ERR_TLS_PROTOCOL;
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
      return TRANSPORT_ERR_CONNECTION_REFUSED;
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

  /*
   * The br_sslio callbacks collapse socket errors into BR_ERR_IO,
   * so preserve the more specific TCP error.
   */
  if (tls->last_tcp_error != TCP_ERR_OK) {
    return convert_tcp_error(tls->last_tcp_error);
  }

  /*
   * EOF while BearSSL was still waiting for a TLS record means the
   * peer closed TCP without completing TLS shutdown.
   */
  if (tls->tcp_eof) {
    return TRANSPORT_ERR_TLS_TRUNCATED;
  }

  int error = br_ssl_engine_last_error(&tls->client.eng);

  switch (error) {
    case BR_ERR_OK:
      /*
       * The engine can be closed without an error after receiving
       * close_notify. Callers should interpret this as clean EOF.
       */
      return TRANSPORT_ERR_OK;

    case BR_ERR_BAD_PARAM:
      return TRANSPORT_ERR_INVALID_ARGUMENT;

    case BR_ERR_IO:
      /*
       * Normally a callback will have recorded the real cause above.
       * This is the fallback for an otherwise unexplained I/O failure.
       */
      return TRANSPORT_ERR_IO;

    case BR_ERR_X509_BAD_SERVER_NAME:
      return TRANSPORT_ERR_TLS_HOSTNAME_MISMATCH;
  }

  /*
   * BearSSL reserves 32..63 for X.509 errors. BR_ERR_X509_OK is 32,
   * so actual certificate failures begin above it.
   */
  if (error > BR_ERR_X509_OK && error < 64) {
    return TRANSPORT_ERR_TLS_CERTIFICATE;
  }

  /*
   * Fatal alert codes consist of a base plus an 8-bit TLS alert value:
   *   256..511: received fatal alert
   *   512..767: sent fatal alert
   */
  if (error >= BR_ERR_RECV_FATAL_ALERT && error < BR_ERR_SEND_FATAL_ALERT + 256) {
    return TRANSPORT_ERR_TLS_ALERT;
  }

  /*
   * Bad records, unsupported versions, handshake failures, bad MACs,
   * unsupported algorithms, and similar engine failures.
   */
  return TRANSPORT_ERR_TLS_PROTOCOL;
}

static int tls_low_read(void *context, unsigned char *buf, size_t length) {
  TlsData *tls = context;
  size_t received;

  TcpError err = tcp_recv_portable(tls->socket, &received, buf, length);

  if (err != TCP_ERR_OK) {
    tls->last_tcp_error = err;
    return -1;
  }

  if (received == 0) {
    tls->tcp_eof = true;
    return -1;
  }

  return (int)received;
}

static int tls_low_write(void *context, const unsigned char *buf, size_t length) {
  TlsData *tls = context;
  size_t sent;

  TcpError err = tcp_send_portable(tls->socket, &sent, buf, length);

  if (err != TCP_ERR_OK) {
    tls->last_tcp_error = err;
    return -1;
  }

  return (int)sent;
}

static TransportError
tls_connect(TlsData *tls, const char *hostname, const char *port, TlsConfig *tlsconfig) {
  TcpError tce = tcp_connect_portable(&tls->socket, hostname, port);
  if (tce != TCP_ERR_OK) {
    return convert_tcp_error(tce);
  }
  tls->last_tcp_error = TCP_ERR_OK;
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

  if (!br_ssl_client_reset(&tls->client, hostname, 0)) {
    return convert_tls_failure(tls);
  }

  if (br_sslio_flush(&tls->io) < 0) {
    return convert_tls_failure(tls);
  }

  return TRANSPORT_ERR_OK;
}

TransportError transport_connect(
  Transport *transport,
  const char *hostname,
  const char *port,
  bool use_tls,
  TlsConfig *tlsconfig
) {
  if (use_tls) {
    transport->tls = malloc(sizeof(TlsData));
    if (transport->tls == NULL) {
      return TRANSPORT_ERR_OUT_OF_MEMORY;
    }
    TransportError e = tls_connect(transport->tls, hostname, port, tlsconfig);
    if (e != TRANSPORT_ERR_OK) {
      free(transport->tls);
    }
    return e;
  } else {
    return convert_tcp_error(tcp_connect_portable(&transport->socket, hostname, port));
  }
}

TransportError transport_send(Transport *transport, const char *data, size_t length);

TransportError transport_recv(Transport *transport, char *buffer, size_t length, size_t *received);

void transport_close(Transport *transport);
