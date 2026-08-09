#ifndef fsops_h_INCLUDED
#define fsops_h_INCLUDED

#include "error.h"
#include "instances/llrb_path_indexdata.h"
#include "instances/vec_char_ptr.h"

// file index manipulation ops
void build_file_index(llrb_path_indexdata *index, char *pkgs_path);
void delete_file_index(llrb_path_indexdata *index);

// function appends to `package_paths`.
ErrVal resolve_package_paths(vec_char_ptr *package_paths,
                             const vec_char_ptr *repositories,
                             const vec_char_ptr *packages);

ErrVal install_package(
    // file index (for file conflict identification)
    llrb_path_indexdata *index,
    // user given package name (For logging)
    char *package,
    // zip file to install
    char *package_path,
    // where to install
    char *sysroot,
    // don't actually change FS
    bool dry_run);

ErrVal uninstall_package(
    // file index (for file conflict identification)
    llrb_path_indexdata *index,
    // user given package name (For logging)
    char *package,
    // zip file to install
    char *package_path,
    // where to install
    char *sysroot,
    // don't actually change FS
    bool dry_run);

#endif // fsops_h_INCLUDED
