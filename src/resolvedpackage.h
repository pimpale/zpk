#ifndef resolvedpackage_h_INCLUDED
#define resolvedpackage_h_INCLUDED


typedef struct {
    char* package;
    char* version;
    char* repository;

    // may be null until fetched
    char* package_path;
} ResolvedPackage;

void delete_ResolvedPackage(ResolvedPackage* rp);

char* entryname(ResolvedPackage *rp);

#endif // resolvedpackage_h_INCLUDED
