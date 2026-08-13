#ifndef index_h_INCLUDED
#define index_h_INCLUDED

#include "error.h"
#include "instances/llrb_path_indexdata.h"
#include "instances/llrbset_char_ptr.h"

#include <miniz/miniz.h>

typedef struct {
  llrbset_char_ptr packages;
  llrb_path_indexdata index;
} fileindex_t;

// file index manipulation ops
void fileindex_build(fileindex_t *index, char *pkgs_path);
void fileindex_delete(fileindex_t *index);

IndexData *fileindex_ensure_actual(fileindex_t *index, const char *sysroot,
                                   char *path, const char *op, const char *pkg);

// claim trees

// collect all explicit (listed in zip) and implicit (parent directory) claims.
void fileclaims_collect(mz_zip_archive *zip,
                        // logging only
                        const char *op, const char *package,
                        llrb_char_ptr_fileclaim *claims);
void fileclaims_delete(llrb_char_ptr_fileclaim *claims);

// claim-tree file-index interactions
ErrVal merge_claims_into_index(fileindex_t *index, const char *package_path,
                             llrb_char_ptr_fileclaim *claims,
                             bool simulate_installed);

void remove_claims_from_index(fileindex_t *index, char *package_path,
                              llrb_char_ptr_fileclaim *claims);

#endif // index_h_INCLUDED
