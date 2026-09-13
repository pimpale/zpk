#ifndef owned_br_x509_trust_anchor_h_INCLUDED
#define owned_br_x509_trust_anchor_h_INCLUDED

#include <bearssl/inc/bearssl_x509.h>

void free_trust_anchor(br_x509_trust_anchor* anchor);

#endif // owned_br_x509_trust_anchor_h_INCLUDED