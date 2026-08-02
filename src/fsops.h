#ifndef fsops_h_INCLUDED
#define fsops_h_INCLUDED

#include "error.h"
#include "configuration.h"

ErrVal install_package(ZpkConfiguration *pConf, char* package, bool dry_run);
ErrVal uninstall_package(ZpkConfiguration *pConf, char* package, bool dry_run);

#endif // fsops_h_INCLUDED
