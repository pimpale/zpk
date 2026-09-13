#include "configuration.h"

#include "error.h"
#include "oscompatlayer.h"
#include "pathutils.h"
#include <asprintf/asprintf.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <toml/toml.h>

#define SYSTEM_CONFIG_PATH "/etc/zpk.ini"
#define USER_CONFIG_PATH "~/.zpk.ini"
#define CONFIGURATION_FILE_NAME ".zpk.ini"

// allocates
static char *resolve_config_relative(const char *config_path, const char *raw) {
  if (strstr(raw, "://") != NULL) {
    return strdup(raw);
  }

  char *expanded = expandtilde(raw);
  if (expanded[0] == '/') {
    return expanded;
  }

  const char *last_slash = strrchr(config_path, '/');
  if (last_slash == NULL) {
    // config_path was a bare filename (e.g. "zpk.ini")
    return expanded;
  }

  size_t dir_len = (size_t)(last_slash - config_path);
  char *resolved;
  asprintf(&resolved, "%.*s/%s", (int)dir_len, config_path, expanded);
  free(expanded);
  return resolved;
}

// rules are sysroot-relative
static void push_protected_path(
  vec_char_ptr *out,
  const char *config_path,
  const char *key,
  const TomlValue *elem
) {
  if (elem->type != TOML_STRING) {
    LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: %s must be strings", config_path, key);
    PANIC();
  }
  const char *rule = elem->value.string->str;
  if (rule[0] != '+' || rule[1] == '\0') {
    LOG_ERROR_ARGS(
      ERR_LEVEL_FATAL,
      "%s: %s: rule \"%s\" must be '+' followed by a path "
      "('+' is the only supported protection mode)",
      config_path,
      key,
      rule
    );
    PANIC();
  }
  char_ptr copy = strdup(rule);
  vec_char_ptr_push(out, &copy);
}

static void
maybe_apply_config_file(ZpkConfiguration *config, const char *path, bool cli_specified) {
  FILE *maybe_file = fopen(path, "r");
  if (maybe_file == NULL) {
    if (cli_specified) {
      LOG_ERROR_ARGS(
        ERR_LEVEL_FATAL,
        "could not open specified config file %s: %s",
        path,
        strerror(errno)
      );
      PANIC();
    } else {
      LOG_ERROR_ARGS(
        ERR_LEVEL_DEBUG,
        "could not open potential conf location %s: %s",
        path,
        strerror(errno)
      );
      return;
    }
  }

  LOG_ERROR_ARGS(ERR_LEVEL_DEBUG, "opened config file %s", path);

  TomlTable *table = toml_load_file_filename(maybe_file, path);
  fclose(maybe_file);
  if (table == NULL) {
    LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "could not parse %s: %s", path, toml_err()->message);
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
    LOG_ERROR_ARGS(ERR_LEVEL_DEBUG, "%s: sysroot set to %s", path, config->sysroot);
  }

  val = toml_table_get(table, "installed-pkgs-path");
  if (val != NULL) {
    if (val->type != TOML_STRING) {
      LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: installed-pkgs-path must be a string", path);
      PANIC();
    }
    free(config->installed_pkgs_path);
    config->installed_pkgs_path = resolve_config_relative(path, val->value.string->str);
    LOG_ERROR_ARGS(
      ERR_LEVEL_DEBUG,
      "%s: installed-pkgs-path set to %s",
      path,
      config->installed_pkgs_path
    );
  }

  val = toml_table_get(table, "cached-pkgs-path");
  if (val != NULL) {
    if (val->type != TOML_STRING) {
      LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: cached-pkgs-path must be a string", path);
      PANIC();
    }
    free(config->cached_pkgs_path);
    config->cached_pkgs_path = resolve_config_relative(path, val->value.string->str);
    LOG_ERROR_ARGS(
      ERR_LEVEL_DEBUG,
      "%s: cached-pkgs-path set to %s",
      path,
      config->cached_pkgs_path
    );
  }

  val = toml_table_get(table, "repositories");
  if (val != NULL) {
    if (val->type != TOML_ARRAY) {
      LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: repositories must be an array", path);
      PANIC();
    }
    // repositories (and not extra repositories) means that we replace any
    // existing repositories with the ones in the config file:
    LOG_ERROR_ARGS(ERR_LEVEL_DEBUG, "%s: repositories vector reset", path);
    vec_char_ptr_clear_and_freeowned(&config->repositories);
    for (size_t i = 0; i < val->value.array->len; i++) {
      TomlValue *elem = val->value.array->elements[i];
      if (elem->type != TOML_STRING) {
        LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: repositories must be strings", path);
        PANIC();
      }
      char_ptr repo = resolve_config_relative(path, elem->value.string->str);
      vec_char_ptr_push(&config->repositories, &repo);
      LOG_ERROR_ARGS(ERR_LEVEL_DEBUG, "%s: repositories vector: added %s", path, repo);
    }
  }

  val = toml_table_get(table, "extra-repositories");
  if (val != NULL) {
    if (val->type != TOML_ARRAY) {
      LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: extra-repositories must be an array", path);
      PANIC();
    }
    // extra-repositories (and not repositories) means that we append, and keep
    // the ones that are already there.
    for (size_t i = 0; i < val->value.array->len; i++) {
      TomlValue *elem = val->value.array->elements[i];
      if (elem->type != TOML_STRING) {
        LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: extra-repositories must be strings", path);
        PANIC();
      }
      char_ptr repo = resolve_config_relative(path, elem->value.string->str);
      vec_char_ptr_push(&config->repositories, &repo);
      LOG_ERROR_ARGS(ERR_LEVEL_DEBUG, "%s: repositories vector: added %s", path, repo);
    }
  }

  val = toml_table_get(table, "protected-paths");
  if (val != NULL) {
    if (val->type != TOML_ARRAY) {
      LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: protected-paths must be an array", path);
      PANIC();
    }
    // protected-paths (and not extra-protected-paths) replaces any rules
    // from lower-precedence config files
    vec_char_ptr_clear_and_freeowned(&config->protected_paths);
    for (size_t i = 0; i < val->value.array->len; i++) {
      push_protected_path(
        &config->protected_paths,
        path,
        "protected-paths",
        val->value.array->elements[i]
      );
    }
  }

  val = toml_table_get(table, "extra-protected-paths");
  if (val != NULL) {
    if (val->type != TOML_ARRAY) {
      LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: extra-protected-paths must be an array", path);
      PANIC();
    }
    for (size_t i = 0; i < val->value.array->len; i++) {
      push_protected_path(
        &config->protected_paths,
        path,
        "extra-protected-paths",
        val->value.array->elements[i]
      );
    }
  }

  val = toml_table_get(table, "cacert-paths");
  if (val != NULL) {
    if (val->type != TOML_ARRAY) {
      LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: cacert-paths must be an array", path);
      PANIC();
    }
    vec_char_ptr_clear_and_freeowned(&config->cacert_paths);
    for (size_t i = 0; i < val->value.array->len; i++) {
      TomlValue *elem = val->value.array->elements[i];
      if (elem->type != TOML_STRING) {
        LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: cacert-paths must be strings", path);
        PANIC();
      }
      char_ptr cacert = resolve_config_relative(path, elem->value.string->str);
      vec_char_ptr_push(&config->cacert_paths, &cacert);
    }
  }

  val = toml_table_get(table, "extra-cacert-paths");
  if (val != NULL) {
    if (val->type != TOML_ARRAY) {
      LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: extra-cacert-paths must be an array", path);
      PANIC();
    }
    for (size_t i = 0; i < val->value.array->len; i++) {
      TomlValue *elem = val->value.array->elements[i];
      if (elem->type != TOML_STRING) {
        LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: extra-cacert-paths must be strings", path);
        PANIC();
      }
      char_ptr cacert = resolve_config_relative(path, elem->value.string->str);
      vec_char_ptr_push(&config->cacert_paths, &cacert);
    }
  }

  toml_table_free(table);
}

// splits a comma-separated environment value and appends each entry to `out`.
// expand tildes
static void push_env_pathlist(vec_char_ptr *out, const char *env, const char *delim) {
  char *dup = strdup(env);
  for (char *tok = strtok(dup, delim); tok != NULL; tok = strtok(NULL, delim)) {
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

  env = getenv("ZPK_INSTALLED_PKGS_PATH");
  if (env != NULL) {
    free(config->installed_pkgs_path);
    config->installed_pkgs_path = expandtilde(env);
  }

  env = getenv("ZPK_CACHED_PKGS_PATH");
  if (env != NULL) {
    free(config->cached_pkgs_path);
    config->cached_pkgs_path = expandtilde(env);
  }

  env = getenv("ZPK_REPOSITORIES");
  if (env != NULL) {
    vec_char_ptr_clear_and_freeowned(&config->repositories);
    push_env_pathlist(&config->repositories, env, ",");
  }

  env = getenv("ZPK_EXTRA_REPOSITORIES");
  if (env != NULL) {
    push_env_pathlist(&config->repositories, env, ",");
  }

  env = getenv("ZPK_CACERT_PATHS");
  if (env != NULL) {
    vec_char_ptr_clear_and_freeowned(&config->cacert_paths);
    push_env_pathlist(&config->cacert_paths, env, ",");
  }

  env = getenv("ZPK_EXTRA_CACERT_PATHS");
  if (env != NULL) {
    push_env_pathlist(&config->cacert_paths, env, ",");
  }
}

static void resolve_configuration(
  ZpkConfiguration *config,
  const char *cli_config,
  const char *cli_sysroot,
  const bool *cli_check_certificate,
  const bool *cli_simulate,
  vec_char_ptr *cli_extra_repositories
) {
  config->sysroot = NULL;
  config->installed_pkgs_path = NULL;
  config->cached_pkgs_path = NULL;
  config->strict_ssl = true;
  config->download_only = false;
  vec_char_ptr_init(&config->repositories);
  vec_char_ptr_init(&config->protected_paths);
  vec_char_ptr_init(&config->cacert_paths);

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
      char *config_path;
      asprintf(&config_path, "%s/%s", cwd, CONFIGURATION_FILE_NAME);
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

  if (cli_check_certificate != NULL) {
    config->strict_ssl = *cli_check_certificate;
  }
  if (cli_simulate != NULL) {
    config->download_only = *cli_simulate;
  }

  if (config->sysroot == NULL) {
    config->sysroot = strdup("/");
  }

  if (config->installed_pkgs_path == NULL) {
    size_t sysroot_len = strlen(config->sysroot);
    bool trailing_slash = sysroot_len > 0 && config->sysroot[sysroot_len - 1] == '/';
    asprintf(&config->installed_pkgs_path, trailing_slash ? "%spkg" : "%s/pkg", config->sysroot);
  }

  if (config->cached_pkgs_path == NULL) {
    size_t sysroot_len = strlen(config->sysroot);
    bool trailing_slash = sysroot_len > 0 && config->sysroot[sysroot_len - 1] == '/';
    asprintf(
      &config->cached_pkgs_path,
      trailing_slash ? "%spkgcache" : "%s/pkgcache",
      config->sysroot
    );
  }

  char *resolved = abspath_portable(config->sysroot);
  free(config->sysroot);
  config->sysroot = resolved;
  resolved = abspath_portable(config->installed_pkgs_path);
  free(config->installed_pkgs_path);
  config->installed_pkgs_path = resolved;
  resolved = abspath_portable(config->cached_pkgs_path);
  free(config->cached_pkgs_path);
  config->cached_pkgs_path = resolved;

  if (cli_extra_repositories != NULL) {
    // -X/--repository appends (like in apk)
    for (uint32_t i = 0; i < vec_char_ptr_len(cli_extra_repositories); i++) {
      char_ptr repo = strdup(*vec_char_ptr_at(cli_extra_repositories, i));
      vec_char_ptr_push(&config->repositories, &repo);
    }
  }

  if (!config->strict_ssl) {
    LOG_ERROR(
      ERR_LEVEL_WARN,
      "certificate validation is disabled; downloads are not protected against tampering"
    );
  }
}

void delete_zpkconfiguration(ZpkConfiguration *config) {
  free(config->sysroot);
  free(config->installed_pkgs_path);
  free(config->cached_pkgs_path);
  vec_char_ptr_delete_and_freeowned(&config->repositories);
  vec_char_ptr_delete_and_freeowned(&config->protected_paths);
  vec_char_ptr_delete_and_freeowned(&config->cacert_paths);
}

static const char *USAGE =
  "usage: zpk [options] <command> [args]\n"
  "\n"
  "commands:\n"
  "  add <pkg>...                   install packages\n"
  "  del <pkg>...                   uninstall packages\n"
  "  fetch [-o DIR] <pkg>...        download packages without installing\n"
  "  upgrade [pkg...]               upgrade packages (all if none given)\n"
  "  fix [pkg...]                   reinstall broken packages (all if none given)\n"
  "  list [-I] [-u] [-a] [-O]       list installed/upgradable/available/orphaned packages\n"
  "  info -W <path>                 show which package owns a path\n"
  "\n"
  "global options:\n"
  "  -p, --root DIR                 install to alternate root\n"
  "  -X, --repository URI           add a repository (repeatable)\n"
  "      --config FILE              use FILE instead of searching for .zpk.ini\n"
  "  -s, --simulate[=BOOL]          when enabled, downloads data but does not commit to FS\n"
  "  -v, --verbose                  raise log level to info; -vv for debug\n"
  "      --check-certificate[=BOOL] when turned off, disables certificate validation\n"
  "  -h, --help                     show this help\n";

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

// matches an option written either `--opt VALUE` or `--opt=VALUE`; returns NULL
// if arg names some other option
static const char *
match_value_flag(int argc, char **argv, int *i, const char *shortopt, const char *longopt) {
  const char *arg = argv[*i];
  const char *names[2] = {shortopt, longopt};
  for (size_t n = 0; n < 2; n++) {
    if (names[n] == NULL) {
      continue;
    }
    size_t len = strlen(names[n]);
    if (strncmp(arg, names[n], len) != 0) {
      continue;
    }
    if (arg[len] == '\0') {
      return opt_value(argc, argv, i);
    }
    if (arg[len] == '=') {
      return arg + len + 1;
    }
  }
  return NULL;
}

static bool parse_bool_value(const char *opt, const char *value) {
  if (strcmp(value, "yes") == 0) {
    return true;
  }
  if (strcmp(value, "no") == 0) {
    return false;
  }
  LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "option '%s': expected 'yes' or 'no', but got '%s'", opt, value);
  PANIC();
}

static bool match_bool_flag(const char *arg, const char *shortopt, const char *longopt, bool *out) {
  const char *names[2] = {shortopt, longopt};
  for (size_t n = 0; n < 2; n++) {
    if (names[n] == NULL) {
      continue;
    }
    size_t len = strlen(names[n]);
    if (strncmp(arg, names[n], len) != 0) {
      continue;
    }
    if (arg[len] == '\0') {
      *out = true;
      return true;
    }
    if (arg[len] == '=') {
      *out = parse_bool_value(names[n], arg + len + 1);
      return true;
    }
  }
  return false;
}

static ZpkOpKind lookup_command(const char *name) {
  if (strcmp(name, "add") == 0) {
    return ZPK_OP_ADD;
  }
  if (strcmp(name, "fetch") == 0) {
    return ZPK_OP_FETCH;
  }
  if (strcmp(name, "del") == 0) {
    return ZPK_OP_DEL;
  }
  if (strcmp(name, "upgrade") == 0) {
    return ZPK_OP_UPGRADE;
  }
  if (strcmp(name, "fix") == 0) {
    return ZPK_OP_FIX;
  }
  if (strcmp(name, "list") == 0) {
    return ZPK_OP_LIST;
  }
  if (strcmp(name, "info") == 0) {
    return ZPK_OP_OWNER;
  }
  LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "unknown command '%s' (see 'zpk --help')", name);
  PANIC();
}

void parse_args(int argc, char **argv, ZpkConfiguration *config, ZpkOperation *op) {
  const char *cli_sysroot = NULL;
  const char *cli_config = NULL;
  const char *fetch_output = NULL;
  bool list_installed = false;
  bool list_upgradable = false;
  bool list_available = false;
  bool list_orphaned = false;
  bool info_who_owns = false;
  bool have_op = false;
  bool no_more_options = false;
  bool cli_simulate = false;
  bool have_simulate = false;
  bool cli_check_certificate = false;
  bool have_check_certificate = false;
  int verbosity = 0;
  ZpkOpKind kind = ZPK_OP_ADD; // overwritten when the command is seen

  vec_char_ptr cli_extra_repositories;
  vec_char_ptr_init(&cli_extra_repositories);
  vec_char_ptr targets;
  vec_char_ptr_init(&targets);

  for (int i = 1; i < argc; i++) {
    char *arg = argv[i];
    int nverbose = count_verbose_flag(arg);
    const char *val = NULL;

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
    } else if ((val = match_value_flag(argc, argv, &i, "-p", "--root")) != NULL) {
      cli_sysroot = val;
    } else if ((val = match_value_flag(argc, argv, &i, "-X", "--repository")) != NULL) {
      char_ptr repo = strdup(val);
      vec_char_ptr_push(&cli_extra_repositories, &repo);
    } else if ((val = match_value_flag(argc, argv, &i, NULL, "--config")) != NULL) {
      cli_config = val;
    } else if (match_bool_flag(arg, "-s", "--simulate", &cli_simulate)) {
      have_simulate = true;
    } else if (match_bool_flag(arg, NULL, "--check-certificate", &cli_check_certificate)) {
      have_check_certificate = true;
    } else if (nverbose > 0 || strcmp(arg, "--verbose") == 0) {
      verbosity += nverbose > 0 ? nverbose : 1;
      g_log_level = verbosity_to_level(verbosity);
    } else if (
      have_op && kind == ZPK_OP_FETCH
      && (val = match_value_flag(argc, argv, &i, "-o", "--output")) != NULL
    ) {
      fetch_output = val;
    } else if (
      have_op && kind == ZPK_OP_LIST && (strcmp(arg, "-I") == 0 || strcmp(arg, "--installed") == 0)
    ) {
      list_installed = true;
    } else if (
      have_op && kind == ZPK_OP_LIST && (strcmp(arg, "-u") == 0 || strcmp(arg, "--upgradable") == 0)
    ) {
      list_upgradable = true;
    } else if (
      have_op && kind == ZPK_OP_LIST && (strcmp(arg, "-a") == 0 || strcmp(arg, "--available") == 0)
    ) {
      list_available = true;
    } else if (
      have_op && kind == ZPK_OP_LIST && (strcmp(arg, "-O") == 0 || strcmp(arg, "--orphaned") == 0)
    ) {
      list_orphaned = true;
    } else if (
      have_op && kind == ZPK_OP_OWNER && (strcmp(arg, "-W") == 0 || strcmp(arg, "--who-owns") == 0)
    ) {
      info_who_owns = true;
    } else {
      LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "unrecognized option '%s' (see 'zpk --help')", arg);
      PANIC();
    }
  }

  if (!have_op) {
    LOG_ERROR(ERR_LEVEL_FATAL, "no operation specified (see 'zpk --help')");
    PANIC();
  }

  size_t ntargets = vec_char_ptr_len(&targets);
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
  resolve_configuration(
    config,
    cli_config,
    cli_sysroot,
    have_check_certificate ? &cli_check_certificate : NULL,
    have_simulate ? &cli_simulate : NULL,
    &cli_extra_repositories
  );
  vec_char_ptr_delete_and_freeowned(&cli_extra_repositories);

  op->op = kind;
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
      op->list.installed = list_installed;
      op->list.upgradable = list_upgradable;
      op->list.available = list_available;
      op->list.orphaned = list_orphaned;
      vec_char_ptr_delete(&targets);
      break;
  }
}

void delete_zpkoperation(ZpkOperation *op) {
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
