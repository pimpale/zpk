#include "error.h"
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <direct.h>
#include <windows.h>

#include "asprintf/asprintf.h"
#include "oscompatlayer.h"

// every path this shim hands back speaks '/' like the rest of the program;
// the Win32 API accepts either separator, so nothing is lost by folding
static void backslashes_to_slashes(char *path) {
  for (char *p = path; *p != '\0'; p++) {
    if (*p == '\\') {
      *p = '/';
    }
  }
}

char *getenv_home_portable(void) {
  char* home = strdup(getenv("USERPROFILE"));
  backslashes_to_slashes(home);
  return home;
}

char *getcwd_portable(void) {
  // the Windows CRT allocates for us when given a NULL buffer
  char *cwd = _getcwd(NULL, 0);
  if (cwd == NULL) {
    LOG_ERROR_ARGS(ERR_LEVEL_FATAL,
                   "could not get current working directory: %s",
                   strerror(errno));
    PANIC();
  }
  backslashes_to_slashes(cwd);
  return cwd;
}

int mkdir_portable(const char *path, int mode) {
  (void)mode; // mode is ignored on Windows
  return _mkdir(path);
}

int rmdir_portable(const char *path) {
  return _rmdir(path);
}

// maps a Win32 error code onto errno so callers can use strerror as usual
static void set_errno_from_win32(DWORD error) {
  switch (error) {
  case ERROR_FILE_NOT_FOUND:
  case ERROR_PATH_NOT_FOUND:
    errno = ENOENT;
    break;
  case ERROR_ACCESS_DENIED:
  case ERROR_SHARING_VIOLATION:
    errno = EACCES;
    break;
  case ERROR_ALREADY_EXISTS:
  case ERROR_FILE_EXISTS:
    errno = EEXIST;
    break;
  case ERROR_NOT_SAME_DEVICE:
    errno = EXDEV;
    break;
  default:
    errno = EINVAL;
    break;
  }
}

path_type path_type_portable(const char *path) {
  DWORD attrs = GetFileAttributesA(path);
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
      return PATH_TYPE_MISSING;
    }
    set_errno_from_win32(error);
    return PATH_TYPE_ERROR;
  }
  return (attrs & FILE_ATTRIBUTE_DIRECTORY) ? PATH_TYPE_DIR : PATH_TYPE_FILE;
}

// like POSIX rename: atomically replaces newpath if it already exists.
// (the Windows CRT rename fails on an existing target, so we go through
// MoveFileEx instead)
int rename_portable(const char *oldpath, const char *newpath) {
  if (MoveFileExA(oldpath, newpath, MOVEFILE_REPLACE_EXISTING)) {
    return 0;
  }
  set_errno_from_win32(GetLastError());
  return -1;
}

static void listdir_push(vec_char_ptr *out, const char *name) {
  char *copy = strdup(name);
  if (copy == NULL) {
    LOG_ERROR(ERR_LEVEL_FATAL,
              "could not allocate memory for directory listing");
    PANIC();
  }
  vec_char_ptr_push(out, &copy);
}

int listdir_portable(const char *path, vec_char_ptr *out_files,
                     vec_char_ptr *out_dirs) {
  // FindFirstFile takes a pattern, not a directory
  char *pattern;
  if (asprintf(&pattern, "%s/*", path) < 0) {
    LOG_ERROR(ERR_LEVEL_FATAL,
              "could not allocate memory for directory listing");
    PANIC();
  }
  WIN32_FIND_DATAA find_data;
  HANDLE handle = FindFirstFileA(pattern, &find_data);
  free(pattern);
  if (handle == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    // no entries at all (not even "."): an empty match, not a failure
    if (error == ERROR_FILE_NOT_FOUND) {
      return 0;
    }
    set_errno_from_win32(error);
    return -1;
  }
  do {
    if (strcmp(find_data.cFileName, ".") != 0 &&
        strcmp(find_data.cFileName, "..") != 0) {
      // reparse points (symlinks, junctions) count as files even when the
      // directory attribute is also set
      bool is_dir =
          (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
          (find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
      vec_char_ptr *dest = is_dir ? out_dirs : out_files;
      if (dest != NULL) {
        listdir_push(dest, find_data.cFileName);
      }
    }
  } while (FindNextFileA(handle, &find_data));
  DWORD error = GetLastError();
  FindClose(handle);
  if (error != ERROR_NO_MORE_FILES) {
    set_errno_from_win32(error);
    return -1;
  }
  return 0;
}

char *abspath_portable(const char *path) {
  char *full = _fullpath(NULL, path, 0);
  if (full == NULL) {
    LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "could not resolve absolute path of %s",
                   path);
    PANIC();
  }
  backslashes_to_slashes(full);
  return full;
}
