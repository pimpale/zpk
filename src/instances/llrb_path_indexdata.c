#include <stdbool.h>
#include <string.h>
#include "instances/llrb_path_indexdata.h"

#define LLRB_NAME path_indexdata
#define LLRB_KEY char_ptr
#define LLRB_VALUE IndexData
#define LLRB_COMPARE(a, b) strcmp(*(a), *(b))
#include <llrb/llrb_impl.h>
#undef LLRB_COMPARE
#undef LLRB_VALUE
#undef LLRB_KEY
#undef LLRB_NAME
