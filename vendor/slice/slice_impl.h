#include "instances/slice_uint8_t.h"
#ifndef SLICE_DTYPE
#error "SLICE_DTYPE must be defined before including slice_impl.h"
#endif

#ifndef SLICE_NAME
#define SLICE_NAME SLICE_DTYPE
#endif

#include <stdint.h>

#include <assert.h>
#include <slice/slice.h>
#include <stdlib.h>
#include <string.h>

#define SLICE_PASTE_(a, b) a##b
#define SLICE_PASTE(a, b) SLICE_PASTE_(a, b)
#define SLICE_T SLICE_PASTE(slice_, SLICE_NAME)
#define SLICE_FN(suffix) SLICE_PASTE(SLICE_T, suffix)

SliceError SLICE_FN(_init)(SLICE_T *slice, size_t len) {
  slice->len = len;
  slice->data = malloc(len * sizeof(SLICE_DTYPE));
  if (slice->data == NULL) {
    return SLICE_ERR_OUT_OF_MEM;
  }
  return SLICE_ERR_OK;
}

SliceError SLICE_FN(_dup)(SLICE_T *slice, SLICE_T *out) {
  out->len = slice->len;
  out->data = malloc(slice->len * sizeof(SLICE_DTYPE));
  if (out->data == NULL) {
    return SLICE_ERR_OUT_OF_MEM;
  }
  memcpy(out->data, slice->data, slice->len * sizeof(SLICE_DTYPE));
  return SLICE_ERR_OK;
}

// compare for byte equality
bool SLICE_FN(_eq(SLICE_T a, SLICE_T b)) {
  if(a.len != b.len) {
    return false;
  }
  return memcmp(a.data, b.data, a.len *sizeof(SLICE_DTYPE)) == 0;
}

#undef SLICE_T
#undef SLICE_FN
#undef SLICE_PASTE
#undef SLICE_PASTE_
