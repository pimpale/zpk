#ifndef slice_uint8_t_h_INCLUDED
#define slice_uint8_t_h_INCLUDED

#include <stdint.h>

#define SLICE_DTYPE uint8_t
#include <slice/slice.h>
#undef SLICE_DTYPE

slice_uint8_t slice_uint8_t_from_str(char* s);

// allocates
char* slice_uint8_t_to_allocated_str(slice_uint8_t in);

#endif // slice_uint8_t_h_INCLUDED
