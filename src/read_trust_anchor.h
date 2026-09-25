#ifndef read_trust_anchor_h_INCLUDED
#define read_trust_anchor_h_INCLUDED

#include "instances/slice_uint8_t.h"
#include "instances/vec_br_x509_trust_anchor.h"
#include <bearssl/inc/bearssl_x509.h>
#include <stddef.h>

typedef enum {
  READ_TRUST_ANCHOR_ERR_OK = 0,
  READ_TRUST_ANCHOR_NOTFOUND,
  READ_TRUST_ANCHOR_ERR_INVALID_ARGUMENT,
  READ_TRUST_ANCHOR_ERR_IO,
  READ_TRUST_ANCHOR_ERR_OUT_OF_MEMORY,
  READ_TRUST_ANCHOR_ERR_PEM,
  READ_TRUST_ANCHOR_ERR_CERTIFICATE,
  READ_TRUST_ANCHOR_ERR_NO_CERTIFICATES,
  READ_TRUST_ANCHOR_ERR_VERIFICATION_UNSUPPORTED
} ReadTrustAnchorError;

const char* readtrustanchor_strerror(ReadTrustAnchorError e); 

ReadTrustAnchorError read_trust_anchors(vec_br_x509_trust_anchor *anchors, slice_uint8_t path);

#endif // read_trust_anchor_h_INCLUDED
