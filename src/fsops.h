#ifndef fsops_h_INCLUDED
#define fsops_h_INCLUDED

#include "error.h"
#include "index.h"
#include "instances/vec_char_ptr.h"
#include "instances/vec_fsop_t.h"
#include "instances/vec_mz_zip_archive_ptr.h"

// function appends to `package_paths`.
ErrVal resolve_package_paths(vec_char_ptr *package_paths,
                             const vec_char_ptr *repositories,
                             const vec_char_ptr *packages);

void fsops_emit_mkdir(
    // for logging only
    const char *op, const char *pkg,
    // takes ownership of path
    char *path,
    // appends to this
    vec_fsop_t *fsops,
    // simulates the behavior in fileindex
    fileindex_t *index);

void fsops_emit_install(
    // logging only
    const char *op, const char *pkg,
    // takes ownership
    char *path, FileClaim claim, mz_zip_archive *zip, vec_fsop_t *fsops,
    // simulates the behavior in fileindex
    fileindex_t *index);
ErrVal fsops_emit_mkdir_p( // logging only
    const char *op, const char *pkg,
    // takes ownership
    char *path,
    // file index op
    vec_fsop_t *fsops,
    // simulates the behavior in fileindex
    fileindex_t *index);
void fsops_emit_rm(
    // logging only
    const char *op, const char *pkg,
    // takes ownership
    char *path, vec_fsop_t *fsops,
    // simulates the behavior in fileindex
    fileindex_t *index);

void fsops_emit_rmdir(
    // logging only
    const char *op, const char *pkg,
    // takes ownership
    char *path, vec_fsop_t *fsops,
    // simulates the behavior in fileindex
    fileindex_t *index);

void fsops_emit_mv(const char *op, const char *pkg, char *from, char *to,
                   vec_fsop_t *fsops,
                   // simulates the behavior in fileindex
                   fileindex_t *index);

void fsops_emit_cp(const char *op, const char *pkg, char *from, char *to,
                   vec_fsop_t *fsops,
                   // simulates the behavior in fileindex
                   fileindex_t *index);

ErrVal fsops_emit_rm_rf(const char *op, const char *pkg,
                        // takes ownership
                        char *path,
                        // appends to this
                        vec_fsop_t *fsops,
                        // simulates the behavior in fileindex
                        fileindex_t *index);

ErrVal fsops_emit_install_package(
    // appends to this if the operation would succeed
    vec_fsop_t *fsops,
    // fsops refer to indexes in the zips. appends to this if the operation
    // would succeed
    vec_mz_zip_archive_ptr *zips,
    // file index (for file conflict identification)
    fileindex_t *index,
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
    fileindex_t *index,
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
