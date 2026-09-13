#include "instances/vec_br_x509_trust_anchor.h"
#include "trust_anchor.h"

#define VEC_DTYPE br_x509_trust_anchor
#include <vec/vec_impl.h>
#undef VEC_DTYPE

void vec_br_x509_trust_anchor_delete_and_freeowned(vec_br_x509_trust_anchor* vec) {
  for(size_t i = 0; i < vec_br_x509_trust_anchor_len(vec); i++ ) {
    free_trust_anchor(vec_br_x509_trust_anchor_at(vec, i));
  }
  vec_br_x509_trust_anchor_delete(vec);
}
