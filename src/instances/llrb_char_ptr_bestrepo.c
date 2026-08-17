#include "instances/llrb_char_ptr_bestrepo.h"
#include <string.h>

#define LLRB_NAME char_ptr_bestrepo
#define LLRB_KEY char_ptr
#define LLRB_VALUE BestRepo
#define LLRB_COMPARE(a, b) strcmp(*(a), *(b))
#include <llrb/llrb_impl.h>
#undef LLRB_COMPARE
#undef LLRB_VALUE
#undef LLRB_KEY
#undef LLRB_NAME

void llrb_char_ptr_bestrepo_delete_and_freeowned(llrb_char_ptr_bestrepo *map) {
  llrb_char_ptr_bestrepo_iter iter;
  llrb_char_ptr_bestrepo_iter_begin(map, &iter);
  char *key;
  BestRepo value;
  while (llrb_char_ptr_bestrepo_iter_next(&iter, &key, &value)) {
    free(key);
    free(value.version);
  }
  llrb_char_ptr_bestrepo_delete(map);
}
