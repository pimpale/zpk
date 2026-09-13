#ifndef fileutils_h_INCLUDED
#define fileutils_h_INCLUDED

#include "instances/vec_uint8_t.h"

int copy_file(const char *from, const char *to);

int read_file(const char* path, vec_uint8_t* out);

#endif // fileutils_h_INCLUDED
