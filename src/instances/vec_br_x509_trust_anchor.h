#ifndef vec_br_x509_trust_anchor_h_INCLUDED
#define vec_br_x509_trust_anchor_h_INCLUDED

#include "trust_anchor.h"

#define VEC_DTYPE br_x509_trust_anchor
#include <vec/vec.h>
#undef VEC_DTYPE

void vec_br_x509_trust_anchor_delete_and_freeowned(vec_br_x509_trust_anchor* vec);

#endif
