#include "repository.h"

#include <assert.h>
#include <errno.h>
#include <stddefer.h>
#include <stdlib.h>
#include <string.h>

#include "apkver/apkver.h"
#include "error.h"
#include "instances/llrb_char_ptr_bestrepo.h"
#include "instances/vec_char_ptr.h"
#include "oscompatlayer.h"
#include "pathutils.h"

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

ErrVal resolve_package_paths_installed(vec_char_ptr *package_paths,
                                       char *directory, vec_char_ptr *packages,
                                       bool none_is_all) {
  vec_char_ptr installedrepo;
  vec_char_ptr_init(&installedrepo);
  defer vec_char_ptr_delete(&installedrepo);
  vec_char_ptr_push(&installedrepo, &directory);
  return resolve_package_paths_repositories(package_paths, &installedrepo,
                                            packages, none_is_all);
}

// there is NO guarantee that package_paths will be in the same order as
// packages. (although currently it is, as long as !insert_all)
ErrVal resolve_package_paths_repositories(vec_char_ptr *package_paths,
                                          vec_char_ptr *repositories,
                                          vec_char_ptr *packages,
                                          bool none_is_all) {
  size_t n_packages;
  if (packages == NULL) {
    n_packages = 0;
    assert(none_is_all);
  } else {
    n_packages = vec_char_ptr_len(packages);
  }
  bool insert_all = none_is_all && (n_packages == 0);

  llrb_char_ptr_bestrepo bestrepo;
  llrb_char_ptr_bestrepo_new(&bestrepo);
  defer llrb_char_ptr_bestrepo_delete_and_freeowned(&bestrepo);

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
      char **pEntry = vec_char_ptr_at(&entries, e);

      char *entrypackagename;
      char *entryversion;
      if (!package_data(&entrypackagename, &entryversion, *pEntry, ".zip")) {
        continue;
      }
      defer free(entrypackagename);
      defer free(entryversion);

      BestRepo *brp;
      if (!llrb_char_ptr_bestrepo_get_ref(&bestrepo, &entrypackagename, &brp)) {
        BestRepo br = {.entry = *pEntry,
                       .version = entryversion,
                       .repository = repository};
        bool inserted = llrb_char_ptr_bestrepo_insert(
                            &bestrepo, &entrypackagename, &br) != NULL;
        assert(inserted);
        // take ownership of entryversion and entrypackagename
        entryversion = NULL;
        entrypackagename = NULL;
        *pEntry = NULL;

      } else {
        if (apk_version_compare(APK_BLOB_STR(entryversion),
                                APK_BLOB_STR(brp->version)) ==
            APK_VERSION_GREATER) {
          // free existing fields
          free(brp->version);
          free(brp->entry);

          // take ownership of entry and entryversion
          brp->version = entryversion;
          brp->entry = *pEntry;
          brp->repository = repository;

          entryversion = NULL;
          *pEntry = NULL;
        }
      }
    }
  }

  if (insert_all) {
    llrb_char_ptr_bestrepo_iter iter;
    llrb_char_ptr_bestrepo_iter_begin(&bestrepo, &iter);
    char *key;
    BestRepo br;
    while (llrb_char_ptr_bestrepo_iter_next(&iter, &key, &br)) {
      char *package_path = joinpath(br.repository, br.entry);
      vec_char_ptr_push(package_paths, &package_path);
    }
  } else {
    bool should_error = false;

    for (size_t i = 0; i < n_packages; i++) {
      char *package = *vec_char_ptr_at(packages, i);
      BestRepo br;
      if (!llrb_char_ptr_bestrepo_get(&bestrepo, &package, &br)) {
        LOG_ERROR_ARGS(
            ERR_LEVEL_ERROR,
            "resolve: did not find validly named package %s in any repository",
            package);
        should_error = true;
        continue;
      }

      char *package_path = joinpath(br.repository, br.entry);
      vec_char_ptr_push(package_paths, &package_path);
    }
    if (should_error) {
      return ERR_NOSUCHFILE;
    }
  }
  return ERR_OK;
}
