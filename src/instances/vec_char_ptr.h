#ifndef vec_char_ptr_h_INCLUDED
#define vec_char_ptr_h_INCLUDED

typedef char *char_ptr;
#define VEC_DTYPE char_ptr
#include <vec/vec.h>
#undef VEC_DTYPE

// free all strings inside the vec too
void vec_char_ptr_clear_and_freeowned(vec_char_ptr *vec);
void vec_char_ptr_delete_and_freeowned(vec_char_ptr *vec);

#endif // vec_char_ptr_h_INCLUDED
