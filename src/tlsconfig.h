#ifndef tlsconfig_h_INCLUDED
#define tlsconfig_h_INCLUDED

#include <bearssl/inc/bearssl_x509.h>
#include <stddef.h>

typedef struct {
  br_x509_trust_anchor *trust_anchors;
  size_t trust_anchor_count;
} TlsConfig;

#endif // tlsconfig_h_INCLUDED
