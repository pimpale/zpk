#include <assert.h>
#include <errno.h>
#include <stddefer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apkver/apkver.h"
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

// add a zip file to an existing index
static void add_zip_file_index(llrb_path_indexdata *index, const char *package,
                               char *package_path) {
  FILE *f = fopen(package_path, "rb");
  if (f == NULL) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "index %s: could not open file: %s",
                   package_path, strerror(errno));
    return;
  }
  defer fclose(f);

  mz_zip_archive zip;
  mz_zip_zero_struct(&zip);
  defer mz_zip_reader_end(&zip);
  if (!mz_zip_reader_init_cfile(&zip, f, 0, 0)) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "index %s: could not open zip archive: %s",
                   package_path,
                   mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    // a damaged package shouldn't hide every other package's claims
    return;
  }

  // Iterate through all files in the zip archive
  mz_uint num_files = mz_zip_reader_get_num_files(&zip);
  for (mz_uint j = 0; j < num_files; j++) {
    mz_zip_archive_file_stat file_stat;
    if (!mz_zip_reader_file_stat(&zip, j, &file_stat)) {
      LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                     "index %s: could not get file stat for file %u in zip: %s",
                     package_path, j,
                     mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
      continue;
    }

    mz_uint need = mz_zip_reader_get_filename(&zip, j, NULL, 0);
    char *name = malloc(need);
    defer free(name);
    mz_zip_reader_get_filename(&zip, j, name, need);
    char *normalized = normalize(name);
    if (normalized == NULL) {
      LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "index %s: invalid filename %s",
                     package_path, name);
      continue;
    }

    // omit trailing / for normalization purposes
    size_t normalized_len = strlen(normalized);
    if (normalized[normalized_len - 1] == '/') {
      normalized[normalized_len - 1] = '\0';
    }

    IndexData *data = NULL;
    if (!llrb_path_indexdata_get_ref(index, &normalized, &data)) {
      IndexData fresh;
      vec_IndexDataEntry_init(&fresh.claims);
      // the llrb now owns `normalized`
      llrb_path_indexdata_insert(index, &normalized, &fresh);
      llrb_path_indexdata_get_ref(index, &normalized, &data);
    } else {
      // path already exists as key
      free(normalized);
    }
    IndexDataEntry claim = {
        .package = strdup(package),
        .is_directory = file_stat.m_is_directory,
        .crc32 = file_stat.m_crc32,
    };
    vec_IndexDataEntry_push(&data->claims, &claim);
  }
}

// read the central directories of all the files and construct an index of all
// the files in it
// only_installed (mandatory for now) only builds the index with installed files
// (useful for ownership tests)
void build_file_index(llrb_path_indexdata *index, char *pkgs_path) {
  llrb_path_indexdata_new(index);
  // first list files in the pkgs path:
  vec_char_ptr entries;
  vec_char_ptr_init(&entries);
  defer vec_char_ptr_delete_and_freeowned(&entries);

  if (listdir_portable(pkgs_path, &entries) != 0) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "index: unable to list files in %s: %s",
                   pkgs_path, strerror(errno));
    return;
  }

  for (size_t i = 0; i < vec_char_ptr_len(&entries); i++) {
    char *entry = *vec_char_ptr_at(&entries, i);
    if (!endswith(entry, ".zip")) {
      continue;
    }
    char *package_path = malloc(strlen(pkgs_path) + 1 + strlen(entry) + 1);
    sprintf(package_path, "%s/%s", pkgs_path, entry);
    add_zip_file_index(index, entry, package_path);
    free(package_path);
  }
}

void delete_file_index(llrb_path_indexdata *index) {
  llrb_path_indexdata_iter iter;
  llrb_path_indexdata_iter_begin(index, &iter);
  char_ptr key;
  IndexData data;
  while (llrb_path_indexdata_iter_next(&iter, &key, &data)) {
    for (size_t i = 0; i < vec_IndexDataEntry_len(&data.claims); i++) {
      free(vec_IndexDataEntry_at(&data.claims, i)->package);
    }
    vec_IndexDataEntry_delete(&data.claims);
    free(key);
  }
  llrb_path_indexdata_delete(index);
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

void uninstall_remove_file(char *sysroot, char *package,
                           mz_zip_archive_file_stat file_stat,
                           mz_zip_archive *zip, bool dry_run) {

  mz_uint need =
      mz_zip_reader_get_filename(zip, file_stat.m_file_index, NULL, 0);
  char *name = malloc(need);
  defer free(name);
  mz_zip_reader_get_filename(zip, file_stat.m_file_index, name, need);
  char *normalized = normalize(name);
  if (normalized == NULL) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "uninstall %s: invalid filename %s",
                   package, name);
    return;
  }
  if (strcmp(normalized, name) != 0) {
    LOG_ERROR_ARGS(ERR_LEVEL_INFO, "uninstall %s: normalized filename %s to %s",
                   package, name, normalized);
  }

  char *target = malloc(strlen(sysroot) + 1 + strlen(normalized) + 1);
  sprintf(target, "%s/%s", sysroot, normalized);

  LOG_ERROR_ARGS(ERR_LEVEL_INFO, "uninstall %s: removing file %s (at %s)",
                 package, normalized, target);

  if (!dry_run) {
    if (file_stat.m_is_directory) {
      // fails harmlessly if the directory is shared with another package or
      // still holds user files
      rmdir_portable(target);
    } else if (remove(target) != 0) {
      LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                     "uninstall %s: could not remove file %s (at %s): %s",
                     package, normalized, target, strerror(errno));
    }
  }

  free(normalized);
  free(target);
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
  defer free(name);
  mz_zip_reader_get_filename(zip, file_stat.m_file_index, name, need);
  char *normalized = normalize(name);
  if (normalized == NULL) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "install %s: invalid filename %s", package,
                   name);
    return;
  }
  if (strcmp(normalized, name) != 0) {
    LOG_ERROR_ARGS(ERR_LEVEL_INFO, "install %s: normalized filename %s to %s",
                   package, name, normalized);
  }

  char *target = malloc(strlen(sysroot) + 1 + strlen(normalized) + 1);
  sprintf(target, "%s/%s", sysroot, normalized);

  LOG_ERROR_ARGS(ERR_LEVEL_INFO, "install %s: extracting file %s to %s",
                 package, normalized, target);

  if (!dry_run) {
    if (file_stat.m_is_directory) {
      ensure_directory_exists(package, target);
    } else {
      // create parents even when the archive has no directory entries or
      // lists them out of order
      ensure_directory_exists(package, target);
      if (!mz_zip_reader_extract_to_file(zip, file_stat.m_file_index, target,
                                         0)) {
        LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                       "install %s: could not extract file %s to %s: %s",
                       package, normalized, target,
                       mz_zip_get_error_string(mz_zip_get_last_error(zip)));
      }
    }
  }

  free(normalized);
  free(target);
}

ErrVal install_package(
    // file index (for file conflict identification)
    llrb_path_indexdata *index,
    // user given package name (For logging)
    char *package,
    // zip file to install
    char *package_path,
    // where to install
    char *sysroot,
    // don't actually change FS
    bool dry_run) {
  FILE *f = fopen(package_path, "rb");
  if (f == NULL) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "install %s: could not open file %s: %s",
                   package, package_path, strerror(errno));
    return ERR_NOSUCHFILE;
  }
  defer fclose(f);

  mz_zip_archive zip;
  mz_zip_zero_struct(&zip);
  defer mz_zip_reader_end(&zip);
  if (!mz_zip_reader_init_cfile(&zip, f, 0, 0)) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                   "install %s: could not open zip archive %s: %s", package,
                   package_path,
                   mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    return ERR_UNKNOWN;
  }

  // Test for conflicts
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
    install_extract_file(sysroot, package, file_stat, &zip, dry_run);
  }

  // Install
  for (mz_uint i = 0; i < num_files; i++) {
    mz_zip_archive_file_stat file_stat;
    if (!mz_zip_reader_file_stat(&zip, i, &file_stat)) {
      LOG_ERROR_ARGS(
          ERR_LEVEL_ERROR,
          "install %s: could not get file stat for file %u in zip: %s", package,
          i, mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
      continue;
    }
    install_extract_file(sysroot, package, file_stat, &zip, dry_run);
  }
  return ERR_OK;
}

ErrVal uninstall_package(
    // file index (for file conflict identification)
    llrb_path_indexdata *index,
    // user given package name (For logging)
    char *package,
    // zip file to install
    char *package_path,
    // where to install
    char *sysroot,
    // don't actually change FS
    bool dry_run) {
  FILE *f = fopen(package_path, "rb");
  if (f == NULL) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "uninstall %s: could not open file %s: %s",
                   package, package_path, strerror(errno));
    return ERR_NOSUCHFILE;
  }
  defer fclose(f);

  mz_zip_archive zip;
  mz_zip_zero_struct(&zip);
  defer mz_zip_reader_end(&zip);
  if (!mz_zip_reader_init_cfile(&zip, f, 0, 0)) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                   "uninstall %s: could not open zip archive %s: %s", package,
                   package_path,
                   mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
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
    uninstall_remove_file(sysroot, package, file_stat, &zip, dry_run);
  }
  return ERR_OK;
}

ErrVal resolve_package_paths(vec_char_ptr *package_paths,
                             const vec_char_ptr *repositories,
                             const vec_char_ptr *packages) {
  size_t n_targets = vec_char_ptr_len(packages);
  vec_char_ptr_init(package_paths);

  // fetch a list of the packages in the repositories
  size_t n_repositories = vec_char_ptr_len(repositories);

  vec_char_ptr *entries =
      (vec_char_ptr *)malloc(sizeof(vec_char_ptr) * n_repositories);
  for (size_t i = 0; i < n_repositories; i++) {
    vec_char_ptr_init(&entries[i]);
  }
  defer {
    for (size_t i = 0; i < n_repositories; i++) {
      vec_char_ptr_delete_and_freeowned(&entries[i]);
    }
    free(entries);
  }

  for (size_t i = 0; i < n_repositories; i++) {
    char *repository = *vec_char_ptr_at(repositories, i);
    LOG_ERROR_ARGS(ERR_LEVEL_INFO, "resolve: listing files in %s", repository);
    if (listdir_portable(repository, &entries[i]) != 0) {
      LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "resolve: unable to list files in %s: %s",
                     repository, strerror(errno));
      return ERR_NOSUCHFILE;
    }
  }

  for (size_t i = 0; i < n_targets; i++) {
    char *package = *vec_char_ptr_at(packages, i);
    size_t package_strlen = strlen(package);

    char *best_package_entry = NULL;
    char *best_repository = NULL;
    apk_blob_t best_package_entry_version;

    for (size_t r = 0; r < n_repositories; r++) {
      char *repository = *vec_char_ptr_at(repositories, r);
      size_t n_entries = vec_char_ptr_len(&entries[r]);
      for (size_t e = 0; e < n_entries; e++) {
        char *entry = *vec_char_ptr_at(&entries[i], e);
        if (strncmp(package, entry, package_strlen) != 0) {
          continue;
        }
        if (!endswith(entry, ".zip")) {
          continue;
        }

        // pointer to the first char in the .zip suffix
        char *end = entry + (strlen(entry) - strlen(".zip"));

        for (char *p = end - 1; p > entry; p--) {
          if (*p == '-') {
            // the char after the hyphen
            char *vstart = p + 1;
            if (apk_version_validate(APK_BLOB_PTR_LEN(vstart, end - vstart))) {
              apk_blob_t candidate_version =
                  APK_BLOB_PTR_LEN(vstart, end - vstart);
              if (best_package_entry == NULL ||
                  apk_blob_compare(candidate_version,
                                   best_package_entry_version) ==
                      APK_VERSION_GREATER) {
                best_package_entry = entry;
                best_repository = repository;
                best_package_entry_version = candidate_version;
              }
            }
          }
        }
      }
    }

    if (best_package_entry == NULL) {
      LOG_ERROR_ARGS(
          ERR_LEVEL_ERROR,
          "resolve: did not find validly named package %s in any repository",
          package);
      return ERR_NOSUCHFILE;
    }

    char *package_path =
        malloc(strlen(best_repository) + 1 + strlen(best_package_entry) + 1);
    sprintf(package_path, "%s/%s", best_repository, best_package_entry);
    vec_char_ptr_push(package_paths, &package_path);
  }
  
  return ERR_OK;
}
