#include "resolvedpackage.h"
#include <stdlib.h>
#include <asprintf/asprintf.h>

void delete_ResolvedPackage(ResolvedPackage *rp) {
  free(rp->package);
  free(rp->package_path);
  free(rp->repository);
  free(rp->version);
}

char* entryname(ResolvedPackage *rp) {
  char* out;
  asprintf(&out, "%s-%s.zip", rp->package, rp->version);
  return out;
}
