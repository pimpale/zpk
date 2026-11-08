#ifndef resolvedpackage_h_INCLUDED
#define resolvedpackage_h_INCLUDED

#include "instances/slice_uint8_t.h"
#include "uri.h"

typedef struct {
    slice_uint8_t entry;
    slice_uint8_t name;
    slice_uint8_t version;
} parsed_package_t;

typedef struct {
    slice_uint8_t source_uri_storage; // owned
    uri_parse_t source_uri;
    parsed_package_t package;

    // path to a copy on the local device
    // may be null until fetched
    // owned
    char* local_path;
} ResolvedPackage;

void delete_ResolvedPackage(ResolvedPackage* rp);

#endif // resolvedpackage_h_INCLUDED
