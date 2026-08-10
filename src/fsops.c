#include <assert.h>
#include <errno.h>
#include <stddefer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "apkver/apkver.h"
#include "constants.h"
#include "error.h"
#include "fsop.h"
#include "fsops.h"
#include "instances/llrb_char_ptr_fileclaim.h"
#include "instances/llrb_path_indexdata.h"
#include "instances/vec_char_ptr.h"
#include "instances/vec_fsop.h"
#include "instances/vec_mz_zip_archive.h"
#include "miniz/miniz.h"
#include "oscompatlayer.h"

// normalize the filename by dropping "." components and empty components
// (leading, doubled, and trailing slashes). if the filename is bad (has a
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
  // omit trailing / for normalization purposes
  if (out[out_len - 1] == '/') {
    out_len--;
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

// free a claims tree including its path keys
static void delete_file_claims(llrb_char_ptr_fileclaim *claims) {
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
static bool insert_file_claim(llrb_char_ptr_fileclaim *claims, const char *op,
                              const char *package, char *path,
                              const FileClaim *claim) {
  FileClaim *existing = NULL;
  if (!llrb_char_ptr_fileclaim_get_ref(claims, &path, &existing)) {
    // the tree now owns `path`
    llrb_char_ptr_fileclaim_insert(claims, &path, claim);
    return true;
  }
  bool compatible = existing->is_directory && claim->is_directory;
  if (!compatible) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "%s %s: path %s claimed as %s and as %s",
                   op, package, path,
                   existing->is_directory ? "directory" : "file",
                   claim->is_directory ? "directory" : "file");
  }
  free(path);
  return compatible;
}

static bool collect_one_claim(mz_zip_archive *zip, mz_uint i, const char *op,
                              const char *package,
                              llrb_char_ptr_fileclaim *claims) {
  mz_zip_archive_file_stat file_stat;
  if (!mz_zip_reader_file_stat(zip, i, &file_stat)) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                   "%s %s: could not get file stat for file %u in zip: %s", op,
                   package, i,
                   mz_zip_get_error_string(mz_zip_get_last_error(zip)));
    return false;
  }

  mz_uint need = mz_zip_reader_get_filename(zip, i, NULL, 0);
  char *name = malloc(need);
  defer free(name);
  mz_zip_reader_get_filename(zip, i, name, need);

  char *normalized = normalize(name);
  if (normalized == NULL) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "%s %s: invalid filename %s", op, package,
                   name);
    return false;
  }
  if (strcmp(normalized, name) != 0) {
    LOG_ERROR_ARGS(ERR_LEVEL_INFO, "%s %s: normalized filename %s to %s", op,
                   package, name, normalized);
  }
  if (!file_stat.m_is_directory && !file_stat.m_is_supported) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                   "%s %s: file %s is encrypted or uses an unsupported "
                   "compression method",
                   op, package, normalized);
    return false;
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
      return false;
    }
  }

  FileClaim claim = {
      .file_index = i,
      .crc32 = file_stat.m_is_directory ? 0 : file_stat.m_crc32,
      .is_directory = file_stat.m_is_directory,
  };
  return insert_file_claim(claims, op, package, normalized, &claim);
}

// collect all explicit (listed in zip) and implicit (parent directory) claims.
//
static void collect_file_claims(mz_zip_archive *zip, const char *op,
                                const char *package,
                                llrb_char_ptr_fileclaim *claims) {
  llrb_char_ptr_fileclaim_new(claims);
  mz_uint num_files = mz_zip_reader_get_num_files(zip);
  for (mz_uint i = 0; i < num_files; i++) {
    collect_one_claim(zip, i, op, package, claims);
  }
}

// move every claim into the shared index as `package`'s IndexDataEntry,
// consuming the tree: each path key is either handed to the index or freed.
// on return `claims` is empty
static void merge_claims_into_index(llrb_path_indexdata *index,
                                    const char *package,
                                    llrb_char_ptr_fileclaim *claims) {
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
      llrb_path_indexdata_insert(index, &path, &fresh);
      llrb_path_indexdata_get_ref(index, &path, &data);
    } else {
      // path already indexed; the index keeps its own key
      free(path);
    }

    char *key = strdup(package);
    bool inserted = llrb_char_ptr_fileclaim_insert(&data->claims, &key, &claim);
    // duplicate package name should have been caught earlier
    assert(inserted);
  }
  llrb_char_ptr_fileclaim_delete(claims);
}

// remove all claims from the index (if they exist). Doesn't error if they're
// not there.
static void remove_claims_from_index(llrb_path_indexdata *index, char *package,
                                     llrb_char_ptr_fileclaim *claims) {
  llrb_char_ptr_fileclaim_iter iter;
  llrb_char_ptr_fileclaim_iter_begin(claims, &iter);
  char_ptr path;
  FileClaim claim;
  while (llrb_char_ptr_fileclaim_iter_next(&iter, &path, &claim)) {
    IndexData *data = NULL;
    if (llrb_path_indexdata_get_ref(index, &path, &data)) {
      FileClaim _claim;
      bool removed =
          llrb_char_ptr_fileclaim_remove(&data->claims, &package, &_claim);
      if (removed && data->claims.len == 0) {
        IndexData _indexdata;
        llrb_path_indexdata_remove(index, &path, &_indexdata);
      }
    }
  }
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

  llrb_char_ptr_fileclaim claims;
  collect_file_claims(&zip, "index", package, &claims);
  merge_claims_into_index(index, package, &claims);
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

// ensure that the indexdata exists
// pass op and pkg to enable logging
static ErrVal ensure_actual(IndexData *indexdata, const char *sysroot,
                            const char *path, const char *op, const char *pkg) {
  if (indexdata->computed_actual) {
    return ERR_OK;
  }

  char *fullpath = malloc(strlen(sysroot) + 1 + strlen(path) + 1);
  defer free(fullpath);
  sprintf(fullpath, "%s/%s", sysroot, path);

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
      return err;
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
    return ERR_UNKNOWN;
  }

  indexdata->computed_actual = true;
  return ERR_OK;
}

static void delete_char_ptr_fileclaims(llrb_char_ptr_fileclaim *claims) {
  llrb_char_ptr_fileclaim_iter claims_iter;
  llrb_char_ptr_fileclaim_iter_begin(claims, &claims_iter);
  char_ptr claims_key;
  FileClaim _claim;
  while (
      llrb_char_ptr_fileclaim_iter_next(&claims_iter, &claims_key, &_claim)) {
    free(claims_key);
  }
  llrb_char_ptr_fileclaim_delete(claims);
}

void delete_file_index(llrb_path_indexdata *index) {
  llrb_path_indexdata_iter index_iter;
  llrb_path_indexdata_iter_begin(index, &index_iter);
  char_ptr index_key;
  IndexData indexdata;
  while (llrb_path_indexdata_iter_next(&index_iter, &index_key, &indexdata)) {
    delete_char_ptr_fileclaims(&indexdata.claims);
    free(index_key);
  }
  llrb_path_indexdata_delete(index);
}

// create sysroot and its ancestors. package-relative directories don't come
// through here: the claims tree lists every one of them, parents strictly
// before children, so install creates them in iteration order
static ErrVal ensure_sysroot_exists(const char *package, const char *sysroot) {
  // append a trailing '/' so the loop sees a boundary for sysroot itself
  char *path = malloc(strlen(sysroot) + 2);
  defer free(path);
  sprintf(path, "%s/", sysroot);
  // path+1: if sysroot is absolute the first boundary belongs to the root,
  // and creating it would be mkdir_portable(""), which fails with ENOENT
  // rather than EEXIST
  for (char *p = path + 1; *p != '\0'; p++) {
    if (*p == '/') {
      *p = '\0';
      int result = mkdir_portable(path, 0o755);
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

// logging is mandatory here
void execute_fsops(vec_fsop *fsops, vec_mz_zip_archive *zips, const char *op,
                   const char *pkg, const bool dry_run) {
  assert(op != NULL && pkg != NULL);

  for (size_t i = 0; i < fsops->len; i++) {
    fsop *o = vec_fsop_at(fsops, i);
    switch (o->kind) {

    case FSOP_RENAME:
      LOG_ERROR_ARGS(ERR_LEVEL_INFO, "%s %s: renaming from %s to %s", op, pkg,
                     o->rename.from, o->rename.to);
      if (!dry_run && rename_portable(o->rename.from, o->rename.to) != 0) {
        LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                       "%s %s: failed to rename file %s to %s: %s", op, pkg,
                       o->rename.from, o->rename.to, strerror(errno));
      }
      break;
    case FSOP_CREATEFILE: {
      LOG_ERROR_ARGS(ERR_LEVEL_INFO, "%s %s: extracting file to %s", op, pkg,
                     o->createfile.path);
      mz_zip_archive *pZip =
          vec_mz_zip_archive_at(zips, o->createfile.zip_index);
      if (!dry_run &&
          !mz_zip_reader_extract_to_file(pZip, o->createfile.file_index,
                                         o->createfile.path, 0)) {
        LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "%s %s: could not extract file %s: %s",
                       op, pkg, o->createfile.path,
                       mz_zip_get_error_string(mz_zip_get_last_error(pZip)));
      }
      break;
    }
    case FSOP_REMOVEFILE:
      if (!dry_run && remove(o->removefile.path) != 0) {
        LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "%s %s: could not remove file %s: %s",
                       op, pkg, o->removefile.path, strerror(errno));
      }
      break;
    case FSOP_MKDIR:
      LOG_ERROR_ARGS(ERR_LEVEL_INFO, "%s %s: creating directory %s", op, pkg,
                     o->mkdir.path);
      if (!dry_run && mkdir_portable(o->mkdir.path, 0o755) != 0) {
        LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                       "%s %s: could not create directory %s: %s", op, pkg,
                       o->mkdir.path, strerror(errno));
      }
      break;
    case FSOP_RMDIR:
      if (!dry_run && rmdir_portable(o->rmdir.path) != 0) {
        LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                       "%s %s: could not remove directory %s: %s", op, pkg,
                       o->rmdir.path, strerror(errno));
      }
      break;
    }
  }
}

// compute the status of the actual file wrt the package claims
static void compute_match_status(
    // index must not contain the package we're considering
    // (natural for installation, means package must be removed from index prior
    // to uninstallation)

    llrb_path_indexdata *index,
    // contains the claims for the package (needed for validating crc32s)
    llrb_char_ptr_fileclaim claims,

    // the package name
    const char *package,
    // sysroot
    const char *sysroot,

    // path to the file we're considering (relative to sysroot)
    char *path,
    // the corresponding fileclaim
    FileClaim claim,

    // sets these 3 bools.
    bool *exists, bool *matchesus, bool *matchesother,
    // if matchesother is true, sets this string (borrowed from index)
    char **otherpackage) {
  IndexData *indexdata;

  if (llrb_path_indexdata_get_ref(index, &path, &indexdata)) {
    ensure_actual(indexdata, sysroot, path, "install", package);
    if (!indexdata->actual.exists) {
      *exists = false;
      *matchesus = false;
      *matchesother = false;
    }

    if (claim.is_directory) {
      if (indexdata->actual.is_directory) {
        continue;
      }
    }
  }
}

ErrVal install_package(
    // appends to this if the operation would succeed
    vec_fsop *fsops,
    // fsops refer to indexes in the zips. appends to this if the operation
    // would succeed
    vec_mz_zip_archive *zips,
    // file index (for file conflict identification)
    llrb_path_indexdata *index,
    // user given package name (For logging)
    char *package,
    // zip file to install
    char *package_path,
    // where to install
    char *sysroot) {

  llrb_char_ptr_fileclaim claims;
  {
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!mz_zip_reader_init_file(&zip, package_path, 0)) {
      LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                     "install %s: could not open zip archive %s: %s", package,
                     package_path,
                     mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
      return ERR_UNKNOWN;
    }
    collect_file_claims(&zip, "install", package, &claims);
    mz_zip_reader_end(&zip);
  }

  vec_fsop pkfsops;
  defer vec_fsop_delete_and_freeowned(&pkfsops);

  vec_mz_zip_archive pkzips;
  defer vec_mz_zip_archive_delete_and_freeowned(&pkzips);

  bool should_not_install = false;

  llrb_char_ptr_fileclaim_iter iter;
  llrb_char_ptr_fileclaim_iter_begin(&claims, &iter);
  char_ptr path;
  FileClaim claim;
  while (llrb_char_ptr_fileclaim_iter_next(&iter, &path, &claim)) {
    bool exists;
    bool matchesus;
    bool matchesother;
    char *otherpackage = NULL;
    compute_match_status(index, claims, package, sysroot, path, claim, &exists,
                         &matchesus, &matchesother, &otherpackage);

    if (exists) {
      if (matchesus) {
        // already exists and matches us
        continue;
      } else if (matchesother) {
        LOG_ERROR_ARGS(
            ERR_LEVEL_ERROR,
            "install %s: file conflict on %s: file exists and matches %s",
            package, path, otherpackage);
        should_not_install = true;
        continue;
      } else {
        // is an unowned file
        
        // we need to save it to .zpksave (both directories and files)
        // but first we need to ensure that the current contents of .zpksave are deleted

      }
    }

    vec_fsop_push(&pkfsops, &o);
  }

  if (should_not_install) {
    return ERR_UNSAFE;
  }

  // if all good transfer ownership of newly created pkgs and zips
  vec_fsop_append(fsops, &pkfsops);
  vec_fsop_clear(&pkfsops);
  vec_mz_zip_archive_append(zips, &pkzips);
  vec_mz_zip_archive_clear(&pkzips);

  return ERR_OK;
}

ErrVal uninstall_package(
    // appends to this if the operation would succeed
    vec_fsop *fsops,
    // fsops refer to indexes in the zips. appends to this if the operation
    // would succeed
    vec_mz_zip_archive *zips,
    // file index (for file conflict identification)
    llrb_path_indexdata *index,
    // user given package name (For logging)
    char *package,
    // zip file to uninstall
    char *package_path,
    // where to uninstall
    char *sysroot) {

  llrb_char_ptr_fileclaim claims;
  {
    mz_zip_archive zip;
    mz_zip_zero_struct(&zip);
    if (!mz_zip_reader_init_file(&zip, package_path, 0)) {
      LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                     "install %s: could not open zip archive %s: %s", package,
                     package_path,
                     mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
      return ERR_UNKNOWN;
    }
    collect_file_claims(&zip, "install", package, &claims);
    mz_zip_reader_end(&zip);
  }

  // remove claims from index
  remove_claims_from_index(index, package, &claims);

  vec_fsop pkfsops;
  defer vec_fsop_delete_and_freeowned(&pkfsops);

  vec_mz_zip_archive pkzips;
  defer vec_mz_zip_archive_delete_and_freeowned(&pkzips);

  llrb_char_ptr_fileclaim_iter iter;
  llrb_char_ptr_fileclaim_iter_begin(&claims, &iter);
  char_ptr path;
  FileClaim claim;
  while (llrb_char_ptr_fileclaim_iter_next(&iter, &path, &claim)) {

    // remove in reverse sorted order so every child is deleted before the
    // parent we also ignore where other packages have claims

    vec_fsop fsops_backward;
    vec_fsop_init_cap(&fsops_backward, claims.len);
    defer vec_char_ptr_delete(&paths);
    llrb_char_ptr_fileclaim_iter iter;
    llrb_char_ptr_fileclaim_iter_begin(&claims, &iter);
    char_ptr path;
    FileClaim claim;
    while (llrb_char_ptr_fileclaim_iter_next(&iter, &path, &claim)) {

      IndexData *indexdata;
      bool indexdata_found =
          llrb_path_indexdata_get_ref(index, &path, &indexdata);
      // something has gone wrong if we didn't find it.
      // this is because the package we are uninstalling must have been
      // installed. we didn't ask the indexing to skip itself, so it has gotta
      // be in there.
      assert(indexdata_found);

      // similarly, we must ensure that this claim is actually in the filetree
      // (in order to have the next thing make sense)
      FileClaim thisclaim;
      bool thisclaim_found =
          llrb_char_ptr_fileclaim_get(&indexdata->claims, &package, &thisclaim);
      assert(thisclaim_found);

      // if there is > 1 claim omit from deletion
      if (indexdata->claims.len > 1) {
        continue;
      }

      // TODO: compare crc32 of actual file and omit if different than what it
      // was supposed to be

      vec_char_ptr_push(&paths, &path);
    }

    for (size_t i_r = vec_char_ptr_len(&paths); i_r > 0; i_r--) {
      size_t i = i_r - 1;
      path = *vec_char_ptr_at(&paths, i);
      bool found_fileclaim =
          llrb_char_ptr_fileclaim_get(&claims, &path, &claim);
      assert(found_fileclaim);

      char *target = malloc(strlen(sysroot) + 1 + strlen(path) + 1);
      defer free(target);
      sprintf(target, "%s/%s", sysroot, path);

      LOG_ERROR_ARGS(ERR_LEVEL_INFO, "uninstall %s: removing %s (at %s)",
                     package, path, target);
      if (!dry_run) {
        if (claim.is_directory) {

        } else if (remove(target) != 0) {
          LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                         "uninstall %s: could not remove file %s (at %s): %s",
                         package, path, target, strerror(errno));
        }
      }
    }

    return ERR_OK;
  }

  ErrVal resolve_package_paths(vec_char_ptr * package_paths,
                               const vec_char_ptr *repositories,
                               const vec_char_ptr *packages) {
    size_t n_targets = vec_char_ptr_len(packages);

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
      LOG_ERROR_ARGS(ERR_LEVEL_INFO, "resolve: listing files in %s",
                     repository);
      if (listdir_portable(repository, &entries[i]) != 0) {
        LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                       "resolve: unable to list files in %s: %s", repository,
                       strerror(errno));
        return ERR_NOSUCHFILE;
      }
    }

    for (size_t i = 0; i < n_targets; i++) {
      char *package = *vec_char_ptr_at(packages, i);
      size_t package_strlen = strlen(package);

      char *best_package_entry = NULL;
      char *best_repository = NULL;
      apk_blob_t best_package_entry_version = {};

      for (size_t r = 0; r < n_repositories; r++) {
        char *repository = *vec_char_ptr_at(repositories, r);
        size_t n_entries = vec_char_ptr_len(&entries[r]);
        for (size_t e = 0; e < n_entries; e++) {
          char *entry = *vec_char_ptr_at(&entries[r], e);
          size_t entry_strlen = strlen(entry);
          if (!endswith(entry, ".zip")) {
            continue;
          }
          if (!(package_strlen + 1 < entry_strlen &&
                entry[package_strlen] == '-' &&
                strncmp(entry, package, package_strlen) == 0)) {
            continue;
          }
          // // pointer to the first char in the .zip suffix
          char *end = entry + (strlen(entry) - strlen(".zip"));
          // the char after the hyphen
          char *vstart = entry + package_strlen + 1;
          apk_blob_t candidate_version = APK_BLOB_PTR_LEN(vstart, end - vstart);

          if (!apk_version_validate(candidate_version)) {
            continue;
          }

          if (best_package_entry == NULL ||
              apk_version_compare(candidate_version,
                                  best_package_entry_version) ==
                  APK_VERSION_GREATER) {
            best_package_entry = entry;
            best_repository = repository;
            best_package_entry_version = candidate_version;
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
