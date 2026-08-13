#ifndef fsops_h_INCLUDED
#define fsops_h_INCLUDED

#include "error.h"
#include "instances/llrb_path_indexdata.h"
#include "instances/vec_char_ptr.h"
#include "instances/vec_fsop_t.h"
#include "instances/vec_mz_zip_archive_ptr.h"

// file index manipulation ops
void build_file_index(llrb_path_indexdata *index, char *pkgs_path);
void delete_file_index(llrb_path_indexdata *index);

// function appends to `package_paths`.
ErrVal resolve_package_paths(vec_char_ptr *package_paths,
                             const vec_char_ptr *repositories,
                             const vec_char_ptr *packages);

void fsops_emit_install(const char *op, const char *pkg, const char *sysroot,
                        const char *path, const char *suffix, FileClaim claim,
                        mz_zip_archive *zip,
                        // appends to this
                        vec_fsop_t *fsops);

void fsops_emit_rm(const char *op, const char *pkg, const char *sysroot,
                   const char *path, const char *suffix,
                   // appends to this
                   vec_fsop_t *fsops);

void fsops_emit_rmdir(const char *op, const char *pkg, const char *sysroot,
                      const char *path, const char *suffix,
                      // appends to this
                      vec_fsop_t *fsops);

void fsops_emit_mv(const char *op, const char *pkg,
                   const char *fromsysroot, const char *frompath,
                   const char *fromsuffix, const char *tosysroot, const char *topath,
                   const char *tosuffix, vec_fsop_t *fsops);

void fsops_emit_cp(const char *op, const char *pkg,
                   const char *fromsysroot, const char *frompath,
                   const char *fromsuffix, const char *tosysroot, const char *topath,
                   const char *tosuffix, vec_fsop_t *fsops);

ErrVal fsops_emit_install_package(
    // appends to this if the operation would succeed
    vec_fsop_t *fsops,
    // fsops refer to indexes in the zips. appends to this if the operation
    // would succeed
    vec_mz_zip_archive_ptr *zips,
    // file index (for file conflict identification)
    llrb_path_indexdata *index,
    // user given package name (For logging)
    char *package,
    // zip file to install
    char *package_path,
    // where to install
    char *sysroot,
    // protected paths
    vec_char_ptr *protected_paths);

ErrVal fsops_emit_uninstall_package(
    // appends to this if the operation would succeed
    vec_fsop_t *fsops,
    // fsops refer to indexes in the zips. appends to this if the operation
    // would succeed
    vec_mz_zip_archive_ptr *zips,
    // file index (for file conflict identification)
    llrb_path_indexdata *index,
    // user given package name (For logging)
    char *package,
    // zip file to uninstall
    char *package_path,
    // where to uninstall
    char *sysroot,
    // protected paths
    vec_char_ptr *protected_paths);

void execute_fsops(
    // executes these
    vec_fsop_t *fsops,
    // don't actually change fs
    const bool dry_run);

#endif // fsops_h_INCLUDED
