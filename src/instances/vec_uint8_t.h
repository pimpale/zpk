#ifndef vec_char_h_INCLUDED
#define vec_char_h_INCLUDED

#include <stdint.h>
#include "slice_uint8_t.h"

#define VEC_DTYPE uint8_t
#include <vec/vec.h>
#undef VEC_DTYPE

// aliases the old struct
vec_uint8_t vec_uint8_t_from_slice(slice_uint8_t slice);

#endif // vec_char_h_INCLUDED
