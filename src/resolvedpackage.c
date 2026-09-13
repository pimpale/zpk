#include "resolvedpackage.h"
#include <stdlib.h>
#include <asprintf/asprintf.h>

void delete_ResolvedPackage(ResolvedPackage *rp) {
  free(rp->package);
  free(rp->package_path);
  free(rp->repository);
  free(rp->version);
  free(rp->entry);
}
