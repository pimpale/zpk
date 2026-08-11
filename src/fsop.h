#ifndef fsop_h_INCLUDED
#define fsop_h_INCLUDED

#include "miniz/miniz.h"
#include <stdint.h>

typedef enum {
  FSOP_RENAME,
  FSOP_CREATEFILE,
  FSOP_REMOVEFILE,
  FSOP_MKDIR,
  FSOP_RMDIR
} fsop_kind_t;

typedef struct {
  fsop_kind_t kind;
  // not owned by delete_fsop
  const char* op;
  // needs to be owned. lifetime can be uncertain.
  char* pkg;
  union {
    struct {
      char *from;
      char *to;
    } rename;
    struct {
      char *path;
      // not owned!
      mz_zip_archive* zip;
      uint32_t file_index;
    } createfile;
    struct {
      char *path;
    } removefile;
    struct {
      char *path;
    } mkdir;
    struct {
      char *path;
    } rmdir;
  };
} fsop_t;

void delete_fsop(fsop_t *o);

#endif // fsop_h_INCLUDED
