#ifndef llrb_path_indexdata_h_INCLUDED
#define llrb_path_indexdata_h_INCLUDED

#include "instances/llrb_char_ptr_fileclaim.h"

// per-path index data. claims holds every package's claim on this path (one
// is the common case, more means packages share or conflict); path-specific
// fields that aren't per-claimant belong here alongside it
typedef struct {
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
