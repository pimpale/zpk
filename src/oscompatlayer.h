#ifndef oscompatlayer_h_INCLUDED
#define oscompatlayer_h_INCLUDED

// the actual implementation is in platsrc/
#include <stdio.h>

#include "instances/vec_char_ptr.h"

typedef enum {
  PATH_TYPE_MISSING, // does not exist (or a parent component is missing)
  PATH_TYPE_FILE,    // regular file
  PATH_TYPE_DIR,     // directory
  PATH_TYPE_OTHER,   // exists but is neither (socket, device, ...)
  PATH_TYPE_ERROR,   // could not tell; errno is set
} PathType;

// follows symlinks
PathType path_type_portable(const char *path);

// mallocs
char *getenv_home_portable(void);
char *getcwd_portable(void);
int mkdir_portable(const char *path, int mode);
int rmdir_portable(const char *path);
int rename_portable(const char *oldpath, const char *newpath);
char *abspath_portable(const char *path);
int listdir_portable(const char *path, vec_char_ptr *out_files, vec_char_ptr *out_dirs);
// Opens in binary read or write mode without preventing a rename while open.
// Only "rb" and "wb" are supported; other modes fail with EINVAL.
FILE *fopen_nolock_portable(const char *restrict filename, const char *restrict modes);

#endif // oscompatlayer_h_INCLUDED
