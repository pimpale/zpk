#include <string.h>
#include "instances/llrb_char_ptr_fileclaim.h"

#define LLRB_NAME char_ptr_fileclaim
#define LLRB_KEY char_ptr
#define LLRB_VALUE FileClaim
#define LLRB_COMPARE(a, b) strcmp(*(a), *(b))
#include <llrb/llrb_impl.h>
#undef LLRB_COMPARE
#undef LLRB_VALUE
#undef LLRB_KEY
#undef LLRB_NAME
