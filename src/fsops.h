#ifndef fsops_h_INCLUDED
#define fsops_h_INCLUDED

#include "error.h"
#include "index.h"
#include "instances/vec_fsop.h"
#include "instances/vec_mz_zip_archive_ptr.h"

void fsops_emit_mkdir(
  // for logging only
  const char *op,
  const char *pkg,
  // takes ownership of path
  char *path,
  // appends to this
  vec_fsop *fsops,
  // simulates the behavior in fileindex
  FileIndex *index
);

void fsops_emit_install(
  // logging only
  const char *op,
  const char *pkg,
  // takes ownership
  char *path,
  FileClaim claim,
  mz_zip_archive *zip,
  vec_fsop *fsops,
  // simulates the behavior in fileindex
  FileIndex *index
);
ErrVal fsops_emit_mkdir_p( // logging only
    const char *op, const char *pkg,
    // takes ownership
    char *path,
    // file index op
    vec_fsop *fsops,
    // simulates the behavior in fileindex
    FileIndex *index);
void fsops_emit_rm(
  // logging only
  const char *op,
  const char *pkg,
  // takes ownership
  char *path,
  vec_fsop *fsops,
  // simulates the behavior in fileindex
  FileIndex *index
);

void fsops_emit_rmdir(
  // logging only
  const char *op,
  const char *pkg,
  // takes ownership
  char *path,
  vec_fsop *fsops,
  // simulates the behavior in fileindex
  FileIndex *index
);

void fsops_emit_mv(
  const char *op,
  const char *pkg,
  char *from,
  char *to,
  vec_fsop *fsops,
  // simulates the behavior in fileindex
  FileIndex *index
);

void fsops_emit_cp(
  const char *op,
  const char *pkg,
  char *from,
  char *to,
  vec_fsop *fsops,
  // simulates the behavior in fileindex
  FileIndex *index
);

ErrVal fsops_emit_rm_rf(
  const char *op,
  const char *pkg,
  // takes ownership
  char *path,
  // appends to this
  vec_fsop *fsops,
  // simulates the behavior in fileindex
  FileIndex *index
);

ErrVal fsops_emit_install_package(
  // install can be called as either a "fix", or "install" operation
  const char *op,
  // the package
  const char *pkg,
  // appends to this if the operation would succeed
  vec_fsop *fsops,
  // fsops refer to indexes in the zips. appends to this if the operation
  // would succeed
  vec_mz_zip_archive_ptr *zips,
  // file index (for file conflict identification)
  FileIndex *index,
  // zip file to install
  char *package_path,
  // where to install
  char *sysroot,
  // protected paths
  vec_char_ptr *protected_paths,
  // refuse to proceed if a duplicate exists
  bool flag_duplicate
);

ErrVal fsops_emit_uninstall_package(
  const char *op,
  // the package
  const char *pkg,
  // appends to this if the operation would succeed
  vec_fsop *fsops,
  // file index (for file conflict identification)
  FileIndex *index,
  // zip file to uninstall
  char *package_path,
  // where to uninstall
  char *sysroot,
  // protected paths
  vec_char_ptr *protected_paths
);

void execute_fsops(
  // executes these
  vec_fsop *fsops,
  // don't actually change fs
  const bool dry_run
);

#endif // fsops_h_INCLUDED
