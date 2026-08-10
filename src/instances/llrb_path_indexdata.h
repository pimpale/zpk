#ifndef llrb_path_indexdata_h_INCLUDED
#define llrb_path_indexdata_h_INCLUDED

#include "instances/llrb_char_ptr_fileclaim.h"

typedef struct {
  bool exists;
  // these 2 are only defined if it actually exists
  bool is_directory; 
  uint32_t crc32; // only defined if is_directory == false
} FileStatus;


typedef struct {
  bool computed_actual;
  FileStatus actual;
  // Map<PackageName, FileClaim>
  llrb_char_ptr_fileclaim claims;
} IndexData;

#define LLRB_NAME path_indexdata
#define LLRB_KEY char_ptr
#define LLRB_VALUE IndexData
#include <llrb/llrb.h>
#undef LLRB_VALUE
#undef LLRB_KEY
#undef LLRB_NAME

#endif // llrb_path_indexdata_h_INCLUDED
