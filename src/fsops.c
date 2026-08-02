#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "constants.h"
#include "error.h"
#include "fsops.h"
#include "miniz/miniz.h"

ErrVal install_package(ZpkConfiguration *pConf, char *package) {
  LOG_ERROR_ARGS(ERR_LEVEL_INFO, "installing package %s", package);
  char* package_path = malloc(strlen(pConf->pkgs_path) + 1 + strlen(package) + 1);
  sprintf(package_path, "%s/%s", pConf->pkgs_path, package);

  FILE *f = fopen(package_path, "r");
  if (f == NULL) {
    free(package_path);
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "could not open package file %s: %s",
                   package_path, strerror(errno));

    return ERR_NOSUCHFILE;
  }
  fclose(f);

  mz_zip_reader_extract_iter_new


  return ERR_OK;
}

ErrVal uninstall_package(ZpkConfiguration *pConf, char *package) {
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
