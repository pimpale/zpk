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
  while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
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
  if (fclose(dst) != 0) {
    return -1;
  }
  return 0;
}


int read_file(const char *path, vec_uint8_t *out) {
  FILE *src = fopen(path, "rb");
  if (src == NULL) {
    return -1;
  }

  uint8_t buf[64 * 1024];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
    if (vec_uint8_t_pushv(out, buf, n) != 0) {
      fclose(src);
      errno = ENOMEM;
      return -1;
    }
  }
  if (ferror(src) != 0) {
    int saved_errno = errno;
    fclose(src);
    errno = saved_errno;
    return -1;
  }
  if(fclose(src) != 0) {
    return -1;
  }
  return 0;
}
