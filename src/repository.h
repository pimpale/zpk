#ifndef repository_h_INCLUDED
#define repository_h_INCLUDED

#include "error.h"
#include "instances/llrb_char_ptr_resolvedpackage.h"
#include "instances/vec_char_ptr.h"
#include "instances/vec_slice_uint8_t.h"

bool version_is_greater(slice_uint8_t a, slice_uint8_t b);

ErrVal resolve_package_paths_installed(
    llrb_char_ptr_resolvedpackage *resolved_packages,
    slice_uint8_t installed_directory,
    vec_char_ptr *packages,
    bool none_is_all
);

ErrVal resolve_and_fetch_package_paths_repositories(
    llrb_char_ptr_resolvedpackage *resolved_packages,
    vec_slice_uint8_t *repositories,
    vec_char_ptr *packages,
    slice_uint8_t cache_directory,
    bool none_is_all,
    bool download,
    vec_slice_uint8_t *cacert_paths,
    bool strict_ssl
);

#endif // repository_h_INCLUDED
