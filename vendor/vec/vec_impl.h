#ifndef VEC_DTYPE
#error "VEC_DTYPE must be defined before including vec_impl.h"
#endif

#ifndef VEC_NAME
#define VEC_NAME VEC_DTYPE
#endif

#include <stdint.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <vec/vec.h>

#define VEC_PASTE_(a, b) a##b
#define VEC_PASTE(a, b) VEC_PASTE_(a, b)
#define VEC_T VEC_PASTE(vec_, VEC_NAME)
#define VEC_FN(suffix) VEC_PASTE(VEC_T, suffix)

void VEC_FN(_init)(VEC_T *vec) {
  VEC_FN(_init_cap)(vec, 16);
}

void VEC_FN(_init_cap)(VEC_T *vec, size_t cap) {
  vec->len = 0;
  vec->cap = cap;
  vec->pData = (VEC_DTYPE*)malloc(vec->cap * sizeof(VEC_DTYPE));
}

void VEC_FN(_push)(VEC_T *vec, const VEC_DTYPE *src) {
  if (vec->len >= vec->cap) {
    vec->cap *= 2;
    vec->pData = (VEC_DTYPE*)realloc(vec->pData, vec->cap * sizeof(VEC_DTYPE));
  }
  vec->pData[vec->len] = *src;
  vec->len++;
}

void VEC_FN(_pushv)(VEC_T *vec, const VEC_DTYPE *src, size_t len) {
  if (len == 0) {
    return;
  }
  if (vec->len + len > vec->cap) {
    while (vec->len + len > vec->cap) {
      vec->cap *= 2;
    }
    vec->pData = (VEC_DTYPE*)realloc(vec->pData, vec->cap * sizeof(VEC_DTYPE));
  }
  memcpy(&vec->pData[vec->len], src, len * sizeof(VEC_DTYPE));
  vec->len += len;
}

void VEC_FN(_append)(VEC_T *vec, const VEC_T *src) {
  VEC_FN(_pushv)(vec, src->pData, src->len);
}

void VEC_FN(_pop)(VEC_T *vec, VEC_DTYPE *dest) {
  assert(vec->len > 0);
  *dest = vec->pData[vec->len - 1];
  vec->len--;
}

void VEC_FN(_clear)(VEC_T *vec) { vec->len = 0; }

void VEC_FN(_get)(const VEC_T *vec, size_t i, VEC_DTYPE *dest) {
  assert(i < vec->len);
  *dest = vec->pData[i];
}

VEC_DTYPE *VEC_FN(_at)(const VEC_T *vec, size_t i) {
  assert(i < vec->len);
  return &vec->pData[i];
}

void VEC_FN(_swap_and_pop)(VEC_T *vec, size_t i) {
  assert(vec->len > 0);
  assert(i < vec->len);
  vec->pData[i] = vec->pData[vec->len - 1];
  vec->len--;
}

size_t VEC_FN(_len)(const VEC_T *vec) { return vec->len; }

void VEC_FN(_delete)(VEC_T *vec) {
  free(vec->pData);
}

#undef VEC_T
#undef VEC_FN
#undef VEC_PASTE
#undef VEC_PASTE_
