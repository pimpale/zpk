#include "pathutils.h"
#include <assert.h>
#include <stddefer.h>
#include <stdlib.h>
#include <string.h>

#include "error.h"
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
    LOG_ERROR(ERR_LEVEL_FATAL,
              "could not determine home directory for tilde expansion");
    PANIC();
  }
  char *expanded = joinstr2(home, input + 1);
  if (expanded == NULL) {
    LOG_ERROR(ERR_LEVEL_FATAL, "could not allocate memory for tilde expansion");
    PANIC();
  }
  return expanded;
}

// normalize the filename by dropping "." components and empty components
// (leading, doubled, and trailing slashes). if the filename is bad (has a
// ".." component, contains a backslash, or normalizes to nothing) then return
// NULL. caller must free the returned string if it is not NULL
char *normalize(const char *filename) {
  char *out = malloc(strlen(filename) + 1);
  size_t out_len = 0;
  // where the current component starts in out; rewinding to this drops the
  // component without disturbing the separator before it
  size_t comp_start = 0;
  enum { CS_START, CS_ONEDOT, CS_TWODOTS, CS_OTHER } state = CS_START;
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

char *joinstr2(const char *s1, const char *s2) {
  size_t len1 = strlen(s1);
  size_t len2 = strlen(s2);
  char *result = malloc(len1 + len2 + 1);

  if (result == NULL)
    return NULL;

  strcpy(result, s1);
  strcat(result, s2);
  return result;
}

char *joinstr3(const char *s1, const char *s2, const char *s3) {
  size_t len1 = strlen(s1);
  size_t len2 = strlen(s2);
  size_t len3 = strlen(s3);
  char *result = malloc(len1 + len2 + len3 + 1);

  if (result == NULL)
    return NULL;

  strcpy(result, s1);
  strcat(result, s2);
  strcat(result, s3);
  return result;
}
