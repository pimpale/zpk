#include "repository.h"

#include <asprintf/asprintf.h>
#include <assert.h>
#include <errno.h>
#include <stddefer.h>
#include <stdlib.h>
#include <string.h>

#include "apkver/apkver.h"
#include "error.h"
#include "fileutils.h"
#include "instances/llrb_char_ptr_resolvedpackage.h"
#include "instances/vec_char_ptr.h"
#include "oscompatlayer.h"
#include "pathutils.h"
#include "resolvedpackage.h"

bool version_is_greater(char *a, char *b) {
  return apk_version_compare_str(a, b) == APK_VERSION_GREATER;
}

// doesn't allocate
static apk_blob_t package_version_blob(char *entry, const char *suffix) {
  if (!endswith(entry, suffix)) {
    return APK_BLOB_NULL;
  }

  size_t entry_strlen = strlen(entry);
  size_t suffix_strlen = strlen(suffix);

  char *end = entry + (entry_strlen - suffix_strlen);
  for (char *vstart_m1 = end; vstart_m1 > entry; vstart_m1--) {
    if (*vstart_m1 != '-') {
      continue;
    }
    char *vstart = vstart_m1 + 1;
    apk_blob_t candidate_version = APK_BLOB_PTR_LEN(vstart, end - vstart);

    if (!apk_version_validate(candidate_version)) {
      continue;
    }
    return candidate_version;
  }
  return APK_BLOB_NULL;
}

// allocates

bool package_data(
    // if not NULL, allocates a string containing just the package name
    char **package_name,
    // if not NULL, allocates a string containing just the package name
    char **package_version,
    // package basename
    char *entry,
    // .zip, or .uninstalling.zip or something.
    // Will be ignored from the package name
    const char *suffix) {
  apk_blob_t version = package_version_blob(entry, suffix);
  if (APK_BLOB_IS_NULL(version)) {
    return false;
  }
  if (package_version != NULL) {
    *package_version = strndup(version.ptr, version.len);
  }
  if (package_name != NULL) {
    *package_name =
        strndup(entry, strlen(entry) - (1 + version.len + strlen(suffix)));
  }
  return true;
}

ErrVal resolve_package_paths_installed(
    llrb_char_ptr_resolvedpackage *resolved_packages, char *directory,
    vec_char_ptr *packages, bool none_is_all) {
  vec_char_ptr installedrepo;
  vec_char_ptr_init(&installedrepo);
  defer vec_char_ptr_delete(&installedrepo);
  vec_char_ptr_push(&installedrepo, &directory);
  ErrVal err = resolve_package_paths_repositories(
      resolved_packages, &installedrepo, packages, none_is_all);
  if (err != ERR_OK) {
    return err;
  }

  llrb_char_ptr_resolvedpackage_iter iter;
  llrb_char_ptr_resolvedpackage_iter_begin(resolved_packages, &iter);
  ResolvedPackage *rp;
  while (llrb_char_ptr_resolvedpackage_iter_next_ref(&iter, NULL, &rp)) {
    asprintf(&rp->package_path, "%s/%s-%s.zip", rp->repository, rp->package,
             rp->version);
  }
  return ERR_OK;
}

// there is NO guarantee that package_paths will be in the same order as
// packages. (although currently it is, as long as !insert_all)
ErrVal resolve_package_paths_repositories(
    llrb_char_ptr_resolvedpackage *resolved_packages,
    vec_char_ptr *repositories, vec_char_ptr *packages, bool none_is_all) {
  size_t n_packages;
  if (packages == NULL) {
    n_packages = 0;
    assert(none_is_all);
  } else {
    n_packages = vec_char_ptr_len(packages);
  }
  bool insert_all = none_is_all && (n_packages == 0);

  // fetch a list of the packages in the repositories
  size_t n_repositories = vec_char_ptr_len(repositories);

  for (size_t r = 0; r < n_repositories; r++) {
    char *repository = *vec_char_ptr_at(repositories, r);

    vec_char_ptr entries;
    vec_char_ptr_init(&entries);
    defer vec_char_ptr_delete_and_freeowned(&entries);

    if (listdir_portable(repository, &entries, NULL) != 0) {
      LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "resolve: unable to list files in %s: %s",
                     repository, strerror(errno));
      return ERR_NOSUCHFILE;
    }

    size_t n_entries = vec_char_ptr_len(&entries);
    for (size_t e = 0; e < n_entries; e++) {
      char *entry = *vec_char_ptr_at(&entries, e);

      char *entrypackagename;
      char *entryversion;
      if (!package_data(&entrypackagename, &entryversion, entry, ".zip")) {
        continue;
      }
      defer free(entrypackagename);
      defer free(entryversion);

      ResolvedPackage *brp;
      if (!llrb_char_ptr_resolvedpackage_get_ref(resolved_packages,
                                                 &entrypackagename, &brp)) {
        ResolvedPackage br = {.package = strdup(entrypackagename),
                              .version = strdup(entryversion),
                              .repository = strdup(repository),
                              .package_path = NULL};
        char *key = strdup(entrypackagename);
        brp =
            llrb_char_ptr_resolvedpackage_insert(resolved_packages, &key, &br);
        assert(brp != NULL);

      } else {
        if (apk_version_compare(APK_BLOB_STR(entryversion),
                                APK_BLOB_STR(brp->version)) ==
            APK_VERSION_GREATER) {
          // free existing version and repository
          free(brp->version);
          free(brp->repository);

          // take ownership of entry and entryversion
          brp->version = strdup(entryversion);
          brp->repository = strdup(repository);
        }
      }
    }
  }

  if (!insert_all) {
    bool should_error = false;

    // get the packages that weren't specified in the packages vector
    vec_char_ptr todelete;
    vec_char_ptr_init(&todelete);
    defer vec_char_ptr_delete(&todelete);
    {
      llrb_char_ptr_resolvedpackage_iter iter;
      llrb_char_ptr_resolvedpackage_iter_begin(resolved_packages, &iter);
      char *key;
      ResolvedPackage rp;
      while (llrb_char_ptr_resolvedpackage_iter_next(&iter, &key, &rp)) {
        bool keep = false;
        for (size_t i = 0; i < n_packages; i++) {
          char *package = *vec_char_ptr_at(packages, i);
          if (strcmp(key, package) == 0) {
            keep = true;
            break;
          }
        }
        if (!keep) {
          vec_char_ptr_push(&todelete, &key);
        }
      }
    }

    // now actually remove those from the tree
    for (size_t i = 0; i < vec_char_ptr_len(&todelete); i++) {
      char *key = *vec_char_ptr_at(&todelete, i);
      ResolvedPackage value;
      bool removed = llrb_char_ptr_resolvedpackage_remove(resolved_packages,
                                                          &key, NULL, &value);
      assert(removed);
      free(key);
      delete_ResolvedPackage(&value);
    }

    // if there exists a package in the package vector not present in the llrb,
    // raise an error
    for (size_t i = 0; i < n_packages; i++) {
      char *package = *vec_char_ptr_at(packages, i);
      if (!llrb_char_ptr_resolvedpackage_get(resolved_packages, &package,
                                             NULL)) {
        LOG_ERROR_ARGS(
            ERR_LEVEL_ERROR,
            "resolve: did not find validly named package %s in any repository",
            package);
        should_error = true;
        continue;
      }
    }
    if (should_error) {
      return ERR_NOSUCHFILE;
    }
  }
  return ERR_OK;
}

ErrVal resolve_and_fetch_package_paths_repositories(
    llrb_char_ptr_resolvedpackage *resolved_packages,
    vec_char_ptr *repositories, vec_char_ptr *packages, const char *directory,
    const char *suffix, bool none_is_all) {
  ErrVal v1 = resolve_package_paths_repositories(
      resolved_packages, repositories, packages, none_is_all);
  if (v1 != ERR_OK) {
    return v1;
  }
  bool should_error = false;
  // now fetch each file (for now just calculate the package path (in the
  // repository itself) and leave it at that) later versions will fetch to
  // directory/packagename.presuf.zip OR copy to there. the idea is to
  llrb_char_ptr_resolvedpackage_iter iter;
  llrb_char_ptr_resolvedpackage_iter_begin(resolved_packages, &iter);
  char *key;
  ResolvedPackage *rp;
  while (llrb_char_ptr_resolvedpackage_iter_next_ref(&iter, &key, &rp)) {
    char *src;
    char *dest;
    asprintf(&src, "%s/%s-%s.zip", rp->repository, rp->package, rp->version);
    asprintf(&dest, "%s/%s-%s%s", directory, rp->package, rp->version, suffix);
    rp->package_path = dest;

    // test if the file doesn't yet exist
    if (path_type_portable(dest) == PATH_TYPE_MISSING) {
      if (copy_file(src, dest) != 0) {
        LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "fetch %s: copying %s to %s: %s",
                       rp->package, src, dest, strerror(errno));
        should_error = true;
      }
    }
    free(src);
  }
  if (should_error) {
    return ERR_UNKNOWN;
  }
  return ERR_OK;
}
