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
  char *cwd = _getcwd(NULL, 0);
#else
  char *cwd = getcwd(NULL, 0);
#endif
  if (cwd == NULL) {
    LOG_ERROR_ARGS(ERR_LEVEL_FATAL,
                   "could not get current working directory: %s",
                   strerror(errno));
    PANIC();
  }
  return cwd;
}

int mkdir_portable(const char *path, int mode) {
#if defined(_WIN32) || defined(_WIN64)
  (void)mode; // mode is ignored on Windows
  return _mkdir(path);
#else
  return mkdir(path, mode);
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

int listdir_portable(const char *path, vec_char_ptr *out) {
#if defined(_WIN32) || defined(_WIN64)
  // FindFirstFile takes a pattern, not a directory
  char *pattern = malloc(strlen(path) + 3);
  if (pattern == NULL) {
    LOG_ERROR(ERR_LEVEL_FATAL,
              "could not allocate memory for directory listing");
    PANIC();
  }
  sprintf(pattern, "%s/*", path);
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
      listdir_push(out, find_data.cFileName);
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
    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
      listdir_push(out, entry->d_name);
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
