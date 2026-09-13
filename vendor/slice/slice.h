#ifndef SLICE_DTYPE
#error "SLICE_DTYPE must be defined before including slice.h"
#endif

#ifndef SLICE_NAME
#define SLICE_NAME SLICE_DTYPE
#endif

#include <stddef.h>
#include <stdint.h>

#define SLICE_PASTE_(a, b) a##b
#define SLICE_PASTE(a, b) SLICE_PASTE_(a, b)
#define SLICE_T SLICE_PASTE(slice_, SLICE_NAME)
#define SLICE_FN(suffix) SLICE_PASTE(SLICE_T, suffix)

typedef struct SLICE_T {
  SLICE_DTYPE *data;
  size_t len;
} SLICE_T;

typedef enum SliceError {
    SLICE_ERR_OK = 0,
    SLICE_ERR_OUT_OF_MEM
} SliceError;

// initialize a slice with a given length
SliceError SLICE_FN(_init)(SLICE_T* slice, size_t len);
// clone an existing slice, overwrite out
SliceError SLICE_FN(_dup)(SLICE_T* src, SLICE_T* out);


#undef SLICE_FN
#undef SLICE_T
#undef SLICE_PASTE
#undef SLICE_PASTE_