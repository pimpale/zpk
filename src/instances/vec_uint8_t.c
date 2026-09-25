#include "instances/vec_uint8_t.h"

#define VEC_DTYPE uint8_t
#include <vec/vec_impl.h>
#undef VEC_DTYPE

vec_uint8_t vec_uint8_t_from_slice(slice_uint8_t slice) {
  vec_uint8_t out = {
    .pData = slice.data,
    .cap = slice.len,
    .len = slice.len
  }
}
