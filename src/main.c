#include <stddefer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "configuration.h"
#include "error.h"
#include "fsops.h"
#include "index.h"
#include "instances/llrb_char_ptr_fileclaim.h"
#include "instances/llrb_char_ptr_resolvedpackage.h"
#include "instances/llrb_path_indexdata.h"
#include "instances/llrbset_char_ptr.h"
#include "instances/vec_char_ptr.h"
#include "instances/vec_fsop.h"
#include "instances/vec_mz_zip_archive_ptr.h"
#include "oscompatlayer.h"
#include "pathutils.h"
#include "repository.h"
#include "resolvedpackage.h"

static int do_fetch(ZpkConfiguration *conf, vec_char_ptr *targets, char *path) {
  LOG_ERROR_ARGS(ERR_LEVEL_INFO, "fetching %zu targets to %s", vec_char_ptr_len(targets), path);
  (void)conf;
  (void)targets;
  (void)path;
  return 0;
}

static int do_add(ZpkConfiguration *conf, vec_char_ptr *packages) {
  LOG_ERROR_ARGS(
    ERR_LEVEL_INFO,
    "installing %zu targets to %s",
    vec_char_ptr_len(packages),
    conf->sysroot
  );
  // resolve and download packages to install
  llrb_char_ptr_resolvedpackage resolved_packages;
  llrb_char_ptr_resolvedpackage_new(&resolved_packages);
  defer llrb_char_ptr_resolvedpackage_delete_and_freeowned(&resolved_packages);

  if (
    resolve_and_fetch_package_paths_repositories(
      &resolved_packages,
      &conf->repositories,
      packages,
      conf->cached_pkgs_path,
      false,
      true
    )
    != ERR_OK
  ) {
    return 1;
  }

  // build index
  FileIndex index;
  fileindex_build(&index, conf->sysroot, conf->installed_pkgs_path);
  defer fileindex_delete(&index);

  // contains the fsops of the actual write operation
  vec_fsop fsops;
  vec_fsop_init(&fsops);
  defer vec_fsop_delete_and_freeowned(&fsops);

  vec_mz_zip_archive_ptr zips;
  vec_mz_zip_archive_ptr_init(&zips);
  defer vec_mz_zip_archive_ptr_delete_and_freeowned(&zips);

  fsops_emit_mkdir_p("prepare", "install", strdup(conf->sysroot), &fsops, &index);

  bool should_proceed = true;

  llrb_char_ptr_resolvedpackage_iter iter;
  llrb_char_ptr_resolvedpackage_iter_begin(&resolved_packages, &iter);
  ResolvedPackage rp;
  while (llrb_char_ptr_resolvedpackage_iter_next(&iter, NULL, &rp)) {

    // journal intent by moving the thing first. Then we can patch it up. if
    // there's a crash.
    char *dest = joinpath(conf->cached_pkgs_path, basename_m(rp.package_path));
    fsops_emit_mv("install", rp.package, strdup(rp.package_path), dest, &fsops, &index);

    ErrVal err = fsops_emit_install_package(
      "install",
      rp.package,
      &fsops,
      &zips,
      &index,
      rp.package_path,
      conf->sysroot,
      &conf->protected_paths,
      true
    );
    if (err != ERR_OK) {
      should_proceed = false;
      continue;
    }
  }
  if (!should_proceed) {
    return 1;
  }

  execute_fsops(&fsops, conf->download_only);
  return 0;
}

static int do_del(ZpkConfiguration *conf, vec_char_ptr *packages) {
  LOG_ERROR_ARGS(
    ERR_LEVEL_INFO,
    "removing %zu targets from %s",
    vec_char_ptr_len(packages),
    conf->sysroot
  );

  // resolve packages to install
  llrb_char_ptr_resolvedpackage resolved_packages;
  llrb_char_ptr_resolvedpackage_new(&resolved_packages);
  defer llrb_char_ptr_resolvedpackage_delete_and_freeowned(&resolved_packages);

  if (
    resolve_package_paths_installed(&resolved_packages, conf->installed_pkgs_path, packages, false)
    != ERR_OK
  ) {
    return 1;
  }

  // build index
  FileIndex index;
  fileindex_build(&index, conf->sysroot, conf->installed_pkgs_path);
  defer fileindex_delete(&index);

  // contains the fsops of the actual write operation
  vec_fsop fsops;
  vec_fsop_init(&fsops);
  defer vec_fsop_delete_and_freeowned(&fsops);

  vec_mz_zip_archive_ptr zips;
  vec_mz_zip_archive_ptr_init(&zips);
  defer vec_mz_zip_archive_ptr_delete_and_freeowned(&zips);

  bool should_proceed = true;

  llrb_char_ptr_resolvedpackage_iter iter;
  llrb_char_ptr_resolvedpackage_iter_begin(&resolved_packages, &iter);
  ResolvedPackage rp;
  while (llrb_char_ptr_resolvedpackage_iter_next(&iter, NULL, &rp)) {
    ErrVal err = fsops_emit_uninstall_package(
      "uninstall",
      rp.package,
      &fsops,
      &index,
      rp.package_path,
      conf->sysroot,
      &conf->protected_paths
    );
    if (err != ERR_OK) {
      should_proceed = false;
      continue;
    }

    // if good to proceed remove the file from installed
    char *dest = joinpath(conf->cached_pkgs_path, basename_m(rp.package_path));
    fsops_emit_mv("uninstall", rp.package, strdup(rp.package_path), dest, &fsops, &index);
  }
  if (!should_proceed) {
    return 1;
  }

  execute_fsops(&fsops, conf->download_only);
  return 0;
}

static int do_upgrade(ZpkConfiguration *conf, vec_char_ptr *targets) {
  (void)conf;
  (void)targets;
  return 0;
}

// reinstall packages whose files are missing or corrupt
static int do_fix(ZpkConfiguration *conf, vec_char_ptr *packages) {
  // resolve packages to install
  llrb_char_ptr_resolvedpackage resolved_packages;
  llrb_char_ptr_resolvedpackage_new(&resolved_packages);
  defer llrb_char_ptr_resolvedpackage_delete_and_freeowned(&resolved_packages);

  if (
    resolve_package_paths_installed(&resolved_packages, conf->installed_pkgs_path, packages, true)
    != ERR_OK
  ) {
    return 1;
  }

  // build index
  FileIndex index;
  fileindex_build(&index, conf->sysroot, conf->installed_pkgs_path);
  defer fileindex_delete(&index);

  // create the fsops vec and the zips vec
  vec_fsop fsops;
  vec_fsop_init(&fsops);
  defer vec_fsop_delete_and_freeowned(&fsops);

  vec_mz_zip_archive_ptr zips;
  vec_mz_zip_archive_ptr_init(&zips);
  defer vec_mz_zip_archive_ptr_delete_and_freeowned(&zips);

  fsops_emit_mkdir_p("prepare", "fix", strdup(conf->sysroot), &fsops, &index);

  bool should_proceed = true;

  llrb_char_ptr_resolvedpackage_iter iter;
  llrb_char_ptr_resolvedpackage_iter_begin(&resolved_packages, &iter);
  ResolvedPackage rp;
  while (llrb_char_ptr_resolvedpackage_iter_next(&iter, NULL, &rp)) {
    ErrVal err = fsops_emit_install_package(
      "fix",
      rp.package,
      &fsops,
      &zips,
      &index,
      rp.package_path,
      conf->sysroot,
      &conf->protected_paths,
      false
    );
    if (err != ERR_OK) {
      should_proceed = false;
      continue;
    }
  }
  if (!should_proceed) {
    return 1;
  }

  execute_fsops(&fsops, conf->download_only);
  return 0;
}

static int do_list(
  ZpkConfiguration *conf,
  bool only_installed,
  bool only_upgradable,
  bool only_available,
  bool only_orphaned
) {
  llrb_char_ptr_resolvedpackage installed_packages;
  llrb_char_ptr_resolvedpackage_new(&installed_packages);
  defer llrb_char_ptr_resolvedpackage_delete_and_freeowned(&installed_packages);

  if (
    resolve_package_paths_installed(&installed_packages, conf->installed_pkgs_path, NULL, true)
    != ERR_OK
  ) {
    return 1;
  }

  llrb_char_ptr_resolvedpackage available_packages;
  llrb_char_ptr_resolvedpackage_new(&available_packages);
  defer llrb_char_ptr_resolvedpackage_delete_and_freeowned(&available_packages);

  // if we are just doing --installed and nothing else, then we can omit
  // fetching important bc what if we're offline
  if (only_upgradable || only_available || only_orphaned || !only_installed) {
    if (
      resolve_and_fetch_package_paths_repositories(
        &available_packages,
        &conf->repositories,
        NULL,
        conf->cached_pkgs_path,
        true,
        false
      )
      != ERR_OK
    ) {
      return 1;
    }
  }

  // create joint set of all package names
  llrbset_char_ptr all_packages;
  llrbset_char_ptr_new(&all_packages);
  defer llrbset_char_ptr_delete(&all_packages); // only borrowing

  {
    llrb_char_ptr_resolvedpackage_iter iter;
    char *package;

    // add all installed
    llrb_char_ptr_resolvedpackage_iter_begin(&installed_packages, &iter);
    while (llrb_char_ptr_resolvedpackage_iter_next(&iter, &package, NULL)) {
      llrbset_char_ptr_insert(&all_packages, &package);
    }
    // add all available
    llrb_char_ptr_resolvedpackage_iter_begin(&available_packages, &iter);
    while (llrb_char_ptr_resolvedpackage_iter_next(&iter, &package, NULL)) {
      llrbset_char_ptr_insert(&all_packages, &package);
    }
  }

  llrbset_char_ptr_iter iter;
  char *package;

  llrbset_char_ptr_iter_begin(&all_packages, &iter);
  while (llrbset_char_ptr_iter_next(&iter, &package)) {
    ResolvedPackage *irp = NULL;
    ResolvedPackage *arp = NULL;
    llrb_char_ptr_resolvedpackage_get_ref(&installed_packages, &package, &irp);
    llrb_char_ptr_resolvedpackage_get_ref(&available_packages, &package, &arp);
    if (only_installed && irp == NULL) {
      continue;
    }
    if (only_available && arp == NULL) {
      continue;
    }
    if (only_orphaned && !(irp != NULL && arp == NULL)) {
      continue;
    }
    if (only_upgradable) {
      if (irp == NULL || arp == NULL) {
        continue;
      }
      if (!version_is_greater(arp->version, irp->version)) {
        continue;
      }
    }
    puts(package);
  }

  return 0;
}

// which package owns each path
static int do_owner(ZpkConfiguration *conf, char *path) {
  // build index
  FileIndex index;
  fileindex_build(&index, conf->sysroot, conf->installed_pkgs_path);
  defer fileindex_delete(&index);

  char *abspath = abspath_portable(path);
  defer free(abspath);
  LOG_ERROR_ARGS(ERR_LEVEL_INFO, "owner %s: resolved to %s", path, abspath);

  IndexData *indexdata;
  if (!llrb_path_indexdata_get_ref(&index.index, &abspath, &indexdata)) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "owner %s: no owning packages found", abspath);
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
  defer delete_zpkoperation(&operation);
  defer delete_zpkconfiguration(&configuration);

  switch (operation.op) {
    case ZPK_OP_ADD:
      return do_add(&configuration, &operation.add.targets);
    case ZPK_OP_FETCH:
      return do_fetch(&configuration, &operation.fetch.targets, operation.fetch.output_dir);
    case ZPK_OP_DEL:
      return do_del(&configuration, &operation.del.targets);
    case ZPK_OP_UPGRADE:
      return do_upgrade(&configuration, &operation.upgrade.targets);
    case ZPK_OP_FIX:
      return do_fix(&configuration, &operation.fix.targets);
    case ZPK_OP_LIST:
      return do_list(
        &configuration,
        operation.list.installed,
        operation.list.upgradable,
        operation.list.available,
        operation.list.orphaned
      );
    case ZPK_OP_OWNER:
      return do_owner(&configuration, operation.owner.path);
  }
}
