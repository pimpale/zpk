#ifndef llrb_path_indexdata_h_INCLUDED
#define llrb_path_indexdata_h_INCLUDED

#include "instances/llrb_char_ptr_fileclaim.h"

typedef struct {
  // Map<PackagePath, FileClaim>
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
