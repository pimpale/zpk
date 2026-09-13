#include "instances/vec_slice_uint8_t.h"

#include <stdlib.h>

#define VEC_DTYPE slice_uint8_t
#include <vec/vec_impl.h>
#undef VEC_DTYPE

void vec_slice_uint8_t_clear_and_freeowned(vec_slice_uint8_t *vec) {
  for (size_t i = 0; i < vec_slice_uint8_t_len(vec); i++) {
    free(vec_slice_uint8_t_at(vec, i)->data);
  }
  vec_slice_uint8_t_clear(vec);
}

void vec_slice_uint8_t_delete_and_freeowned(vec_slice_uint8_t *vec) {
  vec_slice_uint8_t_clear_and_freeowned(vec);
  vec_slice_uint8_t_delete(vec);
}
