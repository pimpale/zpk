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
// path prefix, strdup'd so the caller owns them) to out_files and out_dirs in
// unspecified order; either vector may be NULL to skip entries of that kind.
// symlinks always count as files, never directories, even when they point at
// one (following them risks traversal loops and over-deletion); anything that
// is not a directory counts as a file. the consumer sorts if it needs
// determinism. returns -1 with errno set on failure, in which case entries
// read before the failure may remain in the vectors.
int listdir_portable(const char* path, vec_char_ptr* out_files,
                     vec_char_ptr* out_dirs);

#endif // oscompatlayer_h_INCLUDED
