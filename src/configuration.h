#ifndef configuration_h_INCLUDED
#define configuration_h_INCLUDED

#include <stdbool.h>

typedef char *char_ptr;
#define VEC_DTYPE char_ptr
#include <vec/vec.h>

typedef struct {
  // where the packages are going to be installed to (defaults to /)
  char *sysroot;
  // the installed package dir
  // defaults to $sysroot/pkg
  char *pkgs_path;
  // a list of repositories (in URI format)
  vec_char_ptr* repositories;
} ZpkConfiguration;

void delete_ZpkConfiguration(ZpkConfiguration *config);

typedef enum {
  ZPK_OP_ADD,
  ZPK_OP_FETCH,
  ZPK_OP_DEL,
  ZPK_OP_UPGRADE,
  ZPK_OP_FIX,
  ZPK_OP_LIST,
  ZPK_OP_OWNER
} ZpkOpKind;

typedef struct {
  ZpkOpKind op;
  union {
    struct {
      vec_char_ptr* targets;
    } add;
    struct {
      vec_char_ptr* targets;
      char *output_dir;
    } fetch;
    struct {
      vec_char_ptr* targets;
    } del;
    struct {
      vec_char_ptr* targets; // no targets = all
    } upgrade;
    struct {
      vec_char_ptr* targets; // no targets = all
    } fix;
    struct {
      bool installed;
      bool upgradable;
      bool available;
    } list;
    struct {
      char *path;
    } owner;
  };
} ZpkOperation;

// Parses argv and resolves the effective configuration in one shot; any
// error (bad args, unparseable config file) is fatal and exits.
// PRECEDENCE:
// command-line options (-p/--root, -X/--repository, --config)
// environment variables
// check ./.zpk.ini, ../.zpk.ini, etc
// check /etc/zpk.ini
void parse_args(int argc, char **argv, ZpkConfiguration *config,
                ZpkOperation *op);

void delete_ZpkOperation(ZpkOperation *op);

#endif // configuration_h_INCLUDED
