#include <stdbool.h>
#include <string.h>
#include "instances/llrb_path_filestatus.h"

#define LLRB_NAME path_filestatus
#define LLRB_KEY char_ptr
#define LLRB_VALUE FileStatus
#define LLRB_COMPARE(a, b) strcmp(*(a), *(b))
#include <llrb/llrb_impl.h>
#undef LLRB_COMPARE
#undef LLRB_VALUE
#undef LLRB_KEY
#undef LLRB_NAME

void llrb_path_filestatus_delete_and_freeowned(
    llrb_path_filestatus *map) {
  llrb_path_filestatus_iter iter;
  llrb_path_filestatus_iter_begin(map, &iter);
  char *key;
  while (llrb_path_filestatus_iter_next(&iter, &key, NULL)) {
    free(key);
  }
  llrb_path_filestatus_delete(map);
}
