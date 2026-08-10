#include "configuration.h"

#include "error.h"
#include "oscompatlayer.h"
#include "pathutils.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <toml/toml.h>

static const char *SYSTEM_CONFIG_PATH = "/etc/zpk.ini";
static const char *USER_CONFIG_PATH = "~/.zpk.ini";
static const char *CONFIGURATION_FILE_NAME = ".zpk.ini";

// Resolves `raw` against the directory containing `config_path`, unless
// `raw` is a URI (contains "://"), already absolute, or home-relative (~).
// Returns a newly allocated string; caller must free.
static char *resolve_config_relative(const char *config_path, const char *raw) {
  if (strstr(raw, "://") != NULL) {
    return strdup(raw);
  }

  char *expanded = expandtilde(raw); // no-op unless raw starts with ~
  if (expanded[0] == '/') {
    return expanded; // absolute already, or made absolute by ~ expansion
  }

  const char *last_slash = strrchr(config_path, '/');
  if (last_slash == NULL) {
    // config_path was a bare filename (e.g. "zpk.ini"): its directory is
    // cwd, and `expanded` is already interpreted relative to cwd as-is.
    return expanded;
  }

  size_t dir_len = (size_t)(last_slash - config_path);
  char *resolved = malloc(dir_len + 1 + strlen(expanded) + 1);
  snprintf(resolved, dir_len + 1 + strlen(expanded) + 1, "%.*s/%s",
           (int)dir_len, config_path, expanded);
  free(expanded);
  return resolved;
}

// validates one apk-style protected path rule from config key `key` and
// appends a copy to out. rules are sysroot-relative masks, so unlike
// repositories they are never resolved against the config file's directory
static void push_protected_path(vec_char_ptr *out, const char *config_path,
                                const char *key, const TomlValue *elem) {
  if (elem->type != TOML_STRING) {
    LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: %s must be strings", config_path,
                   key);
    PANIC();
  }
  const char *rule = elem->value.string->str;
  if (rule[0] != '+' || rule[1] == '\0') {
    LOG_ERROR_ARGS(ERR_LEVEL_FATAL,
                   "%s: %s: rule \"%s\" must be '+' followed by a path "
                   "('+' is the only supported protection mode)",
                   config_path, key, rule);
    PANIC();
  }
  char_ptr copy = strdup(rule);
  vec_char_ptr_push(out, &copy);
}

static void maybe_apply_config_file(ZpkConfiguration *config, const char *path,
                                    bool cli_specified) {
  FILE *maybe_file = fopen(path, "r");
  if (maybe_file == NULL) {
    if (cli_specified) {
      LOG_ERROR_ARGS(ERR_LEVEL_FATAL,
                     "could not open specified config file %s: %s", path,
                     strerror(errno));
      PANIC();
    } else {
      LOG_ERROR_ARGS(ERR_LEVEL_DEBUG,
                     "could not open potential conf location %s: %s", path,
                     strerror(errno));
      return;
    }
  }

  LOG_ERROR_ARGS(ERR_LEVEL_DEBUG, "opened config file %s", path);

  TomlTable *table = toml_load_file_filename(maybe_file, path);
  fclose(maybe_file);
  if (table == NULL) {
    LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "could not parse %s: %s", path,
                   toml_err()->message);
    PANIC();
  }

  TomlValue *val = toml_table_get(table, "sysroot");
  if (val != NULL) {
    if (val->type != TOML_STRING) {
      LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: sysroot must be a string", path);
      PANIC();
    }
    free(config->sysroot);
    config->sysroot = resolve_config_relative(path, val->value.string->str);
  }

  val = toml_table_get(table, "pkgs_path");
  if (val != NULL) {
    if (val->type != TOML_STRING) {
      LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: pkgs_path must be a string", path);
      PANIC();
    }
    free(config->pkgs_path);
    config->pkgs_path = resolve_config_relative(path, val->value.string->str);
  }

  val = toml_table_get(table, "repositories");
  if (val != NULL) {
    if (val->type != TOML_ARRAY) {
      LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: repositories must be an array",
                     path);
      PANIC();
    }
    // repositories (and not extra repositories) means that we replace any
    // existing repositories with the ones in the config file:
    vec_char_ptr_clear_and_freeowned(&config->repositories);
    for (size_t i = 0; i < val->value.array->len; i++) {
      TomlValue *elem = val->value.array->elements[i];
      if (elem->type != TOML_STRING) {
        LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: repositories must be strings",
                       path);
        PANIC();
      }
      char_ptr repo = resolve_config_relative(path, elem->value.string->str);
      vec_char_ptr_push(&config->repositories, &repo);
    }
  }

  val = toml_table_get(table, "extra-repositories");
  if (val != NULL) {
    if (val->type != TOML_ARRAY) {
      LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: extra-repositories must be an array",
                     path);
      PANIC();
    }
    // extra-repositories (and not repositories) means that we append, and keep
    // the ones that are already there.
    for (size_t i = 0; i < val->value.array->len; i++) {
      TomlValue *elem = val->value.array->elements[i];
      if (elem->type != TOML_STRING) {
        LOG_ERROR_ARGS(ERR_LEVEL_FATAL,
                       "%s: extra-repositories must be strings", path);
        PANIC();
      }
      char_ptr repo = resolve_config_relative(path, elem->value.string->str);
      vec_char_ptr_push(&config->repositories, &repo);
    }
  }

  val = toml_table_get(table, "protected-paths");
  if (val != NULL) {
    if (val->type != TOML_ARRAY) {
      LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: protected-paths must be an array",
                     path);
      PANIC();
    }
    // protected-paths (and not extra-protected-paths) replaces any rules
    // from lower-precedence config files
    vec_char_ptr_clear_and_freeowned(&config->protected_paths);
    for (size_t i = 0; i < val->value.array->len; i++) {
      push_protected_path(&config->protected_paths, path, "protected-paths",
                          val->value.array->elements[i]);
    }
  }

  val = toml_table_get(table, "extra-protected-paths");
  if (val != NULL) {
    if (val->type != TOML_ARRAY) {
      LOG_ERROR_ARGS(ERR_LEVEL_FATAL,
                     "%s: extra-protected-paths must be an array", path);
      PANIC();
    }
    // extra-protected-paths appends, keeping the rules that are already there
    for (size_t i = 0; i < val->value.array->len; i++) {
      push_protected_path(&config->protected_paths, path,
                          "extra-protected-paths",
                          val->value.array->elements[i]);
    }
  }

  toml_table_free(table);
}

// splits a comma-separated environment value and appends each entry to `out`.
// unlike a command line, nothing has expanded tildes for us here, so we do it.
static void push_env_repositories(vec_char_ptr *out, const char *env) {
  char *dup = strdup(env);
  for (char *tok = strtok(dup, ","); tok != NULL; tok = strtok(NULL, ",")) {
    if (*tok == '\0') {
      continue;
    }
    char_ptr repo = expandtilde(tok);
    vec_char_ptr_push(out, &repo);
  }
  free(dup);
}

static void apply_env_config(ZpkConfiguration *config) {
  const char *env = getenv("ZPK_SYSROOT");
  if (env != NULL) {
    free(config->sysroot);
    config->sysroot = expandtilde(env);
  }

  env = getenv("ZPK_PKGS_PATH");
  if (env != NULL) {
    free(config->pkgs_path);
    config->pkgs_path = expandtilde(env);
  }

  // comma-separated list; replaces any file-configured repositories
  env = getenv("ZPK_REPOSITORIES");
  if (env != NULL) {
    vec_char_ptr_clear_and_freeowned(&config->repositories);
    push_env_repositories(&config->repositories, env);
  }

  // comma-separated list; extends any file-configured repositories
  env = getenv("ZPK_EXTRA_REPOSITORIES");
  if (env != NULL) {
    push_env_repositories(&config->repositories, env);
  }
}

static void resolve_configuration(ZpkConfiguration *config,
                                  const char *cli_config,
                                  const char *cli_sysroot,
                                  vec_char_ptr *cli_extra_repositories) {
  config->sysroot = NULL;
  config->pkgs_path = NULL;
  vec_char_ptr_init(&config->repositories);
  vec_char_ptr_init(&config->protected_paths);

  // we check in reverse order of precedence, so that later sources override
  // earlier ones.

  // 6. system config file
  if (!cli_config) {
    maybe_apply_config_file(config, SYSTEM_CONFIG_PATH, false);
  }

  // 5. user config file
  if (!cli_config) {
    char *user_config_path_expanded = expandtilde(USER_CONFIG_PATH);
    maybe_apply_config_file(config, user_config_path_expanded, false);
    free(user_config_path_expanded);
  }

  // 4. path walk local config files (eg, local directory and up the tree)
  // we're doing reverse precedence, so we start at the root and work our way
  // down to the current directory, so that the current directory's config
  // overrides any parent directories.
  if (!cli_config) {
    char *cwd = getcwd_portable();
    size_t cwd_len = strlen(cwd);
    // visit every directory prefix of the cwd, shallowest first: "", "/home",
    // "/home/user", ... and finally the cwd itself, hence i <= cwd_len. the
    // empty prefix is the filesystem root, since the "%s/%s" supplies the
    // separator.
    for (size_t i = 0; i <= cwd_len; i++) {
      bool at_end = i == cwd_len;
      if (!at_end && cwd[i] != '/') {
        continue;
      }
      // a cwd of "/" is already covered by the i == 0 prefix
      if (at_end && cwd_len > 0 && cwd[cwd_len - 1] == '/') {
        break;
      }
      cwd[i] = '\0';
      char *config_path =
          (char *)malloc(i + 1 + strlen(CONFIGURATION_FILE_NAME) + 1);
      sprintf(config_path, "%s/%s", cwd, CONFIGURATION_FILE_NAME);
      maybe_apply_config_file(config, config_path, false);
      free(config_path);
      if (!at_end) {
        cwd[i] = '/';
      }
    }
    free(cwd);
  }

  // 3. cli specified config file
  // no need to expand tilde, because the shell should have done that for us
  // when it passed the path to us.
  if (cli_config != NULL) {
    maybe_apply_config_file(config, cli_config, true);
  }

  // 2. environment variables
  apply_env_config(config);

  // 1. cli specified sysroot + repositories
  if (cli_sysroot != NULL) {
    free(config->sysroot);
    config->sysroot = strdup(cli_sysroot);
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
  // sysroot and pkgs_path are assembled from joins that can leave "."
  // components or doubled slashes behind (e.g. "$cwd/./zpkroot"); clean them
  // once here so every path later joined onto them stays clean
  char *cleaned = cleanpath(config->sysroot);
  free(config->sysroot);
  config->sysroot = cleaned;
  cleaned = cleanpath(config->pkgs_path);
  free(config->pkgs_path);
  config->pkgs_path = cleaned;

  if (cli_extra_repositories != NULL) {
    // we follow APK semantics in that -X/--repository appends to the list of
    // repositories, rather than replacing it. so we append the cli-specified
    // repositories to the end of the list.
    for (uint32_t i = 0; i < vec_char_ptr_len(cli_extra_repositories); i++) {
      char_ptr repo = strdup(*vec_char_ptr_at(cli_extra_repositories, i));
      vec_char_ptr_push(&config->repositories, &repo);
    }
  }
}

void delete_ZpkConfiguration(ZpkConfiguration *config) {
  free(config->sysroot);
  free(config->pkgs_path);
  vec_char_ptr_delete_and_freeowned(&config->repositories);
  vec_char_ptr_delete_and_freeowned(&config->protected_paths);
}

static const char *USAGE =
    "usage: zpk [options] <command> [args]\n"
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
    "      --config FILE        use FILE instead of searching for .zpk.ini\n"
    "  -s, --simulate           simulate the operation; make no changes\n"
    "  -v, --verbose            raise log level to info; -vv for debug\n"
    "  -h, --help               show this help\n";

// matches -v, -vv, -vvv, ..., returning how many v's it found, or 0 if `arg` is
// not one of those
static int count_verbose_flag(const char *arg) {
  if (arg[0] != '-') {
    return 0;
  }
  int n = 0;
  for (const char *p = arg + 1; *p != '\0'; p++) {
    if (*p != 'v') {
      return 0;
    }
    n++;
  }
  return n;
}

static ErrSeverity verbosity_to_level(int verbosity) {
  if (verbosity <= 0) {
    return ERR_LEVEL_WARN;
  }
  if (verbosity == 1) {
    return ERR_LEVEL_INFO;
  }
  return ERR_LEVEL_DEBUG;
}

// consumes the next argv element as the value of option argv[*i]
static char *opt_value(int argc, char **argv, int *i) {
  if (*i + 1 >= argc) {
    LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "option '%s' requires a value", argv[*i]);
    PANIC();
  }
  *i += 1;
  return argv[*i];
}

static ZpkOpKind lookup_command(const char *name) {
  if (strcmp(name, "add") == 0)
    return ZPK_OP_ADD;
  if (strcmp(name, "fetch") == 0)
    return ZPK_OP_FETCH;
  if (strcmp(name, "del") == 0)
    return ZPK_OP_DEL;
  if (strcmp(name, "upgrade") == 0)
    return ZPK_OP_UPGRADE;
  if (strcmp(name, "fix") == 0)
    return ZPK_OP_FIX;
  if (strcmp(name, "list") == 0)
    return ZPK_OP_LIST;
  if (strcmp(name, "info") == 0)
    return ZPK_OP_OWNER;
  LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "unknown command '%s' (see 'zpk --help')",
                 name);
  PANIC();
}

void parse_args(int argc, char **argv, ZpkConfiguration *config,
                ZpkOperation *op) {
  const char *cli_sysroot = NULL;
  const char *cli_config = NULL;
  const char *fetch_output = NULL;
  bool list_installed = false;
  bool list_upgradable = false;
  bool list_available = false;
  bool info_who_owns = false;
  bool have_op = false;
  bool no_more_options = false;
  bool dry_run = false;
  int verbosity = 0;
  ZpkOpKind kind = ZPK_OP_ADD; // overwritten when the command is seen

  vec_char_ptr cli_extra_repositories;
  vec_char_ptr_init(&cli_extra_repositories);
  vec_char_ptr targets;
  vec_char_ptr_init(&targets);

  for (int i = 1; i < argc; i++) {
    char *arg = argv[i];
    int nverbose = count_verbose_flag(arg);

    if (no_more_options || arg[0] != '-' || arg[1] == '\0') {
      if (!have_op) {
        kind = lookup_command(arg);
        have_op = true;
      } else {
        char_ptr target = strdup(arg);
        vec_char_ptr_push(&targets, &target);
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
      vec_char_ptr_push(&cli_extra_repositories, &repo);
    } else if (strcmp(arg, "--config") == 0) {
      cli_config = opt_value(argc, argv, &i);
    } else if (strcmp(arg, "-s") == 0 || strcmp(arg, "--simulate") == 0) {
      dry_run = true;
    } else if (nverbose > 0 || strcmp(arg, "--verbose") == 0) {
      verbosity += nverbose > 0 ? nverbose : 1;
      // applied immediately, so that everything after this point -- including
      // the config file search -- logs at the requested level
      g_log_level = verbosity_to_level(verbosity);
    } else if (have_op && kind == ZPK_OP_FETCH &&
               (strcmp(arg, "-o") == 0 || strcmp(arg, "--output") == 0)) {
      fetch_output = opt_value(argc, argv, &i);
    } else if (have_op && kind == ZPK_OP_LIST &&
               (strcmp(arg, "-I") == 0 || strcmp(arg, "--installed") == 0)) {
      list_installed = true;
    } else if (have_op && kind == ZPK_OP_LIST &&
               (strcmp(arg, "-u") == 0 || strcmp(arg, "--upgradable") == 0)) {
      list_upgradable = true;
    } else if (have_op && kind == ZPK_OP_LIST &&
               (strcmp(arg, "-a") == 0 || strcmp(arg, "--available") == 0)) {
      list_available = true;
    } else if (have_op && kind == ZPK_OP_OWNER &&
               (strcmp(arg, "-W") == 0 || strcmp(arg, "--who-owns") == 0)) {
      info_who_owns = true;
    } else {
      LOG_ERROR_ARGS(ERR_LEVEL_FATAL,
                     "unrecognized option '%s' (see 'zpk --help')", arg);
      PANIC();
    }
  }

  if (!have_op) {
    LOG_ERROR(ERR_LEVEL_FATAL, "no operation specified (see 'zpk --help')");
    PANIC();
  }

  uint32_t ntargets = vec_char_ptr_len(&targets);
  switch (kind) {
  case ZPK_OP_ADD:
  case ZPK_OP_DEL:
  case ZPK_OP_FETCH:
    if (ntargets == 0) {
      LOG_ERROR(ERR_LEVEL_FATAL, "at least one package required");
      PANIC();
    }
    break;
  case ZPK_OP_OWNER:
    if (!info_who_owns) {
      LOG_ERROR(ERR_LEVEL_FATAL, "'info' supports only -W/--who-owns");
      PANIC();
    }
    if (ntargets != 1) {
      LOG_ERROR(ERR_LEVEL_FATAL, "'info -W' requires exactly one path");
      PANIC();
    }
    break;
  case ZPK_OP_LIST:
    if (ntargets != 0) {
      LOG_ERROR(ERR_LEVEL_FATAL, "'list' takes no arguments");
      PANIC();
    }
    break;
  default:
    break;
  }

  // args are fully validated; only now touch the filesystem.
  // resolve_configuration copies the -X strings, so we still own these
  resolve_configuration(config, cli_config, cli_sysroot,
                        &cli_extra_repositories);
  vec_char_ptr_delete_and_freeowned(&cli_extra_repositories);

  op->op = kind;
  op->dry_run = dry_run;
  switch (kind) {
  case ZPK_OP_ADD:
    op->add.targets = targets;
    break;
  case ZPK_OP_DEL:
    op->del.targets = targets;
    break;
  case ZPK_OP_UPGRADE:
    op->upgrade.targets = targets;
    break;
  case ZPK_OP_FIX:
    op->fix.targets = targets;
    break;
  case ZPK_OP_FETCH:
    op->fetch.targets = targets;
    op->fetch.output_dir = strdup(fetch_output != NULL ? fetch_output : ".");
    break;
  case ZPK_OP_OWNER:
    op->owner.path = *vec_char_ptr_at(&targets, 0);
    vec_char_ptr_delete(&targets);
    break;
  case ZPK_OP_LIST:
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

void delete_ZpkOperation(ZpkOperation *op) {
  switch (op->op) {
  case ZPK_OP_ADD:
    vec_char_ptr_delete_and_freeowned(&op->add.targets);
    break;
  case ZPK_OP_DEL:
    vec_char_ptr_delete_and_freeowned(&op->del.targets);
    break;
  case ZPK_OP_UPGRADE:
    vec_char_ptr_delete_and_freeowned(&op->upgrade.targets);
    break;
  case ZPK_OP_FIX:
    vec_char_ptr_delete_and_freeowned(&op->fix.targets);
    break;
  case ZPK_OP_FETCH:
    vec_char_ptr_delete_and_freeowned(&op->fetch.targets);
    free(op->fetch.output_dir);
    break;
  case ZPK_OP_OWNER:
    free(op->owner.path);
    break;
  default:
    break;
  }
}
