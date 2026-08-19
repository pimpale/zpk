#include "instances/llrbset_char_ptr.h"
#include <string.h>

#define LLRBSET_NAME char_ptr
#define LLRBSET_KEY char_ptr
#define LLRBSET_COMPARE(a, b) strcmp(*(a), *(b))
#include <llrbset/llrbset_impl.h>
#undef LLRBSET_COMPARE
#undef LLRBSET_KEY
#undef LLRBSET_NAME

void llrbset_char_ptr_delete_and_freeowned(llrbset_char_ptr *set) {
  llrbset_char_ptr_iter iter;
  llrbset_char_ptr_iter_begin(set, &iter);
  char *key;
  while (llrbset_char_ptr_iter_next(&iter, &key)) {
    free(key);
  }
  llrbset_char_ptr_delete(set);
}
