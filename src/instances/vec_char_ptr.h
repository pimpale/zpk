#ifndef vec_char_ptr_h_INCLUDED
#define vec_char_ptr_h_INCLUDED

typedef char *char_ptr;
#define VEC_DTYPE char_ptr
#include <vec/vec.h>
#undef VEC_DTYPE

#endif // vec_char_ptr_h_INCLUDED