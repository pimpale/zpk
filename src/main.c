#include <stdio.h>
#include <stdlib.h>
#include <zip/zip.h>

typedef char *char_ptr;
#define VEC_DTYPE char_ptr
#include <vec/vec.h>

typedef enum {
  ZIPKG_OP_ADD,
  ZIPKG_OP_DEL,
  ZIPKG_OP_UPGRADE,
  ZIPKG_OP_FIX,
  ZIPKG_OP_LIST,
  ZIPK_OP_OWNER
} ZipkgOpKind;

typedef struct {
  ZipkgOpKind op;
  union {
    struct {
      vec_char_ptr targets;
    } add;
    struct {
      vec_char_ptr targets;
    } del;
    struct {
      vec_char_ptr targets;       // no targets = all
    } upgrade;
struct { bool installed, upgradable, available; } list;
  };
} ZipkgOperation;

typedef struct {
  // where the packages are going to be installed to (defaults to /)
  char *sysroot;
  // a list of repositories (in URI format)
  vec_char_ptr repositories;
  //

} ZipkgOptions;

ZipkgOperation parse_args(int argc, char **argv) {

}

ZipkgOptions parse_config() {

}


int main(int argc, char **argv) {


}