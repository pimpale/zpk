#include "instances/vec_fsop_t.h"

#define VEC_DTYPE fsop_t
#include <vec/vec_impl.h>
#undef VEC_DTYPE

void vec_fsop_t_delete_and_freeowned(vec_fsop_t *vec) {
  for (uint32_t i = 0; i < vec_fsop_t_len(vec); i++) {
    delete_fsop(vec_fsop_t_at(vec, i));
  }
  vec_fsop_t_delete(vec);
}
