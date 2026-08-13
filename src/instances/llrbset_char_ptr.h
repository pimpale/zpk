#ifndef llrbset_char_ptr_h_INCLUDED
#define llrbset_char_ptr_h_INCLUDED

typedef char *char_ptr;

#define LLRBSET_NAME char_ptr
#define LLRBSET_KEY char_ptr
#include <llrbset/llrbset.h>
#undef LLRBSET_KEY
#undef LLRBSET_NAME


void llrbset_char_ptr_clear_and_freeowned(llrbset_char_ptr *vec);
void llrbset_char_ptr_delete_and_freeowned(llrbset_char_ptr *vec);

#endif // llrbset_char_ptr_h_INCLUDED
