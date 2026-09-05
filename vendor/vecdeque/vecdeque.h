#ifndef VECDEQUE_DTYPE
#error "VECDEQUE_DTYPE must be defined before including vecdeque.h"
#endif

#ifndef VECDEQUE_NAME
#define VECDEQUE_NAME VECDEQUE_DTYPE
#endif

#include <stddef.h>
#include <stdint.h>

#define VECDEQUE_PASTE_(a, b) a##b
#define VECDEQUE_PASTE(a, b) VECDEQUE_PASTE_(a, b)
#define VECDEQUE_T VECDEQUE_PASTE(vecdeque_, VECDEQUE_NAME)
#define VECDEQUE_FN(suffix) VECDEQUE_PASTE(VECDEQUE_T, suffix)

struct VECDEQUE_T {
  VECDEQUE_DTYPE *pData;
  size_t cap;
  size_t len;
  size_t head;
};

typedef struct VECDEQUE_T VECDEQUE_T;

void VECDEQUE_FN(_init)(VECDEQUE_T *vecdeque);
void VECDEQUE_FN(_init_cap)(VECDEQUE_T *vecdeque, size_t cap);
void VECDEQUE_FN(_delete)(VECDEQUE_T *vecdeque);

void VECDEQUE_FN(_push_back)(VECDEQUE_T *vecdeque, const VECDEQUE_DTYPE *src);
// repeated push back
void VECDEQUE_FN(_push_backv)(VECDEQUE_T *vecdeque, const VECDEQUE_DTYPE *src, size_t n);

void VECDEQUE_FN(_push_front)(VECDEQUE_T *vecdeque, const VECDEQUE_DTYPE *src);
// repeated push front
void VECDEQUE_FN(_push_frontv)(VECDEQUE_T *vecdeque, const VECDEQUE_DTYPE *src, size_t n);
// repeated push front, but iterates src in reverse (useful for loading buffer) 
void VECDEQUE_FN(_push_frontv_rev)(VECDEQUE_T *vecdeque, const VECDEQUE_DTYPE *src, size_t n);

void VECDEQUE_FN(_pop_back)(VECDEQUE_T *vecdeque, VECDEQUE_DTYPE *dest);
void VECDEQUE_FN(_pop_backv)(VECDEQUE_T *vecdeque, VECDEQUE_DTYPE *dest, size_t n);

void VECDEQUE_FN(_pop_front)(VECDEQUE_T *vecdeque, VECDEQUE_DTYPE *dest);
void VECDEQUE_FN(_pop_frontv)(VECDEQUE_T *vecdeque, VECDEQUE_DTYPE *dest, size_t n);


void VECDEQUE_FN(_get)(const VECDEQUE_T *vecdeque, size_t i, VECDEQUE_DTYPE *dest);

VECDEQUE_DTYPE *VECDEQUE_FN(_at)(const VECDEQUE_T *vecdeque, size_t i);

void VECDEQUE_FN(_clear)(VECDEQUE_T *vecdeque);

size_t VECDEQUE_FN(_len)(const VECDEQUE_T *vecdeque);

#undef VECDEQUE_T
#undef VECDEQUE_FN
#undef VECDEQUE_PASTE
#undef VECDEQUE_PASTE_
