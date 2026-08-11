#ifndef vec_fsop_h_INCLUDED
#define vec_fsop_h_INCLUDED

#include "fsop.h"

#define VEC_DTYPE fsop_t
#include <vec/vec.h>
#undef VEC_DTYPE

void vec_fsop_t_delete_and_freeowned(vec_fsop_t *vec);

#endif // vec_fsop_h_INCLUDED
