#include "instances/llrb_slice_uint8_t_packagedata.h"
#include "instances/slice_uint8_t.h"

#include <string.h>

#define LLRB_NAME slice_uint8_t_packagedata
#define LLRB_KEY slice_uint8_t
#define LLRB_VALUE PackageData
#define LLRB_COMPARE(a, b) slice_uint8_t_cmp((a), (b))
#include <llrb/llrb_impl.h>
#undef LLRB_COMPARE
#undef LLRB_VALUE
#undef LLRB_KEY
#undef LLRB_NAME

void llrb_slice_uint8_t_packagedata_clear_and_freeowned(
    llrb_slice_uint8_t_packagedata *set) {
  llrb_slice_uint8_t_packagedata_iter iter;
  llrb_slice_uint8_t_packagedata_iter_begin(set, &iter);
  slice_uint8_t key;
  while (llrb_slice_uint8_t_packagedata_iter_next(&iter, &key, NULL)) {
    free(key.data);
  }
  llrb_slice_uint8_t_packagedata_clear(set);
}

void llrb_slice_uint8_t_packagedata_delete_and_freeowned(
    llrb_slice_uint8_t_packagedata *set) {
  llrb_slice_uint8_t_packagedata_iter iter;
  llrb_slice_uint8_t_packagedata_iter_begin(set, &iter);
  slice_uint8_t key;
  while (llrb_slice_uint8_t_packagedata_iter_next(&iter, &key, NULL)) {
    free(key.data);
  }
  llrb_slice_uint8_t_packagedata_delete(set);
}
