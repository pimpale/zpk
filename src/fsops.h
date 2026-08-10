#ifndef fsops_h_INCLUDED
#define fsops_h_INCLUDED

#include "error.h"
#include "instances/llrb_path_indexdata.h"
#include "instances/vec_char_ptr.h"
#include "instances/vec_fsop.h"
#include "instances/vec_mz_zip_archive.h"

// file index manipulation ops
void build_file_index(llrb_path_indexdata *index, char *pkgs_path);
void delete_file_index(llrb_path_indexdata *index);

// function appends to `package_paths`.
ErrVal resolve_package_paths(vec_char_ptr *package_paths,
                             const vec_char_ptr *repositories,
                             const vec_char_ptr *packages);

ErrVal install_package(
    // appends to this if the operation would succeed
    vec_fsop *fsops,
    // fsops refer to indexes in the zips. appends to this if the operation
    // would succeed
    vec_mz_zip_archive *zips,
    // file index (for file conflict identification)
    llrb_path_indexdata *index,
    // user given package name (For logging)
    char *package,
    // zip file to install
    char *package_path,
    // where to install
    char *sysroot);

ErrVal uninstall_package(
    // appends to this if the operation would succeed
    vec_fsop *fsops,
    // fsops refer to indexes in the zips. appends to this if the operation
    // would succeed
    vec_mz_zip_archive *zips,
    // file index (for file conflict identification)
    llrb_path_indexdata *index,
    // user given package name (For logging)
    char *package,
    // zip file to uninstall
    char *package_path,
    // where to uninstall
    char *sysroot);

void execute_fsops(
    // executes these
    vec_fsop *fsops,
    // fsops refer to indexes in the zips
    vec_mz_zip_archive *zips,
    // for logging
    const char *op, const char *pkg,
    // don't actually change fs
    const bool dry_run);
#endif // fsops_h_INCLUDED
