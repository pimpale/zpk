#ifndef vec_mz_zip_archive_h_INCLUDED
#define vec_mz_zip_archive_h_INCLUDED

#include "miniz/miniz.h"

typedef mz_zip_archive* mz_zip_archive_ptr;

#define VEC_DTYPE mz_zip_archive_ptr
#include <vec/vec.h>
#undef VEC_DTYPE

void vec_mz_zip_archive_ptr_delete_and_freeowned(vec_mz_zip_archive_ptr *vec);

#endif // vec_mz_zip_archive_h_INCLUDED
