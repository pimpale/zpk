#include <stdio.h>
#include <stdlib.h>

#include "configuration.h"
#include "fsops.h"

static void do_fetch(ZipkgConfiguration *pConf, vec_char_ptr *pTargets,
                    char *path) {
  (void)pConf;
  (void)pTargets;
  (void)path;
}

static void do_add(ZipkgConfiguration *pConf, vec_char_ptr *pTargets) {
  do_fetch(pConf, pTargets, pConf->pkgs_path);
  for (size_t i = 0; i < vec_char_ptr_len(pTargets); i++) {
    install_package(pConf, *vec_char_ptr_at(pTargets, i));
  }
}

static void do_del(ZipkgConfiguration *pConf, vec_char_ptr *pTargets) {
  for (size_t i = 0; i < vec_char_ptr_len(pTargets); i++) {
    uninstall_package(pConf, *vec_char_ptr_at(pTargets, i));
  }
}

static void do_upgrade(ZipkgConfiguration *pConf, vec_char_ptr *pTargets) {
  (void)pConf;
  (void)pTargets;
}

// reinstall packages whose files are missing or corrupt
static void do_fix(ZipkgConfiguration *pConf, vec_char_ptr *pTargets) {
  (void)pConf;
  (void)pTargets;
}

static void do_list(ZipkgConfiguration *pConf, bool installed, bool upgradable,
                   bool available) {
  (void)pConf;
  (void)installed;
  (void)upgradable;
  (void)available;
}

// which package owns each path
static void do_owner(ZipkgConfiguration *pConf, char *path) {
  (void)pConf;
  (void)path;
}

int main(int argc, char **argv) {
  ZipkgConfiguration configuration;
  ZipkgOperation operation;
  parse_args(argc, argv, &configuration, &operation);

  switch (operation.op) {
  case ZIPKG_OP_ADD:
    do_add(&configuration, operation.add.targets);
    break;
  case ZIPKG_OP_FETCH:
    do_fetch(&configuration, operation.fetch.targets,
            operation.fetch.output_dir);
    break;
  case ZIPKG_OP_DEL:
    do_del(&configuration, operation.del.targets);
    break;
  case ZIPKG_OP_UPGRADE:
    do_upgrade(&configuration, operation.upgrade.targets);
    break;
  case ZIPKG_OP_FIX:
    do_fix(&configuration, operation.fix.targets);
    break;
  case ZIPKG_OP_LIST:
    do_list(&configuration, operation.list.installed, operation.list.upgradable,
           operation.list.available);
    break;
  case ZIPKG_OP_OWNER:
    do_owner(&configuration, operation.owner.path);
    break;
  }

  delete_ZipkgOperation(&operation);
  delete_ZipkgConfiguration(&configuration);
  return 0;
}
