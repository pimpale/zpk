#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "error.h"
#include "fsops.h"
#include "oscompatlayer.h"
#include "miniz/miniz.h"

// normalize the filename by removing leading slashes and rejecting invalid
// components if the filename is bad (ie uses .. or has some other problem) then
// return NULL caller must free the returned string if it is not NULL
static char *normalize(char *filename) {
  // reject if has a component equal to ..
  // a component is the phrase at the beginning, end, or between two slashes
  enum { CS_START, CS_ONEDOT, CS_TWODOTS, CS_OTHER } state = CS_START;
  for (char *p = filename;; p++) {
    char c = *p;
    if (c == '\0' || c == '/') {
      if (state == CS_TWODOTS) {
        return NULL;
      }
      state = CS_START;
      if (c == '\0') {
        break;
      }
    } else if (c == '.') {
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

  // package will be installed at sysroot, so remove leading slashes (redundant)
  while (*filename == '/') {
    filename++;
  }
  return strdup(filename);
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
    if (ensure_directory_exists(package, target) == ERR_OK &&
        !mz_zip_reader_extract_to_file(zip, file_stat.m_file_index, target,
                                       0)) {
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
  if (dry_run) {
    LOG_ERROR_ARGS(ERR_LEVEL_INFO,
                   "install %s: simulating; no changes will be made", package);
  }

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
    LOG_ERROR_ARGS(
        ERR_LEVEL_ERROR, "install %s: could not open zip archive %s: %s", package,
        package_path, mz_zip_get_error_string(mz_zip_get_last_error(&zip)));

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
  (void)dry_run; // uninstall doesn't remove anything yet, so nothing to gate
  char package_path[PATH_MAX];
  int n = snprintf(package_path, sizeof(package_path), "%s/%s",
                   pConf->pkgs_path, package);
  if (n < 0 || n >= (int)sizeof(package_path)) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "package path too long: %s/%s",
                   pConf->pkgs_path, package);
    return ERR_BADARGS;
  }
  FILE *f = fopen(package_path, "r");
  if (f == NULL) {
    return ERR_NOSUCHFILE;
  }
  fclose(f);
  (void)pConf;
  (void)package;
  return ERR_OK;
}
