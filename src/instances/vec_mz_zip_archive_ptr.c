#include "instances/vec_mz_zip_archive_ptr.h"
#include "miniz/miniz.h"

#define VEC_DTYPE mz_zip_archive_ptr
#include <vec/vec_impl.h>
#undef VEC_DTYPE

void vec_mz_zip_archive_ptr_delete_and_freeowned(vec_mz_zip_archive_ptr *vec) {
  for (uint32_t i = 0; i < vec_mz_zip_archive_ptr_len(vec); i++) {
    mz_zip_archive* pZip = *vec_mz_zip_archive_ptr_at(vec, i);
    mz_zip_reader_end(pZip);
    free(pZip);
  }
  vec_mz_zip_archive_ptr_delete(vec);
}
