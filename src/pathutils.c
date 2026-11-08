#include "pathutils.h"
#include <assert.h>
#include <stddefer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
#include "instances/slice_uint8_t.h"
#include "oscompatlayer.h"

// allocates a new string with the tilde expanded to the user's home directory,
// if applicable. only expands tilde at the start of the string, and only if
// followed by a slash or end of string. errors are fatal
char *expandtilde(const char *input) {
  if (input[0] != '~' || (input[1] != '/' && input[1] != '\0')) {
    return strdup(input);
  }

  char *home = getenv_home_portable();
  defer free(home);

  if (home == NULL) {
    LOG_ERROR(ERR_LEVEL_FATAL, "could not determine home directory for tilde expansion");
    PANIC();
  }
  char *expanded = joinstr2(home, input + 1);
  if (expanded == NULL) {
    LOG_ERROR(ERR_LEVEL_FATAL, "could not allocate memory for tilde expansion");
    PANIC();
  }
  return expanded;
}

// allocates
char *normalize(const char *filename) {
  char *out = malloc(strlen(filename) + 1);
  size_t out_len = 0;
  size_t comp_start = 0;

  enum {
    CS_START,
    CS_ONEDOT,
    CS_TWODOTS,
    CS_OTHER
  } state = CS_START;

  for (const char *p = filename;; p++) {
    char c = *p;
    if (c == '\0' || c == '/') {
      if (state == CS_TWODOTS) {
        free(out);
        return NULL;
      }
      if (state == CS_START || state == CS_ONEDOT) {
        // empty or "." component: drop it
        out_len = comp_start;
      } else if (c == '/') {
        out[out_len++] = '/';
        comp_start = out_len;
      }
      state = CS_START;
      if (c == '\0') {
        break;
      }
    } else if (c == '\\') {
      // forbid \\ because it might be a path traversal on windows.
      free(out);
      return NULL;
    } else {
      out[out_len++] = c;
      if (c == '.') {
        if (state == CS_START) {
          state = CS_ONEDOT;
        } else if (state == CS_ONEDOT) {
          state = CS_TWODOTS;
        } else {
          state = CS_OTHER;
        }
      } else {
        state = CS_OTHER;
      }
    }
  }
  if (out_len == 0) {
    free(out);
    return NULL;
  }
  // omit trailing / for normalization purposes
  if (out[out_len - 1] == '/') {
    out_len--;
  }
  out[out_len] = '\0';

  return out;
}

bool startswith(const char *str, const char *prefix) {
  size_t len = strlen(str);
  size_t prelen = strlen(prefix);
  if (prelen > len) {
    return false;
  }
  return strncmp(str, prefix, prelen) == 0;
}

bool endswith(const char *str, const char *suffix) {
  size_t len = strlen(str);
  size_t suflen = strlen(suffix);
  if (suflen > len) {
    return false;
  }
  return strcmp(str + (len - suflen), suffix) == 0;
}

// returns the last part of the path
// the path MUST be absolute
char *basename_m(char *input) {
  char *c = strrchr(input, '/');
  assert(c != NULL);
  return c + 1;
}

// returns the last part of the path
// the path MUST be absolute
slice_uint8_t basename_slice(slice_uint8_t basename) {
  for (size_t i_r = 0; i_r < basename.len; i_r++) {
    size_t i = basename.len - i_r - 1;
    if (basename.data[i] == '/') {
      return (slice_uint8_t){.data = basename.data + i + 1, .len = i_r};
    }
  }
  return (slice_uint8_t){};
}

char *joinstr2(const char *s1, const char *s2) {
  size_t len1 = strlen(s1);
  size_t len2 = strlen(s2);
  char *result = malloc(len1 + len2 + 1);

  if (result == NULL) {
    return NULL;
  }

  strcpy(result, s1);
  strcat(result, s2);
  return result;
}

slice_uint8_t joinslice(slice_uint8_t s1, slice_uint8_t s2) {
  size_t nlen = s1.len + s2.len;
  uint8_t *ptr = malloc(nlen);
  if (ptr == NULL) {
    return (slice_uint8_t){};
  }
  memcpy(ptr, s1.data, s1.len);
  memcpy(ptr + s1.len, s2.data, s2.len);
  return (slice_uint8_t){.data = ptr, .len = nlen};
}

char *joinpath(const char *s1, const char *s3) {
  if (endswith(s1, "/")) {
    return joinstr2(s1, s3);
  }
  const char *s2 = "/";

  size_t len1 = strlen(s1);
  size_t len2 = strlen(s2);
  size_t len3 = strlen(s3);
  char *result = malloc(len1 + len2 + len3 + 1);

  if (result == NULL) {
    return NULL;
  }

  strcpy(result, s1);
  strcat(result, s2);
  strcat(result, s3);
  return result;
}

char *replacesuf(const char *input, const char *oldsuf, const char *newsuf) {
  if (!endswith(input, oldsuf)) {
    return NULL;
  }
  size_t inputlen = strlen(input);
  size_t prefixlen = inputlen - strlen(oldsuf);
  size_t newsuflen = strlen(newsuf);
  char *result = malloc(prefixlen + newsuflen + 1);
  if (result == NULL) {
    return NULL;
  }
  memcpy(result, input, prefixlen);
  strcpy(result + prefixlen, newsuf);
  return result;
}

const char *maybesep(const char *in) {
  return endswith(in, "/") ? "" : "/";
}

slice_uint8_t joinpath_slice(slice_uint8_t a, slice_uint8_t b) {
  // could probably save a byte here
  return slice_uint8_t_from_str(joinpath_slice_str(a, b));
}

char *joinpath_slice_str(slice_uint8_t a, slice_uint8_t b) {
  size_t seplen;
  if (a.len > 0 && a.data[a.len - 1] == '/') {
    seplen = 0;
  } else {
    seplen = 1;
  }
  char *out = malloc(a.len + seplen + b.len + 1);
  assert(out != NULL);
  memcpy(out, a.data, a.len);
  if (seplen == 1) {
    out[a.len] = '/';
  }
  memcpy(out + a.len + seplen, b.data, b.len);
  out[a.len + seplen + b.len] = '\0';
  return out;
}
