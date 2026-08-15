#ifndef llrb_char_ptr_packagedata_h_INCLUDED
#define llrb_char_ptr_packagedata_h_INCLUDED

typedef char *char_ptr;

typedef struct {
    // whether the package is installed
    bool installed;
    // if the install state is something that changed during the transaction 
    bool changed_during_transaction;
} PackageData;

#define LLRB_NAME char_ptr_packagedata
#define LLRB_KEY char_ptr
#define LLRB_VALUE PackageData
#include <llrb/llrb.h>
#undef LLRB_VALUE
#undef LLRB_KEY
#undef LLRB_NAME

void llrb_char_ptr_packagedata_clear_and_freeowned(llrb_char_ptr_packagedata *map);
void llrb_char_ptr_packagedata_delete_and_freeowned(llrb_char_ptr_packagedata *map);

#endif // llrb_char_ptr_packagedata_h_INCLUDED
