#ifndef llrb_path_resolvedpackage_h_INCLUDED
#define llrb_path_resolvedpackage_h_INCLUDED

#include "resolvedpackage.h"

typedef char* char_ptr;

#define LLRB_NAME char_ptr_resolvedpackage
#define LLRB_KEY char_ptr
#define LLRB_VALUE ResolvedPackage
#include <llrb/llrb.h>
#undef LLRB_VALUE
#undef LLRB_KEY
#undef LLRB_NAME


void llrb_char_ptr_resolvedpackage_delete_and_freeowned(llrb_char_ptr_resolvedpackage *map);

#endif // llrb_path_resolvedpackage_h_INCLUDED
