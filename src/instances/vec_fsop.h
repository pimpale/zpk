#ifndef vec_fsop_h_INCLUDED
#define vec_fsop_h_INCLUDED

#include "fsop.h"

#define VEC_NAME fsop
#define VEC_DTYPE FsOp
#include <vec/vec.h>
#undef VEC_DTYPE
#undef VEC_NAME

void vec_fsop_delete_and_freeowned(vec_fsop *vec);

#endif // vec_fsop_h_INCLUDED
