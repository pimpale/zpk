#ifndef llrb_path_bestrepo_h_INCLUDED
#define llrb_path_bestrepo_h_INCLUDED

typedef char* char_ptr;

typedef struct {
    // owned
    char* entry;
    char* version;
    // borrowed
    char* repository;
} BestRepo;

#define LLRB_NAME char_ptr_bestrepo
#define LLRB_KEY char_ptr
#define LLRB_VALUE BestRepo
#include <llrb/llrb.h>
#undef LLRB_VALUE
#undef LLRB_KEY
#undef LLRB_NAME


void llrb_char_ptr_bestrepo_delete_and_freeowned(llrb_char_ptr_bestrepo *map);

#endif // llrb_path_bestrepo_h_INCLUDED
