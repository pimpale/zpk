#ifndef vec_slice_uint8_t_h_INCLUDED
#define vec_slice_uint8_t_h_INCLUDED

#include "instances/slice_uint8_t.h"

#define VEC_DTYPE slice_uint8_t
#include <vec/vec.h>
#undef VEC_DTYPE

// only use if owned
void vec_slice_uint8_t_clear_and_freeowned(vec_slice_uint8_t *vec);
void vec_slice_uint8_t_delete_and_freeowned(vec_slice_uint8_t *vec);

#endif // vec_slice_uint8_t_h_INCLUDED
