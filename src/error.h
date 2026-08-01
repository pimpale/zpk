#ifndef error_h_INCLUDED
#define error_h_INCLUDED

#include <stdio.h>

#ifndef ERROR_MAX_PRINT_LENGTH
#define ERROR_MAX_PRINT_LENGTH 4096
#endif

#ifndef ERROR_APPNAME
#define ERROR_APPNAME "zpk"
#endif

typedef enum ErrSeverity {
  ERR_LEVEL_DEBUG = 1,
  ERR_LEVEL_INFO = 2,
  ERR_LEVEL_WARN = 3,
  ERR_LEVEL_ERROR = 4,
  ERR_LEVEL_FATAL = 5,
  ERR_LEVEL_UNKNOWN = 6,
} ErrSeverity;

typedef enum ErrVal {
  ERR_OK = 0,
  ERR_UNKNOWN = 1,
  ERR_NOTSUPPORTED = 2,
  ERR_UNSAFE = 3,
  ERR_BADARGS = 4,
  ERR_OUTOFDATE = 5,
  ERR_ALLOCFAIL = 6,
  ERR_MEMORY = 7,
  ERR_NOSUCHFILE = 8,
} ErrVal;

const char *levelstrerror(ErrSeverity level);

// Messages below this severity are dropped. Defaults to ERR_LEVEL_WARN; -v
// lowers it to ERR_LEVEL_INFO and -vv to ERR_LEVEL_DEBUG. Set by parse_args as
// soon as it sees the flag, so that config resolution logs at the right level.
extern ErrSeverity zpk_log_level;

#define UNUSED __attribute__((unused))
#define PANIC() exit(EXIT_FAILURE)

// Diagnostics always go to stderr, so that stdout carries only the data a
// command was asked to produce and stays safe to pipe.
#define LOG_ERROR(level, msg)                                                  \
  do {                                                                         \
    if ((level) >= zpk_log_level) {                                            \
      fprintf(stderr, "%s: %s: %s\n", ERROR_APPNAME, levelstrerror(level),     \
              msg);                                                            \
    }                                                                          \
  } while (0)

#define LOG_ERROR_ARGS(level, fmt, ...)                                        \
  do {                                                                         \
    if ((level) >= zpk_log_level) {                                            \
      char macro_message_formatted[ERROR_MAX_PRINT_LENGTH];                    \
      snprintf(macro_message_formatted, ERROR_MAX_PRINT_LENGTH, fmt,           \
               __VA_ARGS__);                                                   \
      fprintf(stderr, "%s: %s: %s\n", ERROR_APPNAME, levelstrerror(level),     \
              macro_message_formatted);                                        \
    }                                                                          \
  } while (0)
#endif // error_h_INCLUDED
