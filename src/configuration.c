#include "configuration.h"

#include "constants.h"
#include "error.h"

#include <stdlib.h>
#include <string.h>

#include <toml/toml.h>

static const char *GLOBAL_CONFIG_PATH = "/etc/zipkg.ini";
static const char *CONFIGURATION_FILE_NAME = ".zipkg.ini";

// walks from the cwd up to /, returning the nearest .zipkg.ini (caller frees),
// or NULL if none found
static char *find_local_config(void) {
  char dir[PATH_MAX];
  if (getcwd(dir, sizeof dir) == NULL) {
    return NULL;
  }

  for (;;) {
    char* candidate = malloc() 
    snprintf(candidate, sizeof candidate, "%s/%s",
             strcmp(dir, "/") == 0 ? "" : dir, CONFIGURATION_FILE_NAME);
    if (strcmp(dir, "/") == 0) {
      return NULL;
    }
    char *slash = strrchr(dir, '/');
    if (slash == dir) {
      dir[1] = '\0';
    } else {
      *slash = '\0';
    }
  }
}

static void apply_config_file(ZipkgConfiguration *config, const char *path) {
  TomlTable *table = toml_load_filename(path);
  if (table == NULL) {
    LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "could not parse %s: %s", path,
                   toml_err()->message);
    PANIC();
  }

  TomlValue *val = toml_table_get(table, "sysroot");
  if (val != NULL && val->type == TOML_STRING) {
    free(config->sysroot);
    config->sysroot = strdup(val->value.string->str);
  }

  val = toml_table_get(table, "pkgs_path");
  if (val != NULL && val->type == TOML_STRING) {
    free(config->pkgs_path);
    config->pkgs_path = strdup(val->value.string->str);
  }

  val = toml_table_get(table, "repositories");
  if (val != NULL && val->type == TOML_ARRAY) {
    for (size_t i = 0; i < val->value.array->len; i++) {
      TomlValue *elem = val->value.array->elements[i];
      if (elem->type != TOML_STRING) {
        LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: repositories must be strings",
                       path);
        PANIC();
      }
      char_ptr repo = strdup(elem->value.string->str);
      vec_char_ptr_push(config->repositories, &repo);
    }
  }

  toml_table_free(table);
}

static void clear_repositories(vec_char_ptr *vec) {
  for (uint32_t i = 0; i < vec_char_ptr_len(vec); i++) {
    free(*vec_char_ptr_at(vec, i));
  }
  vec_char_ptr_clear(vec);
}

static void resolve_configuration(ZipkgConfiguration *config,
                                  const char *cli_config,
                                  const char *cli_sysroot,
                                  vec_char_ptr *cli_repositories) {
  config->sysroot = NULL;
  config->pkgs_path = NULL;
  vec_char_ptr_new(&config->repositories);

  if (cli_config != NULL) {
    apply_config_file(config, cli_config);
  } else {
    char *local_path = find_local_config();
    const char *path = local_path;
    if (path == NULL) {
      path = GLOBAL_CONFIG_PATH;
    }
    if (path != NULL) {
      apply_config_file(config, path);
    }
    free(local_path);
  }

  const char *env = getenv("ZIPKG_SYSROOT");
  if (env != NULL) {
    free(config->sysroot);
    config->sysroot = strdup(env);
  }

  env = getenv("ZIPKG_PKGS_PATH");
  if (env != NULL) {
    free(config->pkgs_path);
    config->pkgs_path = strdup(env);
  }

  // comma-separated list; replaces any file-configured repositories
  env = getenv("ZIPKG_REPOSITORIES");
  if (env != NULL) {
    clear_repositories(config->repositories);
    char *dup = strdup(env);
    for (char *tok = strtok(dup, ","); tok != NULL; tok = strtok(NULL, ",")) {
      if (*tok == '\0') {
        continue;
      }
      char_ptr repo = strdup(tok);
      vec_char_ptr_push(config->repositories, &repo);
    }
    free(dup);
  }

  // command-line overrides win over everything; -X appends (like apk) rather
  // than replacing
  if (cli_sysroot != NULL) {
    free(config->sysroot);
    config->sysroot = strdup(cli_sysroot);
  }
  for (uint32_t i = 0; i < vec_char_ptr_len(cli_repositories); i++) {
    vec_char_ptr_push(config->repositories,
                      vec_char_ptr_at(cli_repositories, i));
  }

  if (config->sysroot == NULL) {
    config->sysroot = strdup("/");
  }

  if (config->pkgs_path == NULL) {
    size_t sysroot_len = strlen(config->sysroot);
    bool trailing_slash =
        sysroot_len > 0 && config->sysroot[sysroot_len - 1] == '/';
    config->pkgs_path = (char *)malloc(sysroot_len + sizeof "/pkg");
    snprintf(config->pkgs_path, sysroot_len + sizeof "/pkg",
             trailing_slash ? "%spkg" : "%s/pkg", config->sysroot);
  }
}

void delete_ZipkgConfiguration(ZipkgConfiguration *config) {
  free(config->sysroot);
  free(config->pkgs_path);
  clear_repositories(config->repositories);
  vec_char_ptr_delete(&config->repositories);
}

static const char *USAGE =
    "usage: zipkg [options] <command> [args]\n"
    "\n"
    "commands:\n"
    "  add <pkg>...             install packages\n"
    "  del <pkg>...             uninstall packages\n"
    "  fetch [-o DIR] <pkg>...  download packages without installing\n"
    "  upgrade [pkg...]         upgrade packages (all if none given)\n"
    "  fix [pkg...]             reinstall broken packages (all if none given)\n"
    "  list [-I] [-u] [-a]      list installed/upgradable/available packages\n"
    "  info -W <path>           show which package owns a path\n"
    "\n"
    "global options:\n"
    "  -p, --root DIR           install to alternate root\n"
    "  -X, --repository URI     add a repository (repeatable)\n"
    "      --config FILE        use FILE instead of searching for .zipkg.ini\n"
    "  -h, --help               show this help\n";

// consumes the next argv element as the value of option argv[*i]
static char *opt_value(int argc, char **argv, int *i) {
  if (*i + 1 >= argc) {
    LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "option '%s' requires a value", argv[*i]);
    PANIC();
  }
  *i += 1;
  return argv[*i];
}

static ZipkgOpKind lookup_command(const char *name) {
  if (strcmp(name, "add") == 0)
    return ZIPKG_OP_ADD;
  if (strcmp(name, "fetch") == 0)
    return ZIPKG_OP_FETCH;
  if (strcmp(name, "del") == 0)
    return ZIPKG_OP_DEL;
  if (strcmp(name, "upgrade") == 0)
    return ZIPKG_OP_UPGRADE;
  if (strcmp(name, "fix") == 0)
    return ZIPKG_OP_FIX;
  if (strcmp(name, "list") == 0)
    return ZIPKG_OP_LIST;
  if (strcmp(name, "info") == 0)
    return ZIPKG_OP_OWNER;
  LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "unknown command '%s' (see 'zipkg --help')",
                 name);
  PANIC();
}

void parse_args(int argc, char **argv, ZipkgConfiguration *config,
                ZipkgOperation *op) {
  const char *cli_sysroot = NULL;
  const char *cli_config = NULL;
  const char *fetch_output = NULL;
  bool list_installed = false;
  bool list_upgradable = false;
  bool list_available = false;
  bool info_who_owns = false;
  bool have_op = false;
  bool no_more_options = false;
  ZipkgOpKind kind = ZIPKG_OP_ADD; // overwritten when the command is seen

  vec_char_ptr *cli_repositories;
  vec_char_ptr_new(&cli_repositories);
  vec_char_ptr *targets;
  vec_char_ptr_new(&targets);

  for (int i = 1; i < argc; i++) {
    char *arg = argv[i];

    if (no_more_options || arg[0] != '-' || arg[1] == '\0') {
      if (!have_op) {
        kind = lookup_command(arg);
        have_op = true;
      } else {
        char_ptr target = strdup(arg);
        vec_char_ptr_push(targets, &target);
      }
      continue;
    }

    if (strcmp(arg, "--") == 0) {
      no_more_options = true;
    } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
      fputs(USAGE, stdout);
      exit(EXIT_SUCCESS);
    } else if (strcmp(arg, "-p") == 0 || strcmp(arg, "--root") == 0) {
      cli_sysroot = opt_value(argc, argv, &i);
    } else if (strcmp(arg, "-X") == 0 || strcmp(arg, "--repository") == 0) {
      char_ptr repo = strdup(opt_value(argc, argv, &i));
      vec_char_ptr_push(cli_repositories, &repo);
    } else if (strcmp(arg, "--config") == 0) {
      cli_config = opt_value(argc, argv, &i);
    } else if (have_op && kind == ZIPKG_OP_FETCH &&
               (strcmp(arg, "-o") == 0 || strcmp(arg, "--output") == 0)) {
      fetch_output = opt_value(argc, argv, &i);
    } else if (have_op && kind == ZIPKG_OP_LIST &&
               (strcmp(arg, "-I") == 0 || strcmp(arg, "--installed") == 0)) {
      list_installed = true;
    } else if (have_op && kind == ZIPKG_OP_LIST &&
               (strcmp(arg, "-u") == 0 || strcmp(arg, "--upgradable") == 0)) {
      list_upgradable = true;
    } else if (have_op && kind == ZIPKG_OP_LIST &&
               (strcmp(arg, "-a") == 0 || strcmp(arg, "--available") == 0)) {
      list_available = true;
    } else if (have_op && kind == ZIPKG_OP_OWNER &&
               (strcmp(arg, "-W") == 0 || strcmp(arg, "--who-owns") == 0)) {
      info_who_owns = true;
    } else {
      LOG_ERROR_ARGS(ERR_LEVEL_FATAL,
                     "unrecognized option '%s' (see 'zipkg --help')", arg);
      PANIC();
    }
  }

  if (!have_op) {
    LOG_ERROR(ERR_LEVEL_FATAL, "no operation specified (see 'zipkg --help')");
    PANIC();
  }

  uint32_t ntargets = vec_char_ptr_len(targets);
  switch (kind) {
  case ZIPKG_OP_ADD:
  case ZIPKG_OP_DEL:
  case ZIPKG_OP_FETCH:
    if (ntargets == 0) {
      LOG_ERROR(ERR_LEVEL_FATAL, "at least one package required");
      PANIC();
    }
    break;
  case ZIPKG_OP_OWNER:
    if (!info_who_owns) {
      LOG_ERROR(ERR_LEVEL_FATAL, "'info' supports only -W/--who-owns");
      PANIC();
    }
    if (ntargets != 1) {
      LOG_ERROR(ERR_LEVEL_FATAL, "'info -W' requires exactly one path");
      PANIC();
    }
    break;
  case ZIPKG_OP_LIST:
    if (ntargets != 0) {
      LOG_ERROR(ERR_LEVEL_FATAL, "'list' takes no arguments");
      PANIC();
    }
    break;
  default:
    break;
  }

  // args are fully validated; only now touch the filesystem. ownership of the
  // -X strings moves into config->repositories
  resolve_configuration(config, cli_config, cli_sysroot, cli_repositories);
  vec_char_ptr_delete(&cli_repositories);

  op->op = kind;
  switch (kind) {
  case ZIPKG_OP_ADD:
    op->add.targets = targets;
    break;
  case ZIPKG_OP_DEL:
    op->del.targets = targets;
    break;
  case ZIPKG_OP_UPGRADE:
    op->upgrade.targets = targets;
    break;
  case ZIPKG_OP_FIX:
    op->fix.targets = targets;
    break;
  case ZIPKG_OP_FETCH:
    op->fetch.targets = targets;
    op->fetch.output_dir = strdup(fetch_output != NULL ? fetch_output : ".");
    break;
  case ZIPKG_OP_OWNER:
    op->owner.path = *vec_char_ptr_at(targets, 0);
    vec_char_ptr_delete(&targets);
    break;
  case ZIPKG_OP_LIST:
    // bare `list` means everything, like apk
    if (!list_installed && !list_upgradable && !list_available) {
      list_available = true;
    }
    op->list.installed = list_installed;
    op->list.upgradable = list_upgradable;
    op->list.available = list_available;
    vec_char_ptr_delete(&targets);
    break;
  }
}

void delete_ZipkgOperation(ZipkgOperation *op) {
  switch (op->op) {
  case ZIPKG_OP_ADD:
    for (size_t i = 0; i < vec_char_ptr_len(op->add.targets); i++) {
      free(*vec_char_ptr_at(op->add.targets, i));
    }
    vec_char_ptr_delete(&op->add.targets);
    break;
  case ZIPKG_OP_DEL:
    for (size_t i = 0; i < vec_char_ptr_len(op->del.targets); i++) {
      free(*vec_char_ptr_at(op->del.targets, i));
    }
    vec_char_ptr_delete(&op->del.targets);
    break;
  case ZIPKG_OP_UPGRADE:
    for (size_t i = 0; i < vec_char_ptr_len(op->upgrade.targets); i++) {
      free(*vec_char_ptr_at(op->upgrade.targets, i));
    }
    vec_char_ptr_delete(&op->upgrade.targets);
    break;
  case ZIPKG_OP_FIX:
    for (size_t i = 0; i < vec_char_ptr_len(op->fix.targets); i++) {
      free(*vec_char_ptr_at(op->fix.targets, i));
    }
    vec_char_ptr_delete(&op->fix.targets);
    break;
  case ZIPKG_OP_FETCH:
    for (size_t i = 0; i < vec_char_ptr_len(op->fetch.targets); i++) {
      free(*vec_char_ptr_at(op->fetch.targets, i));
    }
    vec_char_ptr_delete(&op->fetch.targets);
    free(op->fetch.output_dir);
    break;
  case ZIPKG_OP_OWNER:
    free(op->owner.path);
    break;
  default:
    break;
  }
}
