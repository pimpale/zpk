#ifndef VECDEQUE_DTYPE
#error "VECDEQUE_DTYPE must be defined before including vecdeque_impl.h"
#endif

#ifndef VECDEQUE_NAME
#define VECDEQUE_NAME VECDEQUE_DTYPE
#endif

#include <stdint.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <vecdeque/vecdeque.h>

#define VECDEQUE_PASTE_(a, b) a##b
#define VECDEQUE_PASTE(a, b) VECDEQUE_PASTE_(a, b)
#define VECDEQUE_T VECDEQUE_PASTE(vecdeque_, VECDEQUE_NAME)
#define VECDEQUE_FN(suffix) VECDEQUE_PASTE(VECDEQUE_T, suffix)

void VECDEQUE_FN(_init)(VECDEQUE_T *vecdeque) {
  VECDEQUE_FN(_init_cap)(vecdeque, 16);
}

void VECDEQUE_FN(_init_cap)(VECDEQUE_T *vecdeque, size_t cap) {
  vecdeque->len = 0;
  vecdeque->cap = cap;
  vecdeque->head = 0;
  vecdeque->pData = (VECDEQUE_DTYPE *)malloc(vecdeque->cap * sizeof(VECDEQUE_DTYPE));
}

// have at least these many slots
static void VECDEQUE_FN(_grow)(VECDEQUE_T *vecdeque, size_t at_least_cap) {
  if (vecdeque->cap >= at_least_cap) {
    return;
  }
  size_t new_cap = 1;
  while (new_cap < at_least_cap) {
    new_cap *= 2;
  }
  vecdeque->pData = realloc(vecdeque->pData, new_cap * sizeof(VECDEQUE_DTYPE));
  if (vecdeque->head + vecdeque->len > vecdeque->cap) {
    memmove(
      vecdeque->pData + vecdeque->head + (new_cap - vecdeque->cap),
      vecdeque->pData + vecdeque->head,
      (vecdeque->cap - vecdeque->head) * sizeof(VECDEQUE_DTYPE)
    );
    vecdeque->head = vecdeque->head + (new_cap - vecdeque->cap);
  }
  vecdeque->cap = new_cap;
}

void VECDEQUE_FN(_push_backv)(VECDEQUE_T *vecdeque, const VECDEQUE_DTYPE *src, size_t len) {
  VECDEQUE_FN(_grow)(vecdeque, vecdeque->len + len);
  for (size_t i = 0; i < len; i++) {
    vecdeque->pData[(vecdeque->head + vecdeque->len + i) % vecdeque->cap] = src[i];
  }
  vecdeque->len += len;
}

void VECDEQUE_FN(_push_back)(VECDEQUE_T *vecdeque, const VECDEQUE_DTYPE *src) {
  VECDEQUE_FN(_push_backv)(vecdeque, src, 1);
}

void VECDEQUE_FN(_push_frontv)(VECDEQUE_T *vecdeque, const VECDEQUE_DTYPE *src, size_t len) {
  VECDEQUE_FN(_grow)(vecdeque, vecdeque->len + len);
  for (size_t i = 0; i < len; i++) {
    vecdeque->pData[(vecdeque->head + vecdeque->cap - i-1) % vecdeque->cap] = src[i];
  }
  vecdeque->len += len;
  vecdeque->head = (vecdeque->head + vecdeque->cap  - len) % vecdeque->cap;
}

void VECDEQUE_FN(_push_frontv_rev)(VECDEQUE_T *vecdeque, const VECDEQUE_DTYPE *src, size_t len) {
  VECDEQUE_FN(_grow)(vecdeque, vecdeque->len + len);
  for (size_t i_r = 0; i_r < len; i_r++) {
    size_t i = len-i_r-1;
    vecdeque->pData[(vecdeque->head + vecdeque->cap - i-1) % vecdeque->cap] = src[i_r];
  }
  vecdeque->len += len;
  vecdeque->head = (vecdeque->head + vecdeque->cap  - len) % vecdeque->cap;
}

void VECDEQUE_FN(_push_front)(VECDEQUE_T *vecdeque, const VECDEQUE_DTYPE *src) {
  VECDEQUE_FN(_push_frontv)(vecdeque, src, 1);
}

void VECDEQUE_FN(_pop_backv)(VECDEQUE_T *vecdeque, VECDEQUE_DTYPE *dest, size_t len) {
  assert(vecdeque->len >= len);
  for (size_t i = 0; i < len; i++) {
    dest[i] = vecdeque->pData[(vecdeque->head + vecdeque->len - i-1) % vecdeque->cap];
  }
  vecdeque->len -= len;
}

void VECDEQUE_FN(_pop_back)(VECDEQUE_T *vecdeque, VECDEQUE_DTYPE *dest) {
  VECDEQUE_FN(_pop_backv)(vecdeque, dest, 1);
}

void VECDEQUE_FN(_pop_frontv)(VECDEQUE_T *vecdeque, VECDEQUE_DTYPE *dest, size_t len) {
  assert(vecdeque->len >= len);
  for (size_t i = 0; i < len; i++) {
    dest[i] = vecdeque->pData[(vecdeque->head + i) % vecdeque->cap];
  }
  vecdeque->len -= len;
  vecdeque->head = (vecdeque->head + len) % vecdeque->cap;
}

void VECDEQUE_FN(_pop_front)(VECDEQUE_T *vecdeque, VECDEQUE_DTYPE *dest) {
  VECDEQUE_FN(_pop_frontv)(vecdeque, dest, 1);
}

void VECDEQUE_FN(_clear)(VECDEQUE_T *vecdeque) {
  vecdeque->len = 0;
}

void VECDEQUE_FN(_get)(const VECDEQUE_T *vecdeque, size_t i, VECDEQUE_DTYPE *dest) {
  *dest = *VECDEQUE_FN(_at)(vecdeque, i);
}

VECDEQUE_DTYPE *VECDEQUE_FN(_at)(const VECDEQUE_T *vecdeque, size_t i) {
  assert(i < vecdeque->len);
  return &vecdeque->pData[(vecdeque->head+i)%vecdeque->cap];
}

size_t VECDEQUE_FN(_len)(const VECDEQUE_T *vecdeque) {
  return vecdeque->len;
}

void VECDEQUE_FN(_delete)(VECDEQUE_T *vecdeque) {
  free(vecdeque->pData);
}

#undef VECDEQUE_T
#undef VECDEQUE_FN
#undef VECDEQUE_PASTE
#undef VECDEQUE_PASTE_
