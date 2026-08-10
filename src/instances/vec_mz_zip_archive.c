#include "instances/vec_mz_zip_archive.h"

#define VEC_DTYPE mz_zip_archive
#include <vec/vec_impl.h>
#undef VEC_DTYPE

void vec_mz_zip_archive_delete_and_freeowned(vec_mz_zip_archive *vec) {
  for (uint32_t i = 0; i < vec_mz_zip_archive_len(vec); i++) {
    mz_zip_reader_end(vec_mz_zip_archive_at(vec, i));
  }
  vec_mz_zip_archive_delete(vec);
}
