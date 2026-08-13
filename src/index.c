#include <errno.h>
#include <stddefer.h>
#include <stdlib.h>

#include <asprintf/asprintf.h>
#include <string.h>

#include "error.h"
#include "index.h"
#include "instances/llrbset_char_ptr.h"
#include "oscompatlayer.h"
#include "pathutils.h"

// free a claims tree including its path keys
void fileclaims_delete(llrb_char_ptr_fileclaim *claims) {
  llrb_char_ptr_fileclaim_iter iter;
  llrb_char_ptr_fileclaim_iter_begin(claims, &iter);
  char_ptr path;
  FileClaim claim;
  while (llrb_char_ptr_fileclaim_iter_next(&iter, &path, &claim)) {
    free(path);
  }
  llrb_char_ptr_fileclaim_delete(claims);
}

// insert one claim, taking ownership of path (freed here when not kept).
// duplicate directory claims collapse silently (implied parents repeat for
// every file below them); any duplicate involving a file claim means the zip
// claims one path as two files or as both file and directory — reject
static bool insert_file_claim(llrb_char_ptr_fileclaim *claims,
                              // logging
                              const char *op, const char *pkg,
                              // key pair to insert
                              char *path, const FileClaim *claim) {
  FileClaim *existing = NULL;
  if (!llrb_char_ptr_fileclaim_get_ref(claims, &path, &existing)) {
    // the tree now owns `path`
    llrb_char_ptr_fileclaim_insert(claims, &path, claim);
    return true;
  }
  bool compatible = existing->is_directory && claim->is_directory;
  if (!compatible) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                   "%s %s: within-package conflict: path %s claimed as %s and "
                   "as %s, skipping duplicate",
                   op, pkg, path, existing->is_directory ? "directory" : "file",
                   claim->is_directory ? "directory" : "file");
  }
  free(path);
  return compatible;
}

// collect all explicit (listed in zip) and implicit (parent directory) claims.
void fileclaims_collect(mz_zip_archive *zip,
                        // logging only
                        const char *op, const char *package,
                        llrb_char_ptr_fileclaim *claims) {
  llrb_char_ptr_fileclaim_new(claims);
  mz_uint num_files = mz_zip_reader_get_num_files(zip);
COLLECT_ONE_FILE:
  for (mz_uint i = 0; i < num_files; i++) {
    mz_zip_archive_file_stat file_stat;
    if (!mz_zip_reader_file_stat(zip, i, &file_stat)) {
      LOG_ERROR_ARGS(
          ERR_LEVEL_ERROR,
          "%s %s: could not get file stat for file %u in zip: %s, skipping", op,
          package, i, mz_zip_get_error_string(mz_zip_get_last_error(zip)));
      continue;
    }

    mz_uint need = mz_zip_reader_get_filename(zip, i, NULL, 0);
    char *name = malloc(need);
    defer free(name);
    mz_zip_reader_get_filename(zip, i, name, need);

    char *normalized = normalize(name);
    if (normalized == NULL) {
      LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "%s %s: invalid filename %s, skipping",
                     op, package, name);
      continue;
    }
    if (strcmp(normalized, name) != 0) {
      LOG_ERROR_ARGS(ERR_LEVEL_DEBUG, "%s %s: normalized filename %s to %s", op,
                     package, name, normalized);
    }
    if (!file_stat.m_is_directory && !file_stat.m_is_supported) {
      LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                     "%s %s: file %s is encrypted or uses an unsupported "
                     "compression method, skipping",
                     op, package, normalized);
      continue;
    }

    // implied parent directories, outermost first
    for (char *p = normalized; *p != '\0'; p++) {
      if (*p != '/') {
        continue;
      }
      *p = '\0';
      char *parent = strdup(normalized);
      *p = '/';
      FileClaim parent_claim = {.is_directory = true};
      if (!insert_file_claim(claims, op, package, parent, &parent_claim)) {
        free(normalized);
        continue COLLECT_ONE_FILE;
      }
    }

    FileClaim claim = {
        .file_index = i,
        .crc32 = file_stat.m_is_directory ? 0 : file_stat.m_crc32,
        .is_directory = file_stat.m_is_directory,
    };
    insert_file_claim(claims, op, package, normalized, &claim);
  }
}

// move every claim into the shared index as `package`'s IndexDataEntry,
// consuming the tree: each path key is either handed to the index or freed.
// on return `claims` is empty (but not deleted!)
ErrVal merge_claims_into_index(fileindex_t *fileindex, const char *package_path,
                               llrb_char_ptr_fileclaim *claims,
                               bool simulate_installed) {
  llrbset_char_ptr *packages = &fileindex->packages;
  char *owned_package_path = strdup(package_path);
  if (!llrbset_char_ptr_insert(packages, &owned_package_path)) {
    free(owned_package_path);
    return ERR_UNKNOWN;
  }

  llrb_path_indexdata *index = &fileindex->index;

  llrb_char_ptr_fileclaim_iter iter;
  llrb_char_ptr_fileclaim_iter_begin(claims, &iter);
  char_ptr path;
  FileClaim claim;
  while (llrb_char_ptr_fileclaim_iter_next(&iter, &path, &claim)) {
    IndexData *data = NULL;
    if (!llrb_path_indexdata_get_ref(index, &path, &data)) {
      IndexData fresh;
      llrb_char_ptr_fileclaim_new(&fresh.claims);
      // the index now owns `path`
      data = llrb_path_indexdata_insert(index, &path, &fresh);
    } else {
      // path already indexed; the index keeps its own key
      free(path);
    }
    if (simulate_installed) {
      data->computed_actual = true;
      data->actual.exists = true;
      data->actual.is_directory = claim.is_directory;
      data->actual.crc32 = claim.crc32;
    }

    char *key = strdup(package_path);
    // duplicate package path should have been caught earlier
    assert(llrb_char_ptr_fileclaim_insert(&data->claims, &key, &claim) != NULL);
  }
  llrb_char_ptr_fileclaim_clear(claims);
}

// remove all claims from the index (if they exist). Doesn't error if they're
// not there.
void remove_claims_from_index(fileindex_t *fileindex, char *package_path,
                              llrb_char_ptr_fileclaim *claims) {
  llrbset_char_ptr *packages = &fileindex->packages;

  char *old_package_path;
  if (!llrbset_char_ptr_remove(packages, &package_path, &old_package_path)) {
    // can return early if we don't have this in the index
    return;
  }
  free(old_package_path);

  llrb_path_indexdata *index = &fileindex->index;
  llrb_char_ptr_fileclaim_iter iter;
  llrb_char_ptr_fileclaim_iter_begin(claims, &iter);
  char_ptr path;
  FileClaim claim;
  while (llrb_char_ptr_fileclaim_iter_next(&iter, &path, &claim)) {
    IndexData *data = NULL;
    if (llrb_path_indexdata_get_ref(index, &path, &data)) {
      char_ptr removed_key = NULL;
      if (llrb_char_ptr_fileclaim_remove(&data->claims, &package_path,
                                         &removed_key, NULL)) {
        free(removed_key);
        if (data->claims.len == 0 && !data->computed_actual) {
          char_ptr removed_path = NULL;
          if (llrb_path_indexdata_remove(index, &path, &removed_path, NULL)) {
            free(removed_path);
          }
        }
      }
    }
  }
}

// read the central directories of all the files and construct an index of all
// the files in it
// only_installed (mandatory for now) only builds the index with installed files
// (useful for ownership tests)
void fileindex_build(fileindex_t *fileindex, char *pkgs_path) {
  llrbset_char_ptr *packages = &fileindex->packages;
  llrbset_char_ptr_new(packages);

  llrb_path_indexdata *index = &fileindex->index;
  llrb_path_indexdata_new(index);

  // first list files in the pkgs path:
  vec_char_ptr entries;
  vec_char_ptr_init(&entries);
  defer vec_char_ptr_delete_and_freeowned(&entries);

  if (listdir_portable(pkgs_path, &entries, NULL) != 0) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "index: unable to list files in %s: %s",
                   pkgs_path, strerror(errno));
    return;
  }

  for (size_t i = 0; i < vec_char_ptr_len(&entries); i++) {
    char *entry = *vec_char_ptr_at(&entries, i);
    if (!endswith(entry, ".zip")) {
      continue;
    }
    char *package_path;
    asprintf(&package_path, "%s/%s", pkgs_path, entry);
    defer free(package_path);

    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    defer mz_zip_reader_end(&zip);
    if (!mz_zip_reader_init_file(&zip, package_path, 0)) {
      LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                     "index %s: could not open zip archive: %s", package_path,
                     mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
      continue;
    }

    llrb_char_ptr_fileclaim claims;
    fileclaims_collect(&zip, "index", package_path, &claims);
    merge_claims_into_index(fileindex, package_path, &claims, false);
    fileclaims_delete(&claims);
  }
}

// computes the whole-file crc32 into *out. op/pkg are only for log context and
// may be NULL to log without it
static ErrVal file_crc32(const char *path, uint32_t *out, const char *op,
                         const char *pkg) {
  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    if (op != NULL && pkg != NULL) {
      LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                     "%s %s: could not open file %s to compute hash: %s", op,
                     pkg, path, strerror(errno));
    }
    return ERR_NOSUCHFILE;
  }
  defer fclose(f);

  mz_ulong crc = MZ_CRC32_INIT;
  unsigned char buf[64 * 1024];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0)
    crc = mz_crc32(crc, buf, n);
  if (ferror(f) != 0) {
    if (op != NULL && pkg != NULL) {
      LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                     "%s %s: read error while computing hash of %s", op, pkg,
                     path);
    }
    return ERR_NOSUCHFILE;
  }
  *out = (uint32_t)crc;
  return ERR_OK;
}

// pass op and pkg to enable logging
// creates an empty llrb leaf if needed
// returns Indexdata pointer on success, NULL on failure
IndexData *fileindex_ensure_actual(fileindex_t *fileindex, const char *sysroot,
                                   char *path, const char *op,
                                   const char *pkg) {
  llrb_path_indexdata *index = &fileindex->index;
  IndexData *indexdata;
  if (!llrb_path_indexdata_get_ref(index, &path, &indexdata)) {
    // create a new node for future reference.
    char *pathkey = strdup(path);
    IndexData id = {
        .computed_actual = false,
    };
    llrb_char_ptr_fileclaim_new(&id.claims);
    indexdata = llrb_path_indexdata_insert(index, &pathkey, &id);
  }

  if (indexdata->computed_actual) {
    return indexdata;
  }

  char *fullpath;
  asprintf(&fullpath, "%s/%s", sysroot, path);
  defer free(fullpath);

  switch (path_type_portable(fullpath)) {
  case PATH_TYPE_MISSING:
    indexdata->actual.exists = false;
    break;
  case PATH_TYPE_DIR:
    indexdata->actual.exists = true;
    indexdata->actual.is_directory = true;
    break;
  case PATH_TYPE_FILE: {
    uint32_t crc;
    ErrVal err = file_crc32(fullpath, &crc, op, pkg);
    if (err != ERR_OK) {
      return NULL;
    }
    indexdata->actual.exists = true;
    indexdata->actual.is_directory = false;
    indexdata->actual.crc32 = crc;
    break;
  }
  case PATH_TYPE_OTHER:
    // sockets, devices, fifos: reading one for a crc could block forever, so
    // record it as a file whose crc only collides with an empty file's (0)
    indexdata->actual.exists = true;
    indexdata->actual.is_directory = false;
    indexdata->actual.crc32 = 0;
    break;
  case PATH_TYPE_ERROR:
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "%s %s: could not stat %s: %s", op, pkg,
                   fullpath, strerror(errno));
    return NULL;
  }

  indexdata->computed_actual = true;
  return indexdata;
}

static void delete_char_ptr_fileclaims(llrb_char_ptr_fileclaim *claims) {
  llrb_char_ptr_fileclaim_iter claims_iter;
  llrb_char_ptr_fileclaim_iter_begin(claims, &claims_iter);
  char_ptr claims_key;
  while (llrb_char_ptr_fileclaim_iter_next(&claims_iter, &claims_key, NULL)) {
    free(claims_key);
  }
  llrb_char_ptr_fileclaim_delete(claims);
}

void fileindex_delete(fileindex_t *fileindex) {
  llrb_path_indexdata *index = &fileindex->index;
  llrb_path_indexdata_iter index_iter;
  llrb_path_indexdata_iter_begin(index, &index_iter);
  char_ptr index_key;
  IndexData indexdata;
  while (llrb_path_indexdata_iter_next(&index_iter, &index_key, &indexdata)) {
    delete_char_ptr_fileclaims(&indexdata.claims);
    free(index_key);
  }
  llrb_path_indexdata_delete(index);
  llrbset_char_ptr_delete_and_freeowned(&fileindex->packages);
}
