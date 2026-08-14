#include "instances/llrb_char_ptr_packagedata.h"

#include <string.h>

#define LLRB_NAME char_ptr_packagedata
#define LLRB_KEY char_ptr
#define LLRB_VALUE PackageData
#define LLRB_COMPARE(a, b) strcmp(*(a), *(b))
#include <llrb/llrb_impl.h>
#undef LLRB_COMPARE
#undef LLRB_VALUE
#undef LLRB_KEY
#undef LLRB_NAME

void llrb_char_ptr_packagedata_clear_and_freeowned(
    llrb_char_ptr_packagedata *set) {
  llrb_char_ptr_packagedata_iter iter;
  llrb_char_ptr_packagedata_iter_begin(set, &iter);
  char *key;
  while (llrb_char_ptr_packagedata_iter_next(&iter, &key, NULL)) {
    free(key);
  }
  llrb_char_ptr_packagedata_clear(set);
}

void llrb_char_ptr_packagedata_delete_and_freeowned(
    llrb_char_ptr_packagedata *set) {
  llrb_char_ptr_packagedata_iter iter;
  llrb_char_ptr_packagedata_iter_begin(set, &iter);
  char *key;
  while (llrb_char_ptr_packagedata_iter_next(&iter, &key, NULL)) {
    free(key);
  }
  llrb_char_ptr_packagedata_delete(set);
}
