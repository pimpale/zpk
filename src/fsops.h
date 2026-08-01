#ifndef fsops_h_INCLUDED
#define fsops_h_INCLUDED

#include "error.h"
#include "configuration.h"

ErrVal install_package(ZipkgConfiguration *pConf, char* package);
ErrVal uninstall_package(ZipkgConfiguration *pConf, char* package);

#endif // fsops_h_INCLUDED
