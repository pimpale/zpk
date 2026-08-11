#include <assert.h>
#include <errno.h>
#include <stddefer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "apkver/apkver.h"
#include "asprintf/asprintf.h"
#include "constants.h"
#include "error.h"
#include "fsop.h"
#include "fsops.h"
#include "instances/llrb_char_ptr_fileclaim.h"
#include "instances/llrb_path_indexdata.h"
#include "instances/vec_char_ptr.h"
#include "instances/vec_fsop_t.h"
#include "instances/vec_mz_zip_archive_ptr.h"
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
// on return `claims` is empty (but not deleted!)
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
      data = llrb_path_indexdata_insert(index, &path, &fresh);
    } else {
      // path already indexed; the index keeps its own key
      free(path);
    }

    char *key = strdup(package);
    FileClaim *inserted =
        llrb_char_ptr_fileclaim_insert(&data->claims, &key, &claim);
    // duplicate package name should have been caught earlier
    assert(inserted != NULL);
  }
  llrb_char_ptr_fileclaim_clear(claims);
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
      bool removed =
          llrb_char_ptr_fileclaim_remove(&data->claims, &package, NULL);

      // clean up cases where there's no reason to keep the node around
      if (removed && data->claims.len == 0 && !data->computed_actual) {
        llrb_path_indexdata_remove(index, &path, NULL);
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
  delete_file_claims(&claims);
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

// pass op and pkg to enable logging
// creates an empty llrb leaf if needed
// returns Indexdata pointer on success, NULL on failure
static IndexData *ensure_actual(llrb_path_indexdata *index, const char *sysroot,
                                char *path, const char *op, const char *pkg) {
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
  char *path;
  asprintf(&path, "%s/", sysroot);
  defer free(path);
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
void execute_fsops(vec_fsop_t *fsops, const bool dry_run) {
  for (size_t i = 0; i < fsops->len; i++) {
    fsop_t *o = vec_fsop_t_at(fsops, i);
    switch (o->kind) {

    case FSOP_RENAME:
      LOG_ERROR_ARGS(ERR_LEVEL_INFO, "%s %s: renaming from %s to %s", o->op,
                     o->pkg, o->rename.from, o->rename.to);
      if (!dry_run && rename_portable(o->rename.from, o->rename.to) != 0) {
        LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                       "%s %s: failed to rename file %s to %s: %s", o->op,
                       o->pkg, o->rename.from, o->rename.to, strerror(errno));
      }
      break;
    case FSOP_CREATEFILE:
      LOG_ERROR_ARGS(ERR_LEVEL_INFO, "%s %s: extracting file to %s", o->op,
                     o->pkg, o->createfile.path);
      if (!dry_run && !mz_zip_reader_extract_to_file(o->createfile.zip,
                                                     o->createfile.file_index,
                                                     o->createfile.path, 0)) {
        LOG_ERROR_ARGS(
            ERR_LEVEL_ERROR, "%s %s: could not extract file %s: %s", o->op,
            o->pkg, o->createfile.path,
            mz_zip_get_error_string(mz_zip_get_last_error(o->createfile.zip)));
      }
      break;

    case FSOP_REMOVEFILE:
      if (!dry_run && remove(o->removefile.path) != 0) {
        LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "%s %s: could not remove file %s: %s",
                       o->op, o->pkg, o->removefile.path, strerror(errno));
      }
      break;
    case FSOP_MKDIR:
      LOG_ERROR_ARGS(ERR_LEVEL_INFO, "%s %s: creating directory %s", o->op,
                     o->pkg, o->mkdir.path);
      if (!dry_run && mkdir_portable(o->mkdir.path, 0o755) != 0) {
        LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                       "%s %s: could not create directory %s: %s", o->op,
                       o->pkg, o->mkdir.path, strerror(errno));
      }
      break;
    case FSOP_RMDIR:
      if (!dry_run && rmdir_portable(o->rmdir.path) != 0) {
        LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                       "%s %s: could not remove directory %s: %s", o->op,
                       o->pkg, o->rmdir.path, strerror(errno));
      }
      break;
    }
  }
}

static bool is_match(FileStatus fs, FileClaim claim) {
  if (!fs.exists) {
    return false;
  }
  if (fs.is_directory != claim.is_directory) {
    return false;
  }
  return fs.crc32 == claim.crc32;
}

// compute the status of the actual file wrt the package claims
static ErrVal compute_match_status(
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
  *exists = false;
  *matchesus = false;
  *matchesother = false;
  *otherpackage = NULL;

  IndexData *indexdata =
      ensure_actual(index, sysroot, path, "install", package);
  if (indexdata == NULL) {
    // something went wrong, bail
    return ERR_UNKNOWN;
  }
  if (!indexdata->actual.exists) {
    return ERR_OK;
  }
  *exists = true;
  *matchesus = is_match(indexdata->actual, claim);

  llrb_char_ptr_fileclaim_iter iter;
  llrb_char_ptr_fileclaim_iter_begin(&indexdata->claims, &iter);
  char_ptr package1;
  FileClaim claim1;
  while (llrb_char_ptr_fileclaim_iter_next(&iter, &package1, &claim1)) {
    if (is_match(indexdata->actual, claim1)) {
      *matchesother = true;
      *otherpackage = package1;
      break;
    }
  }
  return ERR_OK;
}

static bool in_protected_paths(vec_char_ptr *protected_paths, char *path) {
  // TODO: implement this
  return false;
}

static char *mkfullpath(const char *sysroot, const char *path,
                        const char *suffix) {
  char *fullpath;
  if (path == NULL) {
    assert(suffix == NULL);
    fullpath = strdup(sysroot);
  } else {
    asprintf(&fullpath, "%s/%s%s", sysroot, path, suffix);
  }
  return fullpath;
}

static ErrVal fsops_emit_install(const char *op, const char *pkg,
                                 const char *sysroot, const char *path,
                                 const char *suffix, FileClaim claim,
                                 mz_zip_archive *zip,
                                 // appends to this
                                 vec_fsop_t *fsops) {

  fsop_t o = {.op = op, .pkg = strdup(pkg)};
  if (claim.is_directory) {
    o.kind = FSOP_MKDIR;
    o.mkdir.path = mkfullpath(sysroot, path, suffix);
  } else {
    o.kind = FSOP_CREATEFILE;
    o.createfile.path = mkfullpath(sysroot, path, suffix);
    o.createfile.file_index = claim.file_index;
    o.createfile.zip = zip;
  }
  vec_fsop_t_push(fsops, &o);
  return ERR_OK;
}

static ErrVal fsops_emit_rm(const char *op, const char *pkg,
                            const char *sysroot, const char *path,
                            const char *suffix,
                            // appends to this
                            vec_fsop_t *fsops) {
  fsop_t o = {.op = op, .pkg = strdup(pkg)};
  o.kind = FSOP_REMOVEFILE;
  o.removefile.path = mkfullpath(sysroot, path, suffix);
  vec_fsop_t_push(fsops, &o);
  return ERR_OK;
}

static ErrVal fsops_emit_rmdir(const char *op, const char *pkg,
                               const char *sysroot, const char *path,
                               const char *suffix,
                               // appends to this
                               vec_fsop_t *fsops) {
  fsop_t o = {.op = op, .pkg = strdup(pkg)};
  o.kind = FSOP_RMDIR;
  o.rmdir.path = mkfullpath(sysroot, path, suffix);
  vec_fsop_t_push(fsops, &o);
  return ERR_OK;
}

static ErrVal fsops_emit_mv(const char *op, const char *pkg,

                            const char *sysroot, const char *frompath,
                            const char *fromsuffix, const char *topath,
                            const char *tosuffix, vec_fsop_t *fsops) {
  fsop_t o = {.op = op,
              .pkg = strdup(pkg),
              .kind = FSOP_RENAME,
              .rename = {.from = mkfullpath(sysroot, frompath, fromsuffix),
                         .to = mkfullpath(sysroot, topath, tosuffix)}};
  vec_fsop_t_push(fsops, &o);
  return ERR_OK;
}

// rm -rf on the path
// needed for when there is something blocking the way on our installation, and
// it's not a protected path
// doesn't log errors if it's not actually a directory. Only logs errors if we
// fail to read a file
static ErrVal fsops_emit_rm_rf(const char *op, const char *pkg,
                               const char *sysroot, const char *path,
                               const char *suffix,
                               // appends to this
                               vec_fsop_t *fsops

) {
  char *fullpath = mkfullpath(sysroot, path, suffix);

  switch (path_type_portable(fullpath)) {
  case PATH_TYPE_MISSING:
    free(fullpath);
    return ERR_OK;
  case PATH_TYPE_FILE:
  case PATH_TYPE_OTHER:
    fsops_emit_rm(op, pkg, fullpath, NULL, NULL, fsops);
    free(fullpath);
    return ERR_OK;
  case PATH_TYPE_ERROR:
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "%s %s: could not stat %s: %s", op, pkg,
                   fullpath, strerror(errno));
    free(fullpath);
    return ERR_NOSUCHFILE;
  case PATH_TYPE_DIR:
    break;
  }

  vec_char_ptr dirs_to_visit;
  vec_char_ptr_init(&dirs_to_visit);
  vec_char_ptr_push(&dirs_to_visit, &fullpath);
  defer vec_char_ptr_delete_and_freeowned(&dirs_to_visit);

  vec_char_ptr dirs_visited;
  vec_char_ptr_init(&dirs_visited);
  defer vec_char_ptr_delete_and_freeowned(&dirs_visited);

  vec_char_ptr dir_entries;
  vec_char_ptr_init(&dir_entries);
  defer vec_char_ptr_delete_and_freeowned(&dir_entries);

  vec_char_ptr file_entries;
  vec_char_ptr_init(&file_entries);
  defer vec_char_ptr_delete_and_freeowned(&file_entries);

  while (vec_char_ptr_len(&dirs_to_visit) > 0) {
    char *p;
    vec_char_ptr_pop(&dirs_to_visit, &p);
    vec_char_ptr_push(&dirs_visited, &p);

    if (listdir_portable(p, &file_entries, &dir_entries) != 0) {
      LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "%s %s: unable to list files in %s: %s",
                     op, pkg, p, strerror(errno));
      return ERR_NOSUCHFILE;
    }

    // delete mere files
    for (size_t i = 0; i < vec_char_ptr_len(&file_entries); i++) {
      char *entry = *vec_char_ptr_at(&file_entries, i);
      fsops_emit_rm(op, pkg, p, entry, "", fsops);
    }
    vec_char_ptr_clear_and_freeowned(&file_entries);

    // recurse into subfolders
    for (size_t i = 0; i < vec_char_ptr_len(&dir_entries); i++) {
      char *entry = *vec_char_ptr_at(&dir_entries, i);
      char *subdir;
      asprintf(&subdir, "%s/%s", p, entry);
      vec_char_ptr_push(&dirs_to_visit, &subdir);
    }
    vec_char_ptr_clear_and_freeowned(&dir_entries);
  }

  // now delete the seen dirs in reverse order
  for (size_t i_r = vec_char_ptr_len(&dirs_visited); i_r > 0; i_r--) {
    size_t i = i_r - 1;
    fsops_emit_rmdir(op, pkg, *vec_char_ptr_at(&dirs_visited, i), NULL, NULL,
                     fsops);
  }
  return ERR_OK;
}
// always mallocs
// note that only one diversion may be applied per path. This is fine for
// .zpknew
static char *maybe_divert_path(const char *path,
                               vec_char_ptr *src_diverted_prefixes,
                               vec_char_ptr *dest_diverted_prefixes) {
  size_t diversions_len = vec_char_ptr_len(src_diverted_prefixes);
  assert(vec_char_ptr_len(dest_diverted_prefixes) == diversions_len);
  for (size_t i = 0; i < diversions_len; i++) {
    char *src_prefix = *vec_char_ptr_at(src_diverted_prefixes, i);
    char *dest_prefix = *vec_char_ptr_at(dest_diverted_prefixes, i);

    size_t prefix_strlen = strlen(src_prefix);
    if (strncmp(path, src_prefix, prefix_strlen) == 0 &&
        (path[prefix_strlen] == '/' || path[prefix_strlen] == '\0')) {
      char *out;
      asprintf(&out, "%s%s", dest_prefix, path + prefix_strlen);
      return out;
    }
  }
  return strdup(path);
}

ErrVal install_package(
    // appends to this if the operation would succeed
    vec_fsop_t *fsops,
    // fsops refer to indexes in the zips. appends to this if the operation
    // would succeed
    vec_mz_zip_archive_ptr *zips,
    // file index (for file conflict identification)
    llrb_path_indexdata *index,
    // user given package name (For logging)
    char *package,
    // zip file to install
    char *package_path,
    // where to install
    char *sysroot,
    // protected paths
    vec_char_ptr *protected_paths) {

  llrb_char_ptr_fileclaim claims;
  mz_zip_archive *pZip = malloc(sizeof(mz_zip_archive));
  mz_zip_zero_struct(pZip);
  if (!mz_zip_reader_init_file(pZip, package_path, 0)) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                   "install %s: could not open zip archive %s: %s", package,
                   package_path,
                   mz_zip_get_error_string(mz_zip_get_last_error(pZip)));
    return ERR_UNKNOWN;
  }
  collect_file_claims(pZip, "install", package, &claims);
  defer delete_file_claims(&claims);

  vec_fsop_t pkfsops;
  vec_fsop_t_init(&pkfsops);
  defer vec_fsop_t_delete_and_freeowned(&pkfsops);

  // set this to true if we reach a package-fatal error that means it shouldn't
  // be installed we want to surface all errors though rather than just the
  // first one
  bool should_not_install = false;

  // in protected directories, we may need to install to $FILE.zpknew
  // recursively. This helps with that
  vec_char_ptr src_diverted_prefixes;
  vec_char_ptr_init(&src_diverted_prefixes);
  defer vec_char_ptr_delete_and_freeowned(&src_diverted_prefixes);
  vec_char_ptr dest_diverted_prefixes;
  vec_char_ptr_init(&dest_diverted_prefixes);
  defer vec_char_ptr_delete_and_freeowned(&dest_diverted_prefixes);

  llrb_char_ptr_fileclaim_iter iter;
  llrb_char_ptr_fileclaim_iter_begin(&claims, &iter);
  char_ptr raw_path;
  FileClaim claim;
  while (llrb_char_ptr_fileclaim_iter_next(&iter, &raw_path, &claim)) {
    char *path = maybe_divert_path(raw_path, &src_diverted_prefixes,
                                   &dest_diverted_prefixes);
    defer free(path);

    bool exists;
    bool matchesus;
    bool matchesother;
    char *otherpackage = NULL;
    compute_match_status(index, claims, package, sysroot, path, claim, &exists,
                         &matchesus, &matchesother, &otherpackage);

    if (exists) {
      if (matchesus) {
        // already exists and matches us, do nothing
      } else if (matchesother) {
        LOG_ERROR_ARGS(
            ERR_LEVEL_ERROR,
            "install %s: file conflict on %s: file exists and matches %s",
            package, path, otherpackage);
        should_not_install = true;
      } else {
        if (in_protected_paths(protected_paths, path)) {
          if (fsops_emit_rm_rf("install", package, sysroot, path, ".zpknew",
                               &pkfsops) != ERR_OK) {
            should_not_install = true;
            continue;
          }
          LOG_ERROR_ARGS(
              ERR_LEVEL_WARN,
              "install %s: installing new %s as %s.zpknew (no match + "
              "in protected path)",
              package, path, path);
          fsops_emit_install("install", package, sysroot, path, ".zpknew",
                             claim, pZip, &pkfsops);

          // emit diversion for future files
          char *src = strdup(path);
          char *dest;
          asprintf(&dest, "%s%s", path, ".zpknew");
          vec_char_ptr_push(&src_diverted_prefixes, &src);
          vec_char_ptr_push(&dest_diverted_prefixes, &dest);
        } else {
          LOG_ERROR_ARGS(
              ERR_LEVEL_WARN,
              "install %s: renaming old %s to %s.zpksave (no match + "
              "not in protected path)",
              package, path, path);
          if (fsops_emit_rm_rf("install", package, sysroot, path, ".zpksave",
                               &pkfsops) != ERR_OK) {
            should_not_install = true;
            continue;
          }
          fsops_emit_mv("install", package, sysroot, path, "", path, ".zpksave",
                        &pkfsops);
          fsops_emit_install("install", package, sysroot, path, "", claim, pZip,
                             &pkfsops);
        }
      }
    } else {
      fsops_emit_install("install", package, sysroot, path, "", claim, pZip,
                         &pkfsops);
    }
  }

  if (should_not_install) {
    mz_zip_reader_end(pZip);
    free(pZip);
    return ERR_UNSAFE;
  }

  // if all good transfer ownership of newly created pkgs and zip
  vec_fsop_t_append(fsops, &pkfsops);
  vec_fsop_t_clear(&pkfsops);
  vec_mz_zip_archive_ptr_push(zips, &pZip);

  // add to index
  merge_claims_into_index(index, package, &claims);
  return ERR_OK;
}

ErrVal uninstall_package(
    // appends to this if the operation would succeed
    vec_fsop_t *fsops,
    // fsops refer to indexes in the zips. appends to this if the operation
    // would succeed
    vec_mz_zip_archive_ptr *zips,
    // file index (for file conflict identification)
    llrb_path_indexdata *index,
    // user given package name (For logging)
    char *package,
    // zip file to uninstall
    char *package_path,
    // where to uninstall
    char *sysroot,
    // protected paths
    vec_char_ptr *protected_paths) {

  mz_zip_archive *pZip = malloc(sizeof(mz_zip_archive));
  mz_zip_zero_struct(pZip);
  if (!mz_zip_reader_init_file(pZip, package_path, 0)) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                   "install %s: could not open zip archive %s: %s", package,
                   package_path,
                   mz_zip_get_error_string(mz_zip_get_last_error(pZip)));
    return ERR_UNKNOWN;
  }
  llrb_char_ptr_fileclaim claims;
  collect_file_claims(pZip, "install", package, &claims);
  defer delete_file_claims(&claims);
  mz_zip_reader_end(pZip);
  free(pZip);

  // remove claims from index. Must use
  // Bug: use zip name instead bc version conflict can happen here. Also in
  // general
  remove_claims_from_index(index, package, &claims);

  vec_fsop_t pkfsops;
  vec_fsop_t_init(&pkfsops);
  defer vec_fsop_t_delete_and_freeowned(&pkfsops);

  // set this to true if we reach a package-fatal error that means it shouldn't
  // be removed we want to surface all errors though rather than just the
  // first one
  bool should_not_proceed = false;

  // remember: we go in reverse direction than installation,
  // because we must remove children before parents
  llrb_char_ptr_fileclaim_iter iter;
  llrb_char_ptr_fileclaim_iter_rbegin(&claims, &iter);
  char_ptr path;
  FileClaim claim;
  while (llrb_char_ptr_fileclaim_iter_prev(&iter, &path, &claim)) {
    bool exists;
    bool matchesus;
    bool matchesother;
    char *otherpackage = NULL;
    compute_match_status(index, claims, package, sysroot, path, claim, &exists,
                         &matchesus, &matchesother, &otherpackage);

    if (exists) {
      if (matchesus) {
        if (matchesother) {
          // if it matches some other package too, we do nothing
          // it'll get removed when the last owning package is removed
        } else {
          // if it matches us and doesn't match other, it belongs to us and we
          // remove it
          if (claim.is_directory) {
            fsops_emit_rmdir("uninstall", package, sysroot, path, "", &pkfsops);
          } else {
            fsops_emit_rm("uninstall", package, sysroot, path, "", &pkfsops);
          }
        }
      } else if (matchesother) {
        LOG_ERROR_ARGS(ERR_LEVEL_WARN,
                       "uninstall %s: file %s seems to match %s instead of "
                       "this package. Leaving in place.",
                       package, path, otherpackage);
      } else {
        LOG_ERROR_ARGS(ERR_LEVEL_WARN,
                       "uninstall %s: file %s does not match any package. "
                       "Leaving in place.",
                       package, path);
      }
    } else {
      LOG_ERROR_ARGS(ERR_LEVEL_WARN, "uninstall %s: file %s is already missing",
                     package, path);
    }
  }

  if (should_not_proceed) {
    return ERR_UNSAFE;
  }

  // if all good transfer ownership of newly created pkgs
  vec_fsop_t_append(fsops, &pkfsops);
  vec_fsop_t_clear(&pkfsops);
  return ERR_OK;
}

ErrVal resolve_package_paths(vec_char_ptr *package_paths,
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
    LOG_ERROR_ARGS(ERR_LEVEL_INFO, "resolve: listing files in %s", repository);
    if (listdir_portable(repository, &entries[i], NULL) != 0) {
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

    char *package_path;
    asprintf(&package_path, "%s/%s", best_repository, best_package_entry);
    vec_char_ptr_push(package_paths, &package_path);
  }

  return ERR_OK;
}
