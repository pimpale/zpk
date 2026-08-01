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
} ZipkgConfiguration;

void delete_ZipkgConfiguration(ZipkgConfiguration *config);

typedef enum {
  ZIPKG_OP_ADD,
  ZIPKG_OP_FETCH,
  ZIPKG_OP_DEL,
  ZIPKG_OP_UPGRADE,
  ZIPKG_OP_FIX,
  ZIPKG_OP_LIST,
  ZIPKG_OP_OWNER
} ZipkgOpKind;

typedef struct {
  ZipkgOpKind op;
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
} ZipkgOperation;

// Parses argv and resolves the effective configuration in one shot; any
// error (bad args, unparseable config file) is fatal and exits.
// PRECEDENCE:
// command-line options (-p/--root, -X/--repository, --config)
// environment variables
// check ./.zipkg.ini, ../.zipkg.ini, etc
// check /etc/zipkg.ini
void parse_args(int argc, char **argv, ZipkgConfiguration *config,
                ZipkgOperation *op);

void delete_ZipkgOperation(ZipkgOperation *op);

#endif // configuration_h_INCLUDED
