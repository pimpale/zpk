#include "pathutils.h"
#include <stdlib.h>
#include <string.h>

#include "asprintf/asprintf.h"
#include "error.h"
#include "oscompatlayer.h"

// allocates a new string with the tilde expanded to the user's home directory,
// if applicable. only expands tilde at the start of the string, and only if
// followed by a slash or end of string. errors are fatal
char *expandtilde(const char *input) {
  if (input[0] != '~' || (input[1] != '/' && input[1] != '\0')) {
    return strdup(input);
  }

  const char* home = getenv_home_portable();

  if (home == NULL) {
    LOG_ERROR(ERR_LEVEL_FATAL,
              "could not determine home directory for tilde expansion");
    PANIC();
  }
  char *expanded;
  if (asprintf(&expanded, "%s%s", home, input + 1) < 0) {
    LOG_ERROR(ERR_LEVEL_FATAL, "could not allocate memory for tilde expansion");
    PANIC();
  }
  return expanded;
}

char *cleanpath(const char *path) {
  size_t len = strlen(path);
  // output never exceeds the input except for the "" -> "." case
  char *out = malloc(len + 2);
  if (out == NULL) {
    LOG_ERROR(ERR_LEVEL_FATAL, "could not allocate memory for path cleaning");
    PANIC();
  }
  bool absolute = path[0] == '/';
  size_t out_len = 0;
  if (absolute) {
    out[out_len++] = '/';
  }
  // components already emitted that a ".." may pop (excludes any leading
  // ".." kept in a relative path)
  size_t poppable = 0;
  // the fixed prefix popping must never eat into: the root slash if absolute
  size_t base = absolute ? 1 : 0;
  for (const char *p = path; *p != '\0';) {
    while (*p == '/') {
      p++;
    }
    if (*p == '\0') {
      break;
    }
    const char *comp = p;
    while (*p != '\0' && *p != '/') {
      p++;
    }
    size_t comp_len = (size_t)(p - comp);
    if (comp_len == 1 && comp[0] == '.') {
      continue;
    }
    if (comp_len == 2 && comp[0] == '.' && comp[1] == '.') {
      if (poppable > 0) {
        while (out_len > base && out[out_len - 1] != '/') {
          out_len--;
        }
        if (out_len > base) {
          out_len--; // drop the separator too
        }
        poppable--;
      } else if (!absolute) {
        // nothing left to pop: a relative path keeps the ".."
        if (out_len > 0) {
          out[out_len++] = '/';
        }
        out[out_len++] = '.';
        out[out_len++] = '.';
      }
      // absolute with nothing to pop: ".." at the root stays at the root
      continue;
    }
    if (out_len > base) {
      out[out_len++] = '/';
    }
    memcpy(out + out_len, comp, comp_len);
    out_len += comp_len;
    poppable++;
  }
  if (out_len == 0) {
    out[out_len++] = absolute ? '/' : '.';
  }
  out[out_len] = '\0';
  return out;
}
