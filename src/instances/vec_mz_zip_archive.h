#ifndef vec_mz_zip_archive_h_INCLUDED
#define vec_mz_zip_archive_h_INCLUDED

#include "miniz/miniz.h"

#define VEC_DTYPE mz_zip_archive
#include <vec/vec.h>
#undef VEC_DTYPE

void vec_mz_zip_archive_delete_and_freeowned(vec_mz_zip_archive *vec);

#endif // vec_mz_zip_archive_h_INCLUDED
