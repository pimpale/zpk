#ifndef oscompatlayer_h_INCLUDED
#define oscompatlayer_h_INCLUDED

#include "instances/vec_char_ptr.h"

char* expandtilde(const char* input);
char* getcwd_portable(void);
int mkdir_portable(const char* path, int mode);
int rmdir_portable(const char* path);
int rename_portable(const char* oldpath, const char* newpath);
// appends the names of path's entries (excluding "." and "..", without any
// path prefix, strdup'd so the caller owns them) to out in unspecified order;
// the consumer sorts if it needs determinism. directories are included; the
// caller filters. returns -1 with errno set on failure, in which case entries
// read before the failure may remain in out.
int listdir_portable(const char* path, vec_char_ptr* out);

#endif // oscompatlayer_h_INCLUDED
