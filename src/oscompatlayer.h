#ifndef oscompatlayer_h_INCLUDED
#define oscompatlayer_h_INCLUDED

// the actual implementation is in platsrc/

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

// mallocs
char* getenv_home_portable(void);
char* getcwd_portable(void);
int mkdir_portable(const char* path, int mode);
int rmdir_portable(const char* path);
int rename_portable(const char* oldpath, const char* newpath);
char* abspath_portable(const char* path);
int listdir_portable(const char* path, vec_char_ptr* out_files,
                     vec_char_ptr* out_dirs);
                     
#endif // oscompatlayer_h_INCLUDED
