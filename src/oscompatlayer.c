// this file is the platform shim: the rest of src/ is strict ISO C, and every
// non-ISO interface the program needs is reached from here. POSIX.1-2008
// §2.2.1 requires the conformance level to be requested before any header is
// included, so this must stay above the includes below.
#if !defined(_WIN32) && !defined(_WIN64)
#define _POSIX_C_SOURCE 200809L
#endif

#include "error.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "asprintf/asprintf.h"
#include "oscompatlayer.h"

const char *getenv_home_portable(void) {
#if defined(_WIN32) || defined(_WIN64)
  const char *home = getenv("USERPROFILE");
#else
  const char *home = getenv("HOME");
#endif
  return home;
}

char *getcwd_portable(void) {
#if defined(_WIN32) || defined(_WIN64)
  // the Windows CRT allocates for us when given a NULL buffer
  char *cwd = _getcwd(NULL, 0);
  if (cwd == NULL) {
    LOG_ERROR_ARGS(ERR_LEVEL_FATAL,
                   "could not get current working directory: %s",
                   strerror(errno));
    PANIC();
  }
  return cwd;
#else
  // POSIX leaves getcwd(NULL, 0) unspecified (allocating is a glibc/BSD
  // extension), so grow our own buffer until the path fits. ERANGE is the
  // only failure worth retrying; anything else is fatal
  for (size_t size = 256;; size *= 2) {
    char *cwd = malloc(size);
    if (cwd == NULL) {
      LOG_ERROR(ERR_LEVEL_FATAL,
                "could not allocate memory for current working directory");
      PANIC();
    }
    if (getcwd(cwd, size) != NULL) {
      return cwd;
    }
    int saved_errno = errno;
    free(cwd);
    if (saved_errno != ERANGE) {
      LOG_ERROR_ARGS(ERR_LEVEL_FATAL,
                     "could not get current working directory: %s",
                     strerror(saved_errno));
      PANIC();
    }
  }
#endif
}

int mkdir_portable(const char *path, int mode) {
#if defined(_WIN32) || defined(_WIN64)
  (void)mode; // mode is ignored on Windows
  return _mkdir(path);
#else
  // the portable signature takes int, since mode_t only exists on POSIX
  return mkdir(path, (mode_t)mode);
#endif
}

int rmdir_portable(const char *path) {
#if defined(_WIN32) || defined(_WIN64)
  return _rmdir(path);
#else
  return rmdir(path);
#endif
}

#if defined(_WIN32) || defined(_WIN64)
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
#endif

path_type path_type_portable(const char *path) {
#if defined(_WIN32) || defined(_WIN64)
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
#else
  struct stat st;
  if (stat(path, &st) != 0) {
    // ENOTDIR: a parent component exists but is a file, so path can't exist
    return errno == ENOENT || errno == ENOTDIR ? PATH_TYPE_MISSING
                                               : PATH_TYPE_ERROR;
  }
  if (S_ISDIR(st.st_mode)) {
    return PATH_TYPE_DIR;
  }
  if (S_ISREG(st.st_mode)) {
    return PATH_TYPE_FILE;
  }
  return PATH_TYPE_OTHER;
#endif
}

// like POSIX rename: atomically replaces newpath if it already exists.
// (the Windows CRT rename fails on an existing target, so we go through
// MoveFileEx instead)
int rename_portable(const char *oldpath, const char *newpath) {
#if defined(_WIN32) || defined(_WIN64)
  if (MoveFileExA(oldpath, newpath, MOVEFILE_REPLACE_EXISTING)) {
    return 0;
  }
  set_errno_from_win32(GetLastError());
  return -1;
#else
  return rename(oldpath, newpath);
#endif
}

// strdups an entry name into out; allocation failure is fatal, like the rest
// of this file
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
#if defined(_WIN32) || defined(_WIN64)
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
#else
  DIR *dir = opendir(path);
  if (dir == NULL) {
    return -1;
  }
  // readdir returns NULL for both end-of-directory and failure; only errno
  // distinguishes them
  errno = 0;
  for (struct dirent *entry = readdir(dir); entry != NULL;
       entry = readdir(dir)) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      errno = 0;
      continue;
    }
    // classify with lstat (not stat, so symlinks stay files). dirent's d_type
    // would save the syscall, but it is a BSD extension no POSIX conformance
    // level exposes, and it is absent entirely on some systems
    char *full;
    if (asprintf(&full, "%s/%s", path, entry->d_name) < 0) {
      LOG_ERROR(ERR_LEVEL_FATAL,
                "could not allocate memory for directory listing");
      PANIC();
    }
    struct stat st;
    int rc = lstat(full, &st);
    free(full);
    if (rc != 0) {
      // entry vanished between readdir and lstat; skip it
      errno = 0;
      continue;
    }
    vec_char_ptr *dest = S_ISDIR(st.st_mode) ? out_dirs : out_files;
    if (dest != NULL) {
      listdir_push(dest, entry->d_name);
    }
    errno = 0;
  }
  if (errno != 0) {
    int saved_errno = errno;
    closedir(dir);
    errno = saved_errno;
    return -1;
  }
  closedir(dir);
#endif
  return 0;
}
