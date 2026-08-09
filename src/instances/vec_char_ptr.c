#include "instances/vec_char_ptr.h"

#define VEC_DTYPE char_ptr
#include <vec/vec_impl.h>
#undef VEC_DTYPE

void vec_char_ptr_clear_and_freeowned(vec_char_ptr *vec) {
  for (uint32_t i = 0; i < vec_char_ptr_len(vec); i++) {
    free(*vec_char_ptr_at(vec, i));
  }
  vec_char_ptr_clear(vec);
}

void vec_char_ptr_delete_and_freeowned(vec_char_ptr *vec) {
  for (uint32_t i = 0; i < vec_char_ptr_len(vec); i++) {
    free(*vec_char_ptr_at(vec, i));
  }
  vec_char_ptr_delete(vec);
}
