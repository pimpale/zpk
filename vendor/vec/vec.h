#ifndef VEC_DTYPE
#error "VEC_DTYPE must be defined before including vec.h"
#endif

#ifndef VEC_NAME
#define VEC_NAME VEC_DTYPE
#endif

#include <stddef.h>
#include <stdint.h>

#define VEC_PASTE_(a, b) a##b
#define VEC_PASTE(a, b) VEC_PASTE_(a, b)
#define VEC_T VEC_PASTE(vec_, VEC_NAME)
#define VEC_FN(suffix) VEC_PASTE(VEC_T, suffix)

typedef struct VEC_T {
  VEC_DTYPE *pData;
  size_t cap;
  size_t len;
} VEC_T;

typedef enum VecError {
  VEC_ERR_OK = 0,
  VEC_ERR_OUT_OF_MEMORY,
} VecError;

VecError VEC_FN(_init)(VEC_T *vec);
VecError VEC_FN(_init_cap)(VEC_T *vec, size_t cap);
void VEC_FN(_delete)(VEC_T *vec);

VecError VEC_FN(_push)(VEC_T *vec, const VEC_DTYPE *src);

VecError VEC_FN(_pushv)(VEC_T *vec, const VEC_DTYPE *src, size_t n);

VecError VEC_FN(_append)(VEC_T *vec, const VEC_T *src);

void VEC_FN(_pop)(VEC_T *vec, VEC_DTYPE *dest);

void VEC_FN(_get)(const VEC_T *vec, size_t i, VEC_DTYPE *dest);

VEC_DTYPE *VEC_FN(_at)(const VEC_T *vec, size_t i);

// swaps i with the last element and then pops, deleting the data
void VEC_FN(_swap_and_pop)(VEC_T *vec, size_t i);

void VEC_FN(_clear)(VEC_T *vec);

size_t VEC_FN(_len)(const VEC_T *vec);

#undef VEC_T
#undef VEC_FN
#undef VEC_PASTE
#undef VEC_PASTE_
