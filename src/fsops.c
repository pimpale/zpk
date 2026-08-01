#include <stdio.h>
#include <stdlib.h>

#include "error.h"
#include "constants.h"
#include "fsops.h"

ErrVal install_package(ZpkConfiguration *pConf, char *package) {
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
