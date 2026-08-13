// Generic ordered-set template backed by llrb. Include with a name and key
// type:
//
//   #define LLRBSET_NAME pid
//   #define LLRBSET_KEY uint64_t
//   #include <llrbset/llrbset.h>
//
// This declares llrbset_pid and its operations. The set struct is
// caller-owned; _new initializes it in place and _delete frees the nodes but
// not the struct. Keys are copied into independently allocated nodes.
//
// LLRBSET_NAME must be a single identifier usable in token pasting and
// LLRBSET_KEY must be a complete type. This file intentionally has no header
// guard so multiple differently named sets can be declared.

#ifndef LLRBSET_NAME
#error "LLRBSET_NAME must be defined before including llrbset.h"
#endif
#ifndef LLRBSET_KEY
#error "LLRBSET_KEY must be defined before including llrbset.h"
#endif

// The wrapper temporarily configures llrb itself. Reject an overlapping llrb
// instantiation rather than silently replacing its configuration.
#if defined(LLRB_NAME) || defined(LLRB_KEY) || defined(LLRB_VALUE)
#error "LLRB_NAME, LLRB_KEY, and LLRB_VALUE must be undefined"
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
#define LLRBSET_FN(suffix) LLRBSET_PASTE(LLRBSET_T, suffix)

// llrb currently requires a complete value type. This byte is an internal
// implementation detail and is never exposed through the set API.
typedef unsigned char LLRBSET_DUMMY_T;

#define LLRB_NAME LLRBSET_MAP_NAME
#define LLRB_KEY LLRBSET_KEY
#define LLRB_VALUE LLRBSET_DUMMY_T
#include <llrb/llrb.h>
#undef LLRB_VALUE
#undef LLRB_KEY
#undef LLRB_NAME

typedef struct LLRBSET_T LLRBSET_T;
typedef struct LLRBSET_ITER_T LLRBSET_ITER_T;

// Publicly complete so callers can own the storage. Treat the backing fields
// as private and use only the llrbset API below.
struct LLRBSET_T {
  LLRBSET_MAP_T impl;
};

struct LLRBSET_ITER_T {
  LLRBSET_MAP_ITER_T impl;
};

void LLRBSET_FN(_new)(LLRBSET_T *set);
void LLRBSET_FN(_delete)(LLRBSET_T *set);
void LLRBSET_FN(_clear)(LLRBSET_T *set);

// Inserts a copy of *key. Returns true when inserted, or false when an
// equivalent key was already present or allocation failed.
bool LLRBSET_FN(_insert)(LLRBSET_T *set, const LLRBSET_KEY *key);

// Removes the key equivalent to *key. When old_key is non-NULL, it receives
// the stored key copy; it is left untouched if no equivalent key was found.
// This is useful for reclaiming resources owned by pointer-like keys.
bool LLRBSET_FN(_remove)(LLRBSET_T *set, const LLRBSET_KEY *key,
                         LLRBSET_KEY *old_key);

bool LLRBSET_FN(_contains)(const LLRBSET_T *set, const LLRBSET_KEY *key);

// Finds the greatest stored key <= *key. found_key may be NULL.
bool LLRBSET_FN(_floor)(const LLRBSET_T *set, const LLRBSET_KEY *key,
                        LLRBSET_KEY *found_key);

size_t LLRBSET_FN(_len)(const LLRBSET_T *set);

// Iterators are allocation-free. Mutating the set invalidates all active
// iterators. _iter_next walks in ascending order and _iter_prev in descending
// order; their key output may be NULL.
void LLRBSET_FN(_iter_begin)(const LLRBSET_T *set, LLRBSET_ITER_T *iter);
bool LLRBSET_FN(_iter_next)(LLRBSET_ITER_T *iter, LLRBSET_KEY *key);

// Positions iter at the first key >= *key, to be returned by _iter_next.
void LLRBSET_FN(_iter_lower_bound)(const LLRBSET_T *set, const LLRBSET_KEY *key,
                                   LLRBSET_ITER_T *iter);

void LLRBSET_FN(_iter_rbegin)(const LLRBSET_T *set, LLRBSET_ITER_T *iter);
bool LLRBSET_FN(_iter_prev)(LLRBSET_ITER_T *iter, LLRBSET_KEY *key);

// Positions iter at the greatest key <= *key, to be returned by _iter_prev.
void LLRBSET_FN(_iter_floor)(const LLRBSET_T *set, const LLRBSET_KEY *key,
                             LLRBSET_ITER_T *iter);

// Intended for tests and diagnostics.
bool LLRBSET_FN(_valid)(const LLRBSET_T *set);

#undef LLRBSET_FN
#undef LLRBSET_MAP_ITER_T
#undef LLRBSET_MAP_T
#undef LLRBSET_MAP_NAME
#undef LLRBSET_DUMMY_T
#undef LLRBSET_ITER_T
#undef LLRBSET_T
#undef LLRBSET_PASTE
#undef LLRBSET_PASTE_
