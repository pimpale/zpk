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
  union {
    struct {
      char *from;
      char *to;
    } rename;
    struct {
      char *path;
      size_t zip_index;
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
} fsop;

void delete_fsop(fsop *o);

#endif // fsop_h_INCLUDED
