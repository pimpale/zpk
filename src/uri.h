#ifndef uri_h_INCLUDED
#define uri_h_INCLUDED

#include "instances/slice_uint8_t.h"

// simple uri, not a full implementation
// here scheme + :// + [ host + [:port]] + /path
typedef struct {
    slice_uint8_t scheme;
    slice_uint8_t host;
    bool has_port;
    uint16_t port;
    slice_uint8_t path;
}  uri_t;

bool decode_uri(slice_uint8_t input, uri_t* out);

#endif // uri_h_INCLUDED
