#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "error.h"

const char *levelstrerror(ErrSeverity level) {
  switch (level) {
  case ERR_LEVEL_DEBUG: {
    return ("debug");
  }
  case ERR_LEVEL_INFO: {
    return ("info");
  }
  case ERR_LEVEL_WARN: {
    return ("warn");
  }
  case ERR_LEVEL_ERROR: {
    return ("error");
  }
  case ERR_LEVEL_FATAL: {
    return ("fatal");
  }
  case ERR_LEVEL_UNKNOWN: {
    return ("unknown");
  }
  }
}
