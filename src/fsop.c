#include "fsop.h"
#include <stdlib.h>

void delete_fsop(fsop_t *o) {
  free(o->pkg);
  switch (o->kind) {
  case FSOP_RENAME:
    free(o->rename.from);
    free(o->rename.to);
    break;
  case FSOP_CREATEFILE:
    free(o->createfile.path);
    break;
  case FSOP_REMOVEFILE:
    free(o->removefile.path);
    break;
  case FSOP_MKDIR:
    free(o->mkdir.path);
    break;
  case FSOP_RMDIR:
    free(o->rmdir.path);
    break;
  }
}
