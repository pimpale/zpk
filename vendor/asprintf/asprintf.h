
/**
 * `asprintf.h' - asprintf.c
 *
 * copyright (c) 2014 joseph werle <joseph.werle@gmail.com>
 */

#ifndef HAVE_ASPRINTF
#ifndef ASPRINTF_H
#define ASPRINTF_H 1

#include <stdarg.h>

#if __has_c_attribute(gnu::format)
#define ASPRINTF_FORMAT(fmt_index, first_arg)                                  \
  [[gnu::format(printf, fmt_index, first_arg)]]
#endif
#ifndef ASPRINTF_FORMAT
#if defined(__GNUC__)
#define ASPRINTF_FORMAT(fmt_index, first_arg)                                  \
  __attribute__((format(printf, fmt_index, first_arg)))
#else
#define ASPRINTF_FORMAT(fmt_index, first_arg)
#endif
#endif

/**
 * Sets `char **' pointer to be a buffer
 * large enough to hold the formatted string
 * accepting a `va_list' args of variadic
 * arguments.
 */

ASPRINTF_FORMAT(2, 0) int
vasprintf (char **, const char *, va_list);

/**
 * Sets `char **' pointer to be a buffer
 * large enough to hold the formatted
 * string accepting `n' arguments of
 * variadic arguments.
 */

ASPRINTF_FORMAT(2, 3) int
asprintf (char **, const char *, ...);

#endif
#endif
