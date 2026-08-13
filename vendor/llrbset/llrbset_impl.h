// Generic ordered-set implementation. Include exactly once per LLRBSET_NAME
// from a dedicated .c file, after the matching llrbset.h declaration has been
// included. Required configuration:
//
//   #define LLRBSET_NAME pid
//   #define LLRBSET_KEY uint64_t
//   #define LLRBSET_COMPARE(a, b) (((*a) > (*b)) - ((*a) < (*b)))
//   #include <llrbset/llrbset_impl.h>
//
// LLRBSET_COMPARE receives pointers to keys and returns negative, zero, or
// positive. LLRBSET_MALLOC/LLRBSET_FREE and
// LLRBSET_NODE_MALLOC/LLRBSET_NODE_FREE may optionally override the backing
// llrb allocation hooks.

#ifndef LLRBSET_NAME
#error "LLRBSET_NAME must be defined before including llrbset_impl.h"
#endif
#ifndef LLRBSET_KEY
#error "LLRBSET_KEY must be defined before including llrbset_impl.h"
#endif
#ifndef LLRBSET_COMPARE
#error "LLRBSET_COMPARE(a, b) must be defined before including llrbset_impl.h"
#endif

#if defined(LLRB_NAME) || defined(LLRB_KEY) || defined(LLRB_VALUE) ||          \
    defined(LLRB_COMPARE)
#error "LLRB_NAME, LLRB_KEY, LLRB_VALUE, and LLRB_COMPARE must be undefined"
#endif

#include <stdbool.h>
#include <stddef.h>

#define LLRBSET_PASTE_(a, b) a##b
#define LLRBSET_PASTE(a, b) LLRBSET_PASTE_(a, b)
#define LLRBSET_T LLRBSET_PASTE(llrbset_, LLRBSET_NAME)
#define LLRBSET_ITER_T LLRBSET_PASTE(LLRBSET_T, _iter)
#define LLRBSET_DUMMY_T LLRBSET_PASTE(LLRBSET_T, _impl_dummy)
#define LLRBSET_MAP_NAME LLRBSET_PASTE(LLRBSET_NAME, _llrbset_backing)
#define LLRBSET_MAP_T LLRBSET_PASTE(llrb_, LLRBSET_MAP_NAME)
#define LLRBSET_MAP_ITER_T LLRBSET_PASTE(LLRBSET_MAP_T, _iter)
#define LLRBSET_MAP_FN(suffix) LLRBSET_PASTE(LLRBSET_MAP_T, suffix)
#define LLRBSET_FN(suffix) LLRBSET_PASTE(LLRBSET_T, suffix)

#define LLRB_NAME LLRBSET_MAP_NAME
#define LLRB_KEY LLRBSET_KEY
#define LLRB_VALUE LLRBSET_DUMMY_T
#define LLRB_COMPARE(a, b) LLRBSET_COMPARE((a), (b))

#ifdef LLRBSET_MALLOC
#define LLRB_MALLOC(size) LLRBSET_MALLOC(size)
#endif
#ifdef LLRBSET_FREE
#define LLRB_FREE(ptr) LLRBSET_FREE(ptr)
#endif
#ifdef LLRBSET_NODE_MALLOC
#define LLRB_NODE_MALLOC(size) LLRBSET_NODE_MALLOC(size)
#endif
#ifdef LLRBSET_NODE_FREE
#define LLRB_NODE_FREE(ptr) LLRBSET_NODE_FREE(ptr)
#endif

#include <llrb/llrb_impl.h>

#ifdef LLRBSET_NODE_FREE
#undef LLRB_NODE_FREE
#endif
#ifdef LLRBSET_NODE_MALLOC
#undef LLRB_NODE_MALLOC
#endif
#ifdef LLRBSET_FREE
#undef LLRB_FREE
#endif
#ifdef LLRBSET_MALLOC
#undef LLRB_MALLOC
#endif
#undef LLRB_COMPARE
#undef LLRB_VALUE
#undef LLRB_KEY
#undef LLRB_NAME

void LLRBSET_FN(_new)(LLRBSET_T *set) { LLRBSET_MAP_FN(_new)(&set->impl); }

void LLRBSET_FN(_delete)(LLRBSET_T *set) {
  LLRBSET_MAP_FN(_delete)(&set->impl);
}

void LLRBSET_FN(_clear)(LLRBSET_T *set) { LLRBSET_MAP_FN(_clear)(&set->impl); }

bool LLRBSET_FN(_insert)(LLRBSET_T *set, const LLRBSET_KEY *key) {
  const LLRBSET_DUMMY_T present = 0;
  return LLRBSET_MAP_FN(_insert)(&set->impl, key, &present) != NULL;
}

bool LLRBSET_FN(_remove)(LLRBSET_T *set, const LLRBSET_KEY *key,
                         LLRBSET_KEY *old_key) {
  return LLRBSET_MAP_FN(_remove)(&set->impl, key, old_key, NULL);
}

bool LLRBSET_FN(_contains)(const LLRBSET_T *set, const LLRBSET_KEY *key) {
  return LLRBSET_MAP_FN(_get)(&set->impl, key, NULL);
}

bool LLRBSET_FN(_floor)(const LLRBSET_T *set, const LLRBSET_KEY *key,
                        LLRBSET_KEY *found_key) {
  return LLRBSET_MAP_FN(_floor)(&set->impl, key, found_key, NULL);
}

size_t LLRBSET_FN(_len)(const LLRBSET_T *set) {
  return LLRBSET_MAP_FN(_len)(&set->impl);
}

void LLRBSET_FN(_iter_begin)(const LLRBSET_T *set, LLRBSET_ITER_T *iter) {
  LLRBSET_MAP_FN(_iter_begin)(&set->impl, &iter->impl);
}

bool LLRBSET_FN(_iter_next)(LLRBSET_ITER_T *iter, LLRBSET_KEY *key) {
  return LLRBSET_MAP_FN(_iter_next)(&iter->impl, key, NULL);
}

void LLRBSET_FN(_iter_lower_bound)(const LLRBSET_T *set, const LLRBSET_KEY *key,
                                   LLRBSET_ITER_T *iter) {
  LLRBSET_MAP_FN(_iter_lower_bound)(&set->impl, key, &iter->impl);
}

void LLRBSET_FN(_iter_rbegin)(const LLRBSET_T *set, LLRBSET_ITER_T *iter) {
  LLRBSET_MAP_FN(_iter_rbegin)(&set->impl, &iter->impl);
}

bool LLRBSET_FN(_iter_prev)(LLRBSET_ITER_T *iter, LLRBSET_KEY *key) {
  return LLRBSET_MAP_FN(_iter_prev)(&iter->impl, key, NULL);
}

void LLRBSET_FN(_iter_floor)(const LLRBSET_T *set, const LLRBSET_KEY *key,
                             LLRBSET_ITER_T *iter) {
  LLRBSET_MAP_FN(_iter_floor)(&set->impl, key, &iter->impl);
}

bool LLRBSET_FN(_valid)(const LLRBSET_T *set) {
  return LLRBSET_MAP_FN(_valid)(&set->impl);
}

#undef LLRBSET_FN
#undef LLRBSET_MAP_FN
#undef LLRBSET_MAP_ITER_T
#undef LLRBSET_MAP_T
#undef LLRBSET_MAP_NAME
#undef LLRBSET_DUMMY_T
#undef LLRBSET_ITER_T
#undef LLRBSET_T
#undef LLRBSET_PASTE
#undef LLRBSET_PASTE_
