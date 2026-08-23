#include <errno.h>
#include <stdio.h>

#include "fileutils.h"

int copy_file(const char *from, const char *to) {
  FILE *src = fopen(from, "rb");
  if (src == NULL) {
    return -1;
  }
  FILE *dst = fopen(to, "wb");
  if (dst == NULL) {
    int saved_errno = errno;
    fclose(src);
    errno = saved_errno;
    return -1;
  }

  char buf[64 * 1024];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, src)) > 0) {
    if (fwrite(buf, 1, n, dst) != n) {
      int saved_errno = errno;
      fclose(src);
      fclose(dst);
      errno = saved_errno;
      return -1;
    }
  }
  if (ferror(src) != 0) {
    int saved_errno = errno;
    fclose(src);
    fclose(dst);
    errno = saved_errno;
    return -1;
  }
  fclose(src);
  // the last buffered write can only fail here, so this close is load-bearing
  if (fclose(dst) != 0) {
    return -1;
  }
  return 0;
}
