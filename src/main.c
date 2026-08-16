#include <stddefer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "configuration.h"
#include "error.h"
#include "fsops.h"
#include "index.h"
#include "instances/llrb_char_ptr_fileclaim.h"
#include "instances/llrb_path_indexdata.h"
#include "instances/vec_char_ptr.h"
#include "instances/vec_fsop_t.h"
#include "instances/vec_mz_zip_archive_ptr.h"
#include "oscompatlayer.h"
#include "pathutils.h"

static int do_fetch(ZpkConfiguration *pConf, vec_char_ptr *pTargets,
                    char *path) {
  LOG_ERROR_ARGS(ERR_LEVEL_INFO, "fetching %zu targets to %s",
                 vec_char_ptr_len(pTargets), path);
  (void)pConf;
  (void)pTargets;
  (void)path;
  return 0;
}

static int do_add(ZpkConfiguration *pConf, vec_char_ptr *packages,
                  bool dry_run) {
  size_t n_targets = vec_char_ptr_len(packages);
  LOG_ERROR_ARGS(ERR_LEVEL_INFO, "installing %zu targets to %s", n_targets,
                 pConf->sysroot);

  // resolve packages to install
  vec_char_ptr package_paths;
  vec_char_ptr_init(&package_paths);
  defer vec_char_ptr_delete_and_freeowned(&package_paths);

  if (resolve_package_paths(&package_paths, &pConf->repositories, packages) !=
      ERR_OK) {
    LOG_ERROR(ERR_LEVEL_FATAL, "failed to resolve package paths");
    PANIC();
  }

  // build index
  fileindex_t index;
  fileindex_build(&index, pConf->sysroot, pConf->pkgs_path);
  defer fileindex_delete(&index);

  // create the fsops vec and the zips vec
  vec_fsop_t fsops;
  vec_fsop_t_init(&fsops);
  defer vec_fsop_t_delete_and_freeowned(&fsops);

  vec_mz_zip_archive_ptr zips;
  vec_mz_zip_archive_ptr_init(&zips);
  defer vec_mz_zip_archive_ptr_delete_and_freeowned(&zips);

  fsops_emit_mkdir_p("prepare", "install", strdup(pConf->sysroot), &fsops,
                     &index);

  bool should_proceed = true;
  for (size_t i = 0; i < n_targets; i++) {
    char *package = *vec_char_ptr_at(packages, i);
    char *package_path = *vec_char_ptr_at(&package_paths, i);

    // journal intent by moving the thing first. Then we can patch it up. if there's a crash.
    fsops_emit_cp("install", package, strdup(package_path),
                  joinstr3(pConf->pkgs_path, "/", basename_m(package_path)),
                  &fsops, &index);

    ErrVal err =
        fsops_emit_install_package(&fsops, &zips, &index, package_path,
                                   pConf->sysroot, &pConf->protected_paths);
    if (err != ERR_OK) {
      should_proceed = false;
      continue;
    }
  }
  if (!should_proceed) {
    return 1;
  }

  execute_fsops(&fsops, dry_run);
  return 0;
}

static int do_del(ZpkConfiguration *pConf, vec_char_ptr *packages,
                  bool dry_run) {
  size_t n_targets = vec_char_ptr_len(packages);
  LOG_ERROR_ARGS(ERR_LEVEL_INFO, "removing %zu targets from %s", n_targets,
                 pConf->sysroot);

  // resolve packages to remove
  vec_char_ptr package_paths;
  vec_char_ptr_init(&package_paths);
  defer vec_char_ptr_delete_and_freeowned(&package_paths);

  // we only look at packages that are actually installed
  vec_char_ptr installedrepo;
  vec_char_ptr_init_cap(&installedrepo, 1);
  vec_char_ptr_push(&installedrepo, &pConf->pkgs_path);
  // no ownership over pkgs path so no freeowned
  defer vec_char_ptr_delete(&installedrepo);

  if (resolve_package_paths(&package_paths, &installedrepo, packages) !=
      ERR_OK) {
    LOG_ERROR(
        ERR_LEVEL_FATAL,
        "failed to resolve package paths. The packages may not be installed.");
    PANIC();
  }

  // build index
  fileindex_t index;
  fileindex_build(&index, pConf->sysroot, pConf->pkgs_path);
  defer fileindex_delete(&index);

  // create the fsops vec and the zips vec
  vec_fsop_t fsops;
  vec_fsop_t_init(&fsops);
  defer vec_fsop_t_delete_and_freeowned(&fsops);

  vec_mz_zip_archive_ptr zips;
  vec_mz_zip_archive_ptr_init(&zips);
  defer vec_mz_zip_archive_ptr_delete_and_freeowned(&zips);

  bool should_proceed = true;

  for (size_t i = 0; i < n_targets; i++) {
    char *package = *vec_char_ptr_at(packages, i);
    char *package_path = *vec_char_ptr_at(&package_paths, i);
    ErrVal err =
        fsops_emit_uninstall_package(&fsops, &zips, &index, package_path,
                                     pConf->sysroot, &pConf->protected_paths);
    if (err != ERR_OK) {
      should_proceed = false;
      continue;
    }

    // if good to proceed emit final fsop removing the zip
    // this happens last because if the uninstall is interrupted we want to be
    // able to resume it.
    fsops_emit_rm("uninstall", package, strdup(package_path), &fsops, &index);
  }
  if (!should_proceed) {
    return 1;
  }

  execute_fsops(&fsops, dry_run);
  return 0;
}

static int do_upgrade(ZpkConfiguration *pConf, vec_char_ptr *pTargets) {
  (void)pConf;
  (void)pTargets;
  return 0;
}

// reinstall packages whose files are missing or corrupt
static int do_fix(ZpkConfiguration *pConf, vec_char_ptr *pTargets) {
  (void)pConf;
  (void)pTargets;
  return 0;
}

static int do_list(ZpkConfiguration *pConf, bool installed, bool upgradable,
                   bool available) {
  (void)pConf;
  (void)installed;
  (void)upgradable;
  (void)available;
  return 0;
}

// which package owns each path
static int do_owner(ZpkConfiguration *pConf, char *path) {
  // build index
  fileindex_t index;
  fileindex_build(&index, pConf->sysroot, pConf->pkgs_path);
  defer fileindex_delete(&index);

  char* abspath = abspath_portable(path);
  defer free(abspath);

  IndexData *indexdata;
  if (!llrb_path_indexdata_get_ref(&index.index, &abspath, &indexdata)) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "no package owns %s", path);
    return 1;
  }
  llrb_char_ptr_fileclaim_iter iter;
  llrb_char_ptr_fileclaim_iter_begin(&indexdata->claims, &iter);
  char *package;
  while (llrb_char_ptr_fileclaim_iter_next(&iter, &package, NULL)) {
    puts(package);
  }
  return 0;
}

int main(int argc, char **argv) {
  ZpkConfiguration configuration;
  ZpkOperation operation;
  parse_args(argc, argv, &configuration, &operation);
  defer delete_ZpkOperation(&operation);
  defer delete_ZpkConfiguration(&configuration);

  switch (operation.op) {
  case ZPK_OP_ADD:
    return do_add(&configuration, &operation.add.targets, operation.dry_run);
  case ZPK_OP_FETCH:
    return do_fetch(&configuration, &operation.fetch.targets,
                    operation.fetch.output_dir);
  case ZPK_OP_DEL:
    return do_del(&configuration, &operation.del.targets, operation.dry_run);
  case ZPK_OP_UPGRADE:
    return do_upgrade(&configuration, &operation.upgrade.targets);
  case ZPK_OP_FIX:
    return do_fix(&configuration, &operation.fix.targets);
  case ZPK_OP_LIST:
    return do_list(&configuration, operation.list.installed,
                   operation.list.upgradable, operation.list.available);
  case ZPK_OP_OWNER:
    return do_owner(&configuration, operation.owner.path);
  }
}
