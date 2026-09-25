#ifndef pathutils_h_INCLUDED
#define pathutils_h_INCLUDED

#include "instances/slice_uint8_t.h"

char *expandtilde(const char *input);
char *normalize(const char *path);
char *basename_m(char *input);
bool startswith(const char *str, const char *prefix);
bool endswith(const char *str, const char *suffix);
char *joinstr2(const char *s1, const char *s2);
char *joinpath(const char *s1, const char* s2);
slice_uint8_t joinpath_slice(slice_uint8_t a, slice_uint8_t b);
char* joinpath_slice_str(slice_uint8_t a, slice_uint8_t b);
char *replacesuf(const char *input, const char* oldsuf, const char* newsuf);
const char* maybesep(const char* in);
#endif // pathutils_h_INCLUDED
