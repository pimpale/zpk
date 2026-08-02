#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "error.h"
#include "fsops.h"
#include "instances/llrb_path_indexdata.h"
#include "instances/vec_char_ptr.h"
#include "miniz/miniz.h"
#include "oscompatlayer.h"

// normalize the filename by dropping "." components and empty components
// (leading, doubled, and trailing slashes — though a single trailing slash is
// kept so directory entries stay recognizable). if the filename is bad (has a
// ".." component, contains a backslash, or normalizes to nothing) then return
// NULL. caller must free the returned string if it is not NULL
static char *normalize(const char *filename) {
  char *out = malloc(strlen(filename) + 1);
  size_t out_len = 0;
  // where the current component starts in out; rewinding to this drops the
  // component without disturbing the separator before it
  size_t comp_start = 0;
  enum { CS_START, CS_ONEDOT, CS_TWODOTS, CS_OTHER } state = CS_START;
  for (const char *p = filename;; p++) {
    char c = *p;
    if (c == '\0' || c == '/') {
      if (state == CS_TWODOTS) {
        free(out);
        return NULL;
      }
      if (state == CS_START || state == CS_ONEDOT) {
        // empty or "." component: drop it
        out_len = comp_start;
      } else if (c == '/') {
        out[out_len++] = '/';
        comp_start = out_len;
      }
      state = CS_START;
      if (c == '\0') {
        break;
      }
    } else if (c == '\\') {
      // forbid \\ because it might be a path traversal on windows.
      free(out);
      return NULL;
    } else {
      out[out_len++] = c;
      if (c == '.') {
        if (state == CS_START) {
          state = CS_ONEDOT;
        } else if (state == CS_ONEDOT) {
          state = CS_TWODOTS;
        } else {
          state = CS_OTHER;
        }
      } else {
        state = CS_OTHER;
      }
    }
  }
  if (out_len == 0) {
    free(out);
    return NULL;
  }
  out[out_len] = '\0';
  return out;
}

static bool endswith(const char *str, const char *suffix) {
  size_t len = strlen(str);
  size_t suflen = strlen(suffix);
  if (suflen > len) {
    return false;
  }
  return strcmp(str + (len - suflen), suffix) == 0;
}

// read the central directories of all the files and construct an index of all
// the files in it
// only_installed (mandatory for now) only builds the index with installed files
// (useful for ownership tests)
static llrb_path_indexdata *build_file_index(char *pkgs_path,
                                             bool only_installed) {

  // for now only installed works
  assert(only_installed);
  // first list files in the pkgs path:
  vec_char_ptr *entries = NULL;
  vec_char_ptr_new(&entries);
  if (listdir_portable(pkgs_path, entries) != 0) {
    LOG_ERROR_ARGS("index: unable to list files in %s: %s", pkgs_path,
                   strerror(errno));
    vec_char_ptr_delete(&entries);
    return NULL;
  }

  llrb_path_indexdata* index = NULL;
  llrb_path_indexdata_new(&index);

  for (size_t i = 0; i < vec_char_ptr_len(entries); i++) {
    char *entry = *vec_char_ptr_at(entries, i);
    // if entry doesn't end with ".zip", skip
    if (!endswith(entry, ".zip")) {
      continue;
    }
    // if entry ends with "INSTALLED.zip", skip
    if (only_installed && !endswith(entry, "_INSTALLED.zip")) {
      continue;
    }

    char *package_path = malloc(strlen(pkgs_path) + 1 + strlen(entry));
    sprintf(package_path, "%s/%s", pkgs_path, entry);
    FILE *f = fopen(package_path, "rb");
    if (f == NULL) {
      LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "index %s: could not open file: %s",
                     package_path, strerror(errno));
      free(package_path);
      continue;
    }

    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!mz_zip_reader_init_cfile(&zip, f, 0, 0)) {
      LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                     "index %s: could not open zip archive: %s", package_path,
                     mz_zip_get_error_string(mz_zip_get_last_error(&zip)));

      free(package_path);
      mz_zip_reader_end(&zip);
      fclose(f);
      return ERR_UNKNOWN;
    }

    // Iterate through all files in the zip archive
    mz_uint num_files = mz_zip_reader_get_num_files(&zip);
    for (mz_uint j = 0; j < num_files; j++) {
      mz_zip_archive_file_stat file_stat;
      if (!mz_zip_reader_file_stat(&zip, j, &file_stat)) {
        LOG_ERROR_ARGS(
            ERR_LEVEL_ERROR,
            "index %s: could not get file stat for file %u in zip: %s",
            package_path, j,
            mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
        continue;
      }

      // compute 


      if(!llrb_path_indexdata_get)
      llrb_path_indexdata_insert(index, )
    }
  }
}

static ErrVal ensure_directory_exists(char *package, char *path) {
  // path+1: path is always absolute (starts with '/'), so the first
  // component boundary belongs to the root and creating it would be
  // mkdir_portable(""), which fails with ENOENT rather than EEXIST.
  for (char *p = path + 1; *p != '\0'; p++) {
    if (*p == '/') {
      *p = '\0';
      int result = mkdir_portable(path, 0755);
      if (result == 0) {
        LOG_ERROR_ARGS(ERR_LEVEL_DEBUG, "install %s: created directory %s",
                       package, path);
      } else if (errno != EEXIST) {
        LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                       "install %s: could not create directory %s: %s", package,
                       path, strerror(errno));
        return ERR_UNKNOWN;
      }
      *p = '/';
    }
  }
  return ERR_OK;
}

void install_extract_file(char *sysroot, char *package,
                          mz_zip_archive_file_stat file_stat,
                          mz_zip_archive *zip, bool dry_run) {

  if (!file_stat.m_is_supported) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                   "uninstall %s: file %s is encrypted or uses an unsupported "
                   "compression method",
                   package, file_stat.m_filename);
    return;
  }

  mz_uint need =
      mz_zip_reader_get_filename(zip, file_stat.m_file_index, NULL, 0);
  char *name = malloc(need);
  mz_zip_reader_get_filename(zip, file_stat.m_file_index, name, need);
  char *normalized = normalize(name);
  if (normalized == NULL) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "uninstall %s: invalid filename %s",
                   package, name);
    free(name);
    return;
  }
  if (strcmp(normalized, name) != 0) {
    LOG_ERROR_ARGS(ERR_LEVEL_INFO, "uninstall %s: normalized filename %s to %s",
                   package, name, normalized);
  }
  free(name);

  char *target = malloc(strlen(sysroot) + 1 + strlen(normalized) + 1);
  sprintf(target, "%s/%s", sysroot, normalized);

  LOG_ERROR_ARGS(ERR_LEVEL_INFO, "uninstall %s: removing file %s to %s",
                 package, normalized, target);

  if (!dry_run) {
    if (file_stat.m_is_directory) {
      ensure_directory_exists(package, target);
    } else {
      ensure_directory_exists(package, target);
      if (!mz_zip_reader_extract_to_file(zip, file_stat.m_file_index, target,
                                         0)) {
        LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                       "uninstall %s: could not extract file %s to %s: %s",
                       package, normalized, target,
                       mz_zip_get_error_string(mz_zip_get_last_error(zip)));
      }
    }

    free(normalized);
    free(target);
  }
}

void install_extract_file(char *sysroot, char *package,
                          mz_zip_archive_file_stat file_stat,
                          mz_zip_archive *zip, bool dry_run) {

  if (!file_stat.m_is_supported) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                   "install %s: file %s is encrypted or uses an unsupported "
                   "compression method",
                   package, file_stat.m_filename);
    return;
  }

  mz_uint need =
      mz_zip_reader_get_filename(zip, file_stat.m_file_index, NULL, 0);
  char *name = malloc(need);
  mz_zip_reader_get_filename(zip, file_stat.m_file_index, name, need);
  char *normalized = normalize(name);
  if (normalized == NULL) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "install %s: invalid filename %s", package,
                   name);
    free(name);
    return;
  }
  if (strcmp(normalized, name) != 0) {
    LOG_ERROR_ARGS(ERR_LEVEL_INFO, "install %s: normalized filename %s to %s",
                   package, name, normalized);
  }
  free(name);

  char *target = malloc(strlen(sysroot) + 1 + strlen(normalized) + 1);
  sprintf(target, "%s/%s", sysroot, normalized);

  LOG_ERROR_ARGS(ERR_LEVEL_INFO, "install %s: extracting file %s to %s",
                 package, normalized, target);

  if (!dry_run) {
    if (file_stat.m_is_directory) {
      ensure_directory_exists(package, target);
    } else if (!mz_zip_reader_extract_to_file(zip, file_stat.m_file_index,
                                              target, 0)) {
      LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                     "install %s: could not extract file %s to %s: %s", package,
                     normalized, target,
                     mz_zip_get_error_string(mz_zip_get_last_error(zip)));
    }
  }

  free(normalized);
  free(target);
}

ErrVal install_package(ZpkConfiguration *pConf, char *package, bool dry_run) {
  char *package_path =
      malloc(strlen(pConf->pkgs_path) + 1 + strlen(package) + 1);
  sprintf(package_path, "%s/%s", pConf->pkgs_path, package);
  LOG_ERROR_ARGS(ERR_LEVEL_INFO, "install %s: located at %s", package,
                 package_path);
  FILE *f = fopen(package_path, "rb");
  if (f == NULL) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "install %s: could not open file %s: %s",
                   package, package_path, strerror(errno));
    free(package_path);
    return ERR_NOSUCHFILE;
  }

  mz_zip_archive zip;
  mz_zip_zero_struct(&zip);
  if (!mz_zip_reader_init_cfile(&zip, f, 0, 0)) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                   "install %s: could not open zip archive %s: %s", package,
                   package_path,
                   mz_zip_get_error_string(mz_zip_get_last_error(&zip)));

    free(package_path);
    mz_zip_reader_end(&zip);
    fclose(f);
    return ERR_UNKNOWN;
  }

  // Iterate through all files in the zip archive
  mz_uint num_files = mz_zip_reader_get_num_files(&zip);
  for (mz_uint i = 0; i < num_files; i++) {
    mz_zip_archive_file_stat file_stat;
    if (!mz_zip_reader_file_stat(&zip, i, &file_stat)) {
      LOG_ERROR_ARGS(
          ERR_LEVEL_ERROR,
          "install %s: could not get file stat for file %u in zip: %s", package,
          i, mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
      continue;
    }
    install_extract_file(pConf->sysroot, package, file_stat, &zip, dry_run);
  }

  mz_zip_reader_end(&zip);
  fclose(f);
  free(package_path);
  return ERR_OK;
}

ErrVal uninstall_package(ZpkConfiguration *pConf, char *package, bool dry_run) {
  char *package_path =
      malloc(strlen(pConf->pkgs_path) + 1 + strlen(package) + 1);
  sprintf(package_path, "%s/%s", pConf->pkgs_path, package);
  LOG_ERROR_ARGS(ERR_LEVEL_INFO, "uninstall %s: located at %s", package,
                 package_path);
  FILE *f = fopen(package_path, "rb");
  if (f == NULL) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "uninstall %s: could not open file %s: %s",
                   package, package_path, strerror(errno));
    free(package_path);
    return ERR_NOSUCHFILE;
  }

  mz_zip_archive zip;
  mz_zip_zero_struct(&zip);
  if (!mz_zip_reader_init_cfile(&zip, f, 0, 0)) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                   "uninstall %s: could not open zip archive %s: %s", package,
                   package_path,
                   mz_zip_get_error_string(mz_zip_get_last_error(&zip)));

    free(package_path);
    mz_zip_reader_end(&zip);
    fclose(f);
    return ERR_UNKNOWN;
  }

  // Iterate through all files in the zip archive
  // we do this in REVERSE order to ensure that we delete files before their
  // parent directories
  mz_uint num_files = mz_zip_reader_get_num_files(&zip);
  for (mz_uint i = num_files; i > 0; i--) {
    mz_uint index = i - 1;
    mz_zip_archive_file_stat file_stat;
    if (!mz_zip_reader_file_stat(&zip, index, &file_stat)) {
      LOG_ERROR_ARGS(
          ERR_LEVEL_ERROR,
          "uninstall %s: could not get file stat for file %u in zip: %s",
          package, index, mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
      continue;
    }
    uninstall_remove_file(pConf->sysroot, package, file_stat, &zip, dry_run);
  }

  mz_zip_reader_end(&zip);
  fclose(f);
  free(package_path);
  return ERR_OK;
}
