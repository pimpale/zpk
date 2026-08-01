#include "error.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "oscompatlayer.h"

// allocates a new string with the tilde expanded to the user's home directory,
// if applicable. only expands tilde at the start of the string, and only if
// followed by a slash or end of string. errors are fatal
char *expandtilde(const char *input) {
  if (input[0] != '~' || (input[1] != '/' && input[1] != '\0')) {
    return strdup(input);
  }

#if defined(_WIN32) || defined(_WIN64)
  const char *home = getenv("USERPROFILE");
#else
  const char *home = getenv("HOME");
#endif

  if (home == NULL) {
    LOG_ERROR(ERR_LEVEL_FATAL,
              "could not determine home directory for tilde expansion");
    PANIC();
  }
  size_t home_len = strlen(home);
  size_t input_len = strlen(input);
  char *expanded = (char *)malloc(home_len + input_len);
  if (expanded == NULL) {
    LOG_ERROR(ERR_LEVEL_FATAL, "could not allocate memory for tilde expansion");
    PANIC();
  }
  sprintf(expanded, "%s%s", home, input + 1);
  return expanded;
}

char *getcwd_portable(void) {
#if defined(_WIN32) || defined(_WIN64)
  char *cwd = _getcwd(NULL, 0);
#else
  char *cwd = getcwd(NULL, 0);
#endif
  if (cwd == NULL) {
    LOG_ERROR_ARGS(ERR_LEVEL_FATAL,
                   "could not get current working directory: %s",
                   strerror(errno));
    PANIC();
  }
  return cwd;
}
