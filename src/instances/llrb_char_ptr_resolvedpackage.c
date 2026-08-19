#include "instances/llrb_char_ptr_resolvedpackage.h"
#include "resolvedpackage.h"
#include <string.h>

#define LLRB_NAME char_ptr_resolvedpackage
#define LLRB_KEY char_ptr
#define LLRB_VALUE ResolvedPackage
#define LLRB_COMPARE(a, b) strcmp(*(a), *(b))
#include <llrb/llrb_impl.h>
#undef LLRB_COMPARE
#undef LLRB_VALUE
#undef LLRB_KEY
#undef LLRB_NAME

void llrb_char_ptr_resolvedpackage_delete_and_freeowned(
    llrb_char_ptr_resolvedpackage *map) {
  llrb_char_ptr_resolvedpackage_iter iter;
  llrb_char_ptr_resolvedpackage_iter_begin(map, &iter);
  char *key;
  ResolvedPackage value;
  while (llrb_char_ptr_resolvedpackage_iter_next(&iter, &key, &value)) {
    free(key);
    delete_ResolvedPackage(&value);
  }
  llrb_char_ptr_resolvedpackage_delete(map);
}
