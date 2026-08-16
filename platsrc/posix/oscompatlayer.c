#define _POSIX_C_SOURCE 200809L

#include "error.h"
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "asprintf/asprintf.h"
#include "oscompatlayer.h"


char *getenv_home_portable(void) {
  return strdup(getenv("HOME"));
}

char *getcwd_portable(void) {
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
}

int mkdir_portable(const char *path, int mode) {
  return mkdir(path, (mode_t)mode);
}

int rmdir_portable(const char *path) {
  return rmdir(path);
}

path_type path_type_portable(const char *path) {
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
}

int rename_portable(const char *oldpath, const char *newpath) {
  return rename(oldpath, newpath);
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
  return 0;
}

// only the POSIX abspath_portable needs this; Windows delegates to _fullpath
static char *cleanpath(const char *path) {
  size_t len = strlen(path);
  // output never exceeds the input except for the "" -> "." case
  char *out = malloc(len + 2);
  if (out == NULL) {
    LOG_ERROR(ERR_LEVEL_FATAL, "could not allocate memory for path cleaning");
    PANIC();
  }
  bool absolute = path[0] == '/';
  size_t out_len = 0;
  if (absolute) {
    out[out_len++] = '/';
  }
  // components already emitted that a ".." may pop (excludes any leading
  // ".." kept in a relative path)
  size_t poppable = 0;
  // the fixed prefix popping must never eat into: the root slash if absolute
  size_t base = absolute ? 1 : 0;
  for (const char *p = path; *p != '\0';) {
    while (*p == '/') {
      p++;
    }
    if (*p == '\0') {
      break;
    }
    const char *comp = p;
    while (*p != '\0' && *p != '/') {
      p++;
    }
    size_t comp_len = (size_t)(p - comp);
    if (comp_len == 1 && comp[0] == '.') {
      continue;
    }
    if (comp_len == 2 && comp[0] == '.' && comp[1] == '.') {
      if (poppable > 0) {
        while (out_len > base && out[out_len - 1] != '/') {
          out_len--;
        }
        if (out_len > base) {
          out_len--; // drop the separator too
        }
        poppable--;
      } else if (!absolute) {
        // nothing left to pop: a relative path keeps the ".."
        if (out_len > 0) {
          out[out_len++] = '/';
        }
        out[out_len++] = '.';
        out[out_len++] = '.';
      }
      // absolute with nothing to pop: ".." at the root stays at the root
      continue;
    }
    if (out_len > base) {
      out[out_len++] = '/';
    }
    memcpy(out + out_len, comp, comp_len);
    out_len += comp_len;
    poppable++;
  }
  if (out_len == 0) {
    out[out_len++] = absolute ? '/' : '.';
  }
  out[out_len] = '\0';
  return out;
}

char *abspath_portable(const char *path) {
  if (path[0] == '/') {
    return cleanpath(path);
  }
  char *cwd = getcwd_portable();
  char *joined;
  if (asprintf(&joined, "%s/%s", cwd, path) < 0) {
    LOG_ERROR(ERR_LEVEL_FATAL,
              "could not allocate memory for absolute path resolution");
    PANIC();
  }
  free(cwd);
  char *cleaned = cleanpath(joined);
  free(joined);
  return cleaned;
}
