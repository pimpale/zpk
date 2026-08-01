#include <stdio.h>
#include <stdlib.h>

#include "configuration.h"
#include "fsops.h"

static void do_fetch(ZpkConfiguration *pConf, vec_char_ptr *pTargets,
                    char *path) {
  (void)pConf;
  (void)pTargets;
  (void)path;
}

static void do_add(ZpkConfiguration *pConf, vec_char_ptr *pTargets) {
  do_fetch(pConf, pTargets, pConf->pkgs_path);
  for (size_t i = 0; i < vec_char_ptr_len(pTargets); i++) {
    install_package(pConf, *vec_char_ptr_at(pTargets, i));
  }
}

static void do_del(ZpkConfiguration *pConf, vec_char_ptr *pTargets) {
  for (size_t i = 0; i < vec_char_ptr_len(pTargets); i++) {
    uninstall_package(pConf, *vec_char_ptr_at(pTargets, i));
  }
}

static void do_upgrade(ZpkConfiguration *pConf, vec_char_ptr *pTargets) {
  (void)pConf;
  (void)pTargets;
}

// reinstall packages whose files are missing or corrupt
static void do_fix(ZpkConfiguration *pConf, vec_char_ptr *pTargets) {
  (void)pConf;
  (void)pTargets;
}

static void do_list(ZpkConfiguration *pConf, bool installed, bool upgradable,
                   bool available) {
  (void)pConf;
  (void)installed;
  (void)upgradable;
  (void)available;
}

// which package owns each path
static void do_owner(ZpkConfiguration *pConf, char *path) {
  (void)pConf;
  (void)path;
}

int main(int argc, char **argv) {
  ZpkConfiguration configuration;
  ZpkOperation operation;
  parse_args(argc, argv, &configuration, &operation);

  switch (operation.op) {
  case ZPK_OP_ADD:
    do_add(&configuration, operation.add.targets);
    break;
  case ZPK_OP_FETCH:
    do_fetch(&configuration, operation.fetch.targets,
            operation.fetch.output_dir);
    break;
  case ZPK_OP_DEL:
    do_del(&configuration, operation.del.targets);
    break;
  case ZPK_OP_UPGRADE:
    do_upgrade(&configuration, operation.upgrade.targets);
    break;
  case ZPK_OP_FIX:
    do_fix(&configuration, operation.fix.targets);
    break;
  case ZPK_OP_LIST:
    do_list(&configuration, operation.list.installed, operation.list.upgradable,
           operation.list.available);
    break;
  case ZPK_OP_OWNER:
    do_owner(&configuration, operation.owner.path);
    break;
  }

  delete_ZpkOperation(&operation);
  delete_ZpkConfiguration(&configuration);
  return 0;
}
