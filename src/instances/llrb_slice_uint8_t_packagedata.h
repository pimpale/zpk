#ifndef llrb_slice_uint8_t_packagedata_h_INCLUDED
#define llrb_slice_uint8_t_packagedata_h_INCLUDED

#include "slice_uint8_t.h"

typedef struct {
    // whether the package is installed
    bool installed;
    // if the install state is something that changed during the transaction 
    bool changed_during_transaction;
} PackageData;

#define LLRB_NAME slice_uint8_t_packagedata
#define LLRB_KEY slice_uint8_t
#define LLRB_VALUE PackageData
#include <llrb/llrb.h>
#undef LLRB_VALUE
#undef LLRB_KEY
#undef LLRB_NAME

void llrb_slice_uint8_t_packagedata_clear_and_freeowned(llrb_slice_uint8_t_packagedata *map);
void llrb_slice_uint8_t_packagedata_delete_and_freeowned(llrb_slice_uint8_t_packagedata *map);

#endif // llrb_slice_uint8_t_packagedata_h_INCLUDED
