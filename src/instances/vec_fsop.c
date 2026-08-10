#include "instances/vec_fsop.h"
#include "fsop.h"

#define VEC_DTYPE fsop
#include <vec/vec_impl.h>
#undef VEC_DTYPE

void vec_fsop_delete_and_freeowned(vec_fsop *vec) {
  for (uint32_t i = 0; i < vec_fsop_len(vec); i++) {
    delete_fsop(vec_fsop_at(vec, i));
  }
  vec_fsop_delete(vec);
}
