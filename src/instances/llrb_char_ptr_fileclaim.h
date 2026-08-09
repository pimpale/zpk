#ifndef llrb_path_fileclaim_h_INCLUDED
#define llrb_path_fileclaim_h_INCLUDED

#include <stdbool.h>
#include <stdint.h>

#include "instances/vec_char_ptr.h"

typedef struct {
  uint32_t file_index; // entry's index in the zip; meaningless for implied
                       // parent directories
  uint32_t crc32;      // 0 for directories
  bool is_directory;
} FileClaim;

#define LLRB_NAME char_ptr_fileclaim
#define LLRB_KEY char_ptr
#define LLRB_VALUE FileClaim
#include <llrb/llrb.h>
#undef LLRB_VALUE
#undef LLRB_KEY
#undef LLRB_NAME

#endif // llrb_path_fileclaim_h_INCLUDED
