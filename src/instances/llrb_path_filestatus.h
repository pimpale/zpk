
#ifndef llrb_path_filestatus_h_INCLUDED
#define llrb_path_filestatus_h_INCLUDED

#include <stdint.h>

typedef struct {
  bool exists;
  bool changed_during_transaction;
  // these 2 are only defined if it actually exists
  bool is_directory;
  uint32_t crc32; // only defined if is_directory == false
} FileStatus;

typedef char *char_ptr;

#define LLRB_NAME path_filestatus
#define LLRB_KEY char_ptr
#define LLRB_VALUE FileStatus
#include <llrb/llrb.h>
#undef LLRB_VALUE
#undef LLRB_KEY
#undef LLRB_NAME

void llrb_path_filestatus_delete_and_freeowned(llrb_path_filestatus *map);

#endif // llrb_path_filestatus_h_INCLUDED
