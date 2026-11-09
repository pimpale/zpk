#ifndef uri_h_INCLUDED
#define uri_h_INCLUDED

#include "instances/slice_uint8_t.h"

// simple uri, not a full implementation
// here [scheme:] + [//host + [:port]] + [/path]
typedef struct {
    bool has_scheme;// always true when call parse_absolute_uri
    slice_uint8_t scheme;
    bool has_authority;
    slice_uint8_t host;
    bool has_port;
    slice_uint8_t port;
    slice_uint8_t path;
}  uri_parse_t;


bool parse_uri(slice_uint8_t input, uri_parse_t* parsed, bool absolute);
bool encode_uri(slice_uint8_t *output, uri_parse_t parsed);

bool resolve_uri_ref(uri_parse_t* parsed_reference, uri_parse_t* parsed_output, slice_uint8_t *out);

#endif // uri_h_INCLUDED
