#ifndef repository_h_INCLUDED
#define repository_h_INCLUDED

#include "error.h"
#include "instances/llrb_char_ptr_resolvedpackage.h"
#include "instances/vec_char_ptr.h"

bool package_data(
    // if not NULL, allocates a string containing just the package name
    char **package_name,
    // if not NULL, allocates a string containing just the package name
    char **package_version,
    // package basename
    char *entry,
    // .zip, or .uninstalling.zip or something.
    // Will be ignored from the package name
    const char *suffix);

ErrVal resolve_package_paths_installed(llrb_char_ptr_resolvedpackage *resolved_packages,
                                       char *directory, vec_char_ptr *packages,
                                       bool none_is_all);


ErrVal resolve_package_paths_repositories(llrb_char_ptr_resolvedpackage *resolved_packages,
                                          vec_char_ptr *repositories,
                                          vec_char_ptr *packages,
                                          bool none_is_all);

ErrVal resolve_and_fetch_package_paths_repositories(llrb_char_ptr_resolvedpackage *resolved_packages,
                                          vec_char_ptr *repositories,
                                          vec_char_ptr *packages,
                                          const char* directory,
                                          const char* presuf,
                                          bool none_is_all);

#endif // repository_h_INCLUDED
