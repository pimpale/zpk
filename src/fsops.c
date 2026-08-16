#include <assert.h>
#include <errno.h>
#include <stddefer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "apkver/apkver.h"
#include "error.h"
#include "fsop.h"
#include "fsops.h"
#include "index.h"
#include "instances/llrb_char_ptr_fileclaim.h"
#include "instances/llrb_path_indexdata.h"
#include "instances/vec_char_ptr.h"
#include "instances/vec_fsop_t.h"
#include "instances/vec_mz_zip_archive_ptr.h"
#include "miniz/miniz.h"
#include "oscompatlayer.h"
#include "pathutils.h"

static int copy_file(const char *from, const char *to) {
  FILE *src = fopen(from, "rb");
  if (src == NULL) {
    return -1;
  }
  FILE *dst = fopen(to, "wb");
  if (dst == NULL) {
    int saved_errno = errno;
    fclose(src);
    errno = saved_errno;
    return -1;
  }

  char buf[64 * 1024];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, src)) > 0) {
    if (fwrite(buf, 1, n, dst) != n) {
      int saved_errno = errno;
      fclose(src);
      fclose(dst);
      errno = saved_errno;
      return -1;
    }
  }
  if (ferror(src) != 0) {
    int saved_errno = errno;
    fclose(src);
    fclose(dst);
    errno = saved_errno;
    return -1;
  }
  fclose(src);
  // the last buffered write can only fail here, so this close is load-bearing
  if (fclose(dst) != 0) {
    return -1;
  }
  return 0;
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
    case FSOP_COPY:
      LOG_ERROR_ARGS(ERR_LEVEL_INFO, "%s %s: copying from %s to %s", o->op,
                     o->pkg, o->copy.from, o->copy.to);
      if (!dry_run && copy_file(o->copy.from, o->copy.to) != 0) {
        LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                       "%s %s: failed to copy file %s to %s: %s", o->op, o->pkg,
                       o->copy.from, o->copy.to, strerror(errno));
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
      LOG_ERROR_ARGS(ERR_LEVEL_INFO, "%s %s: removing file %s", o->op, o->pkg,
                     o->removefile.path);
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
      LOG_ERROR_ARGS(ERR_LEVEL_INFO, "%s %s: removing directory %s", o->op,
                     o->pkg, o->rmdir.path);
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
    fileindex_t *index,
    // for logging only
    const char *op,
    // package
    const char *package,
    // path to the file we're considering (relative to sysroot)
    char *fullpath,
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

  FileStatus *filestatus =
      fileindex_ensure_actual(index, fullpath, op, package);
  if (filestatus == NULL) {
    // something went wrong, bail
    return ERR_UNKNOWN;
  }
  if (!filestatus->exists) {
    return ERR_OK;
  }
  *exists = true;
  *matchesus = is_match(*filestatus, claim);

  IndexData *indexdata;
  if (llrb_path_indexdata_get_ref(&index->index, &fullpath, &indexdata)) {
    llrb_char_ptr_fileclaim_iter iter;
    llrb_char_ptr_fileclaim_iter_begin(&indexdata->claims, &iter);
    char_ptr package1;
    FileClaim claim1;
    while (llrb_char_ptr_fileclaim_iter_next(&iter, &package1, &claim1)) {
      if (strcmp(package1, package) == 0) {
        continue;
      }
      if (is_match(*filestatus, claim1)) {
        *matchesother = true;
        *otherpackage = package1;
        break;
      }
    }
  }

  return ERR_OK;
}

static bool in_protected_paths(vec_char_ptr *protected_paths, char *path) {
  // TODO: implement this
  return false;
}

void fsops_emit_mkdir(
    // for logging only
    const char *op, const char *pkg,
    // takes ownership of path
    char *path,
    // appends to this
    vec_fsop_t *fsops,
    // simulates the behavior in fileindex
    fileindex_t *index) {
  fsop_t o = {.op = op, .pkg = strdup(pkg)};
  o.kind = FSOP_MKDIR;
  o.mkdir.path = path;
  vec_fsop_t_push(fsops, &o);

  FileStatus *status = fileindex_status_or_default(index, path, NULL);
  status->changed_during_transaction = true;
  status->exists = true;
}

void fsops_emit_install(
    // for logging only
    const char *op, const char *pkg,
    // takes ownership of path
    char *path, FileClaim claim, mz_zip_archive *zip,
    // appends to this
    vec_fsop_t *fsops,
    // simulates the behavior in fileindex
    fileindex_t *index) {

  if (claim.is_directory) {
    fsops_emit_mkdir(op, pkg, path, fsops, index);
  } else {
    fsop_t o = {.op = op, .pkg = strdup(pkg)};
    o.kind = FSOP_CREATEFILE;
    o.createfile.path = path;
    o.createfile.file_index = claim.file_index;
    o.createfile.zip = zip;

    vec_fsop_t_push(fsops, &o);

    // update filestatus
    FileStatus *status = fileindex_status_or_default(index, path, NULL);
    status->changed_during_transaction = true;
    status->exists = true;
    status->is_directory = false;
    status->crc32 = claim.crc32;
  }
}

ErrVal fsops_emit_mkdir_p( // logging only
    const char *op, const char *pkg,
    // takes ownership
    char *path,
    // file index op
    vec_fsop_t *fsops,
    // simulates the behavior in fileindex
    fileindex_t *index) {
  char *p = path;
  while (true) {
    bool nullbyte = *p == '\0';
    if (nullbyte || *p == '/') {
      *p = '\0';
      if (strlen(path) > 0) {
        FileStatus *fs = fileindex_ensure_actual(index, path, op, pkg);
        if (!(fs->exists && fs->is_directory)) {
          char *ownedpath = strdup(path);
          fsops_emit_mkdir(op, pkg, ownedpath, fsops, index);
        }
      }
      *p = '/';
    }
    if (nullbyte) {
      break;
    }
    p++;
  }
  free(path);
  return ERR_OK;
}

void fsops_emit_rm(
    // logging
    const char *op, const char *pkg,
    // takes ownership
    char *path, vec_fsop_t *fsops,
    // simulates the behavior in fileindex
    fileindex_t *index) {
  fsop_t o = {.op = op, .pkg = strdup(pkg)};
  o.kind = FSOP_REMOVEFILE;
  o.removefile.path = path;
  vec_fsop_t_push(fsops, &o);

  FileStatus *status = fileindex_status_or_default(index, path, NULL);
  status->changed_during_transaction = true;
  status->exists = false;
}

void fsops_emit_rmdir(const char *op, const char *pkg, char *path,
                      vec_fsop_t *fsops,
                      // simulates the behavior in fileindex
                      fileindex_t *index) {
  fsop_t o = {.op = op, .pkg = strdup(pkg)};
  o.kind = FSOP_RMDIR;
  o.rmdir.path = path;
  vec_fsop_t_push(fsops, &o);

  FileStatus *status = fileindex_status_or_default(index, path, NULL);
  status->changed_during_transaction = true;
  status->exists = false;
}

void fsops_emit_mv(const char *op, const char *pkg, char *from, char *to,
                   vec_fsop_t *fsops, fileindex_t *index) {
  fsop_t o = {.op = op,
              .pkg = strdup(pkg),
              .kind = FSOP_RENAME,
              .rename = {.from = from, .to = to}};
  vec_fsop_t_push(fsops, &o);

  // these may alias
  FileStatus *fromstatus = fileindex_status_or_default(index, from, NULL);
  FileStatus *tostatus = fileindex_status_or_default(index, to, NULL);

  fromstatus->changed_during_transaction = true;
  *tostatus = *fromstatus;

  fromstatus->exists = false;
  tostatus->exists = true;
}

void fsops_emit_cp(const char *op, const char *pkg, char *from, char *to,
                   vec_fsop_t *fsops, fileindex_t *index) {
  fsop_t o = {.op = op,
              .pkg = strdup(pkg),
              .kind = FSOP_COPY,
              .copy = {.from = from, .to = to}};
  vec_fsop_t_push(fsops, &o);

  // these may alias
  FileStatus *fromstatus = fileindex_status_or_default(index, from, NULL);
  FileStatus *tostatus = fileindex_status_or_default(index, to, NULL);

  *tostatus = *fromstatus;
  tostatus->changed_during_transaction = true;
}

// rm -rf on the path
// needed for when there is something blocking the way on our installation, and
// it's not a protected path
// doesn't log errors if it's not actually a directory. Only logs errors if we
// fail to read a file
ErrVal fsops_emit_rm_rf(const char *op, const char *pkg,
                        // takes ownership
                        char *path, vec_fsop_t *fsops,
                        // simulates the behavior in fileindex
                        fileindex_t *index

) {
  switch (path_type_portable(path)) {
  case PATH_TYPE_MISSING:
    free(path);
    return ERR_OK;
  case PATH_TYPE_FILE:
  case PATH_TYPE_OTHER:
    fsops_emit_rm(op, pkg, path, fsops, index);
    return ERR_OK;
  case PATH_TYPE_ERROR:
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "%s %s: could not stat %s: %s", op, pkg,
                   path, strerror(errno));
    free(path);
    return ERR_NOSUCHFILE;
  case PATH_TYPE_DIR:
    break;
  }

  vec_char_ptr dirs_to_visit;
  vec_char_ptr_init(&dirs_to_visit);
  vec_char_ptr_push(&dirs_to_visit, &path);
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
      fsops_emit_rm(op, pkg, joinstr3(p, "/", entry), fsops, index);
    }
    vec_char_ptr_clear_and_freeowned(&file_entries);

    // recurse into subfolders
    for (size_t i = 0; i < vec_char_ptr_len(&dir_entries); i++) {
      char *entry = *vec_char_ptr_at(&dir_entries, i);
      char *subdir = joinstr3(p, "/", entry);
      vec_char_ptr_push(&dirs_to_visit, &subdir);
    }
    vec_char_ptr_clear_and_freeowned(&dir_entries);
  }

  // now delete the seen dirs in reverse order
  for (size_t i_r = vec_char_ptr_len(&dirs_visited); i_r > 0; i_r--) {
    size_t i = i_r - 1;
    fsops_emit_rmdir(op, pkg, *vec_char_ptr_at(&dirs_visited, i), fsops, index);
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
      return joinstr2(dest_prefix, path + prefix_strlen);
    }
  }
  return strdup(path);
}

ErrVal fsops_emit_install_package(
    // appends to this if the operation would succeed
    vec_fsop_t *fsops,
    // fsops refer to indexes in the zips. appends to this if the operation
    // would succeed
    vec_mz_zip_archive_ptr *zips,
    // file index (for file conflict identification)
    fileindex_t *index,
    // zip file to install
    char *package_path,
    // where to install
    char *sysroot,
    // protected paths
    vec_char_ptr *protected_paths) {

  char *package = basename_m(package_path);
  assert(package != NULL);

  bool changed_during_transaction = false;
  if (fileindex_contains_package(index, package, &changed_during_transaction)) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "install %s: package %s %s", package,
                   package,
                   changed_during_transaction
                       ? "was already installed earlier in this transaction"
                       : "is already installed");
    return ERR_UNKNOWN;
  }

  llrb_char_ptr_fileclaim claims;
  mz_zip_archive *pZip = malloc(sizeof(mz_zip_archive));
  mz_zip_zero_struct(pZip);
  if (!mz_zip_reader_init_file(pZip, package_path, 0)) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                   "install %s: could not open zip archive %s: %s", package,
                   package_path,
                   mz_zip_get_error_string(mz_zip_get_last_error(pZip)));
    free(pZip);
    return ERR_UNKNOWN;
  }
  fileclaims_collect(pZip, "install", package, sysroot, &claims);
  defer fileclaims_delete(&claims);

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
    compute_match_status(index, "install", package, path, claim, &exists,
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
          if (fsops_emit_rm_rf("install", package, joinstr2(path, ".zpknew"),
                               &pkfsops, index) != ERR_OK) {
            should_not_install = true;
            continue;
          }
          LOG_ERROR_ARGS(
              ERR_LEVEL_WARN,
              "install %s: installing new %s as %s.zpknew (no match + "
              "in protected path)",
              package, path, path);
          fsops_emit_install("install", package, joinstr2(path, ".zpknew"),
                             claim, pZip, &pkfsops, index);

          // emit diversion for future files
          char *src = strdup(path);
          char *dest = joinstr2(path, ".zpknew");
          vec_char_ptr_push(&src_diverted_prefixes, &src);
          vec_char_ptr_push(&dest_diverted_prefixes, &dest);
        } else {
          LOG_ERROR_ARGS(
              ERR_LEVEL_WARN,
              "install %s: renaming old %s to %s.zpksave (no match + "
              "not in protected path)",
              package, path, path);
          if (fsops_emit_rm_rf("install", package, joinstr2(path, ".zpksave"),
                               &pkfsops, index) != ERR_OK) {
            should_not_install = true;
            continue;
          }
          fsops_emit_mv("install", package, strdup(path),
                        joinstr2(path, ".zpksave"), &pkfsops, index);
          fsops_emit_install("install", package, strdup(path), claim, pZip,
                             &pkfsops, index);
        }
      }
    } else {
      fsops_emit_install("install", package, strdup(path), claim, pZip,
                         &pkfsops, index);
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
  // must work because we already checked that package_path was not in the index
  ErrVal merge_result = merge_claims_into_index(index, package, &claims, true);
  assert(merge_result == ERR_OK);

  return ERR_OK;
}

ErrVal fsops_emit_uninstall_package(
    // appends to this if the operation would succeed
    vec_fsop_t *fsops,
    // fsops refer to indexes in the zips. appends to this if the operation
    // would succeed
    vec_mz_zip_archive_ptr *zips,
    // file index (for file conflict identification)
    fileindex_t *index,
    // zip file to uninstall
    char *package_path,
    // where to uninstall
    char *sysroot,
    // protected paths
    vec_char_ptr *protected_paths) {

  char *package = basename_m(package_path);
  assert(package != NULL);

  bool changed_during_transaction = false;
  if (!fileindex_contains_package(index, package,
                                  &changed_during_transaction)) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "uninstall %s: package %s %s", package,
                   package,
                   changed_during_transaction
                       ? "would be uninstalled earlier during this transaction"
                       : "is not currently installed");
    return ERR_UNKNOWN;
  }

  mz_zip_archive *pZip = malloc(sizeof(mz_zip_archive));
  mz_zip_zero_struct(pZip);
  if (!mz_zip_reader_init_file(pZip, package_path, 0)) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR,
                   "uninstall %s: could not open zip archive %s: %s", package,
                   package_path,
                   mz_zip_get_error_string(mz_zip_get_last_error(pZip)));
    free(pZip);
    return ERR_UNKNOWN;
  }
  llrb_char_ptr_fileclaim claims;
  fileclaims_collect(pZip, "uninstall", package, sysroot, &claims);
  defer fileclaims_delete(&claims);
  mz_zip_reader_end(pZip);
  free(pZip);

  vec_fsop_t pkfsops;
  vec_fsop_t_init(&pkfsops);
  defer vec_fsop_t_delete_and_freeowned(&pkfsops);

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
    compute_match_status(index, "uninstall", package, path, claim, &exists,
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
            fsops_emit_rmdir("uninstall", package, strdup(path), &pkfsops,
                             index);
          } else {
            fsops_emit_rm("uninstall", package, strdup(path), &pkfsops, index);
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

  remove_claims_from_index(index, package, &claims, true);

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

    char *package_path = joinstr3(best_repository, "/", best_package_entry);
    vec_char_ptr_push(package_paths, &package_path);
  }

  return ERR_OK;
}
