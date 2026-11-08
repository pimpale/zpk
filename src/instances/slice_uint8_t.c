#include "instances/slice_uint8_t.h"
#include <stdlib.h>
#include <string.h>

#define SLICE_DTYPE uint8_t
#include <slice/slice_impl.h>
#undef SLICE_DTYPE

char *slice_uint8_t_to_allocated_str(slice_uint8_t in) {
  char *tmp = malloc(in.len + 1);
  if (tmp == NULL) {
    return tmp;
  }
  memcpy(tmp, in.data, in.len);
  tmp[in.len] = 0;
  return tmp;
}

slice_uint8_t slice_uint8_t_from_str(char *s) {
  return (slice_uint8_t){(uint8_t *)s, strlen(s)};
}

int slice_uint8_t_cmp(slice_uint8_t *a, slice_uint8_t *b) {
  bool alonger = a->len > b->len;
  size_t minlen = alonger ? b->len : a->len;

  int res = minlen ? memcmp(a->data, b->data, minlen) : 0;
  if (res != 0) {
    return res;
  } else if (a->len != b->len) {
    return alonger ? 1 : -1;
  } else {
    return 0;
  }
}
