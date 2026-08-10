#ifndef oscompatlayer_h_INCLUDED
#define oscompatlayer_h_INCLUDED

#include "instances/vec_char_ptr.h"

typedef enum {
  PATH_TYPE_MISSING, // does not exist (or a parent component is missing)
  PATH_TYPE_FILE,    // regular file
  PATH_TYPE_DIR,     // directory
  PATH_TYPE_OTHER,   // exists but is neither (socket, device, ...); never
                     // reported on Windows, which only distinguishes
                     // directory vs not
  PATH_TYPE_ERROR,   // could not tell; errno is set
} path_type;

// classifies what path refers to on disk, following symlinks
path_type path_type_portable(const char* path);

const char* getenv_home_portable(void);
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
