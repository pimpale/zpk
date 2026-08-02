#ifndef vec_indexdataentry_h_INCLUDED
#define vec_indexdataentry_h_INCLUDED

#include <stdbool.h>
#include <stdint.h>

// one package's claim on an indexed path. packages may conflict: several
// entries can claim the same path, and a path claimed as a directory by one
// package and a file by another is also representable (and detectable)
typedef struct {
  char *package;  // claimant; owned by the index's creator
  bool is_directory;
  // crc32 of the claimant's copy (0 for directories). comparing against the
  // file on disk tells you which claimant's copy is actually installed
  uint32_t crc32;
} IndexDataEntry;

#define VEC_DTYPE IndexDataEntry
#include <vec/vec.h>
#undef VEC_DTYPE

#endif // vec_indexdataentry_h_INCLUDED
