#include "resolvedpackage.h"
#include <stdlib.h>
#include <asprintf/asprintf.h>

void delete_ResolvedPackage(ResolvedPackage *rp) {
  free(rp->source_uri_storage.data);
  free(rp->local_path);
}
