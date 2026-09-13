#include "trust_anchor.h"
#include <stdlib.h>

void free_trust_anchor(br_x509_trust_anchor *anchor) {
  free(anchor->dn.data);
  switch (anchor->pkey.key_type) {
    case BR_KEYTYPE_RSA:
      free(anchor->pkey.key.rsa.n);
      free(anchor->pkey.key.rsa.e);
      break;
    case BR_KEYTYPE_EC:
      free(anchor->pkey.key.ec.q);
      break;
    default:
      break;
  }
}
