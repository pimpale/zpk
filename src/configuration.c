#include "configuration.h"

#include "error.h"
#include "instances/slice_uint8_t.h"
#include "instances/vec_slice_uint8_t.h"
#include "oscompatlayer.h"
#include "pathutils.h"
#include "uri.h"
#include <asprintf/asprintf.h>

#include <errno.h>
#include <stddefer.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <toml/toml.h>

#define SYSTEM_CONFIG_PATH "/etc/zpk.ini"
#define USER_CONFIG_PATH "~/.zpk.ini"
#define CONFIGURATION_FILE_NAME ".zpk.ini"

// duplicate slice or fail
static slice_uint8_t slicedup(slice_uint8_t in) {
  slice_uint8_t out;
  if (slice_uint8_t_dup(&in, &out) != 0) {
    LOG_ERROR(ERR_LEVEL_FATAL, "out of memory");
    PANIC();
  }
  return out;
}

// returns the dir of the file by stripping the last one
static slice_uint8_t getconfig_dir(char *config_path) {
  const char *last_slash = strrchr(config_path, '/');
  if (last_slash == NULL) {
    return slice_uint8_t_from_str(getcwd_portable());
  } else {
    return (
      slice_uint8_t
    ){.data = (uint8_t *)config_path, .len = (size_t)(last_slash - config_path)};
  }
}

// allocates a new string with the tilde expanded to the user's home directory,
// if applicable. only expands tilde at the start of the string, and only if
// followed by a slash or end of string. errors are fatal
static bool can_expandtilde_slice(slice_uint8_t slice) {
  bool expand = false;
  switch (slice.len) {
    case 0:
      LOG_ERROR(ERR_LEVEL_FATAL, "could not expand tilde: bad input path: empty string");
      PANIC();
    case 1:
      if (slice.data[0] == '~') {
        expand = true;
      }
      break;
    default:
      if (slice.data[0] == '~' && slice.data[1] == '/') {
        expand = true;
      }
      break;
  }
  return expand;
}

// allocates
static slice_uint8_t
resolve_path(slice_uint8_t config_dir, slice_uint8_t input, bool expand_tilde, slice_uint8_t home) {
  char *unnormalized;
  if (expand_tilde && can_expandtilde_slice(input)) {
    unnormalized =
      joinpath_slice_str(home, (slice_uint8_t){.data = input.data + 1, .len = input.len - 1});
  } else if (path_is_absolute_portable((char *)input.data, input.len)) {
    unnormalized = slice_uint8_t_to_allocated_str(input);
  } else {
    unnormalized = joinpath_slice_str(config_dir, input);
  }

  char *out = abspath_portable(unnormalized);

  free(unnormalized);
  return slice_uint8_t_from_str(out);
}

// allocates
static slice_uint8_t resolve_path_uri(
  slice_uint8_t config_dir,
  slice_uint8_t input,
  bool expand_tilde,
  slice_uint8_t home
) {
  // try to parse uri
  uri_parse_t parsed;
  if (!decode_uri(input, &parsed)) {
    LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%.*s: failed to parse URI", (int)input.len, input.data);
    PANIC();
  }

  if (!slice_uint8_t_eq(parsed.scheme, slice_uint8_t_from_str("file"))) {
    return slicedup(input);
  }
  parsed.path = resolve_path(config_dir, parsed.path, expand_tilde, home);

  slice_uint8_t output;
  if (!encode_uri(&output, parsed)) {
    LOG_ERROR(ERR_LEVEL_FATAL, "failed to resolve relative URI: ran out of memory");
    PANIC();
  }
  free(parsed.path.data);
  return output;
}

// rules are sysroot-relative
static void push_protected_path(
  vec_slice_uint8_t *out,
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
  if (copy == NULL) {
    LOG_ERROR(ERR_LEVEL_FATAL, "failed to allocate memory for protected path");
    PANIC();
  }
  slice_uint8_t slice = slice_uint8_t_from_str(copy);
  vec_slice_uint8_t_push(out, &slice);
}

static void maybe_apply_config_file(
  ZpkConfiguration *config,
  char *path,
  bool cli_specified,
  slice_uint8_t home
) {
  FILE *maybe_file = fopen(path, "r");
  slice_uint8_t config_dir = getconfig_dir(path);

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
    free(config->sysroot.data);
    config->sysroot =
      resolve_path(config_dir, slice_uint8_t_from_str(val->value.string->str), true, home);
    LOG_ERROR_ARGS(
      ERR_LEVEL_DEBUG,
      "%s: sysroot set to %.*s",
      path,
      (int)config->sysroot.len,
      config->sysroot.data
    );
  }

  val = toml_table_get(table, "installed-pkgs-path");
  if (val != NULL) {
    if (val->type != TOML_STRING) {
      LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: installed-pkgs-path must be a string", path);
      PANIC();
    }
    free(config->installed_pkgs_path.data);
    config->installed_pkgs_path =
      resolve_path(config_dir, slice_uint8_t_from_str(val->value.string->str), true, home);
    LOG_ERROR_ARGS(
      ERR_LEVEL_DEBUG,
      "%s: installed-pkgs-path set to %.*s",
      path,
      (int)config->installed_pkgs_path.len,
      config->installed_pkgs_path.data
    );
  }

  val = toml_table_get(table, "cached-pkgs-path");
  if (val != NULL) {
    if (val->type != TOML_STRING) {
      LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: cached-pkgs-path must be a string", path);
      PANIC();
    }
    free(config->cached_pkgs_path.data);
    config->cached_pkgs_path =
      resolve_path(config_dir, slice_uint8_t_from_str(val->value.string->str), true, home);
    LOG_ERROR_ARGS(
      ERR_LEVEL_DEBUG,
      "%s: cached-pkgs-path set to %.*s",
      path,
      (int)config->cached_pkgs_path.len,
      config->cached_pkgs_path.data
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
    vec_slice_uint8_t_clear_and_freeowned(&config->repositories);
    for (size_t i = 0; i < val->value.array->len; i++) {
      TomlValue *elem = val->value.array->elements[i];
      if (elem->type != TOML_STRING) {
        LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: repositories must be strings", path);
        PANIC();
      }
      slice_uint8_t repo =
        resolve_path_uri(config_dir, slice_uint8_t_from_str(elem->value.string->str), true, home);
      vec_slice_uint8_t_push(&config->repositories, &repo);
      LOG_ERROR_ARGS(
        ERR_LEVEL_DEBUG,
        "%s: repositories vector: added %.*s",
        path,
        (int)repo.len,
        repo.data
      );
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
      slice_uint8_t repo =
        resolve_path_uri(config_dir, slice_uint8_t_from_str(elem->value.string->str), true, home);
      vec_slice_uint8_t_push(&config->repositories, &repo);
      LOG_ERROR_ARGS(
        ERR_LEVEL_DEBUG,
        "%s: repositories vector: added %.*s",
        path,
        (int)repo.len,
        repo.data
      );
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
    vec_slice_uint8_t_clear_and_freeowned(&config->protected_paths);
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
    vec_slice_uint8_t_clear_and_freeowned(&config->cacert_paths);
    for (size_t i = 0; i < val->value.array->len; i++) {
      TomlValue *elem = val->value.array->elements[i];
      if (elem->type != TOML_STRING) {
        LOG_ERROR_ARGS(ERR_LEVEL_FATAL, "%s: cacert-paths must be strings", path);
        PANIC();
      }
      slice_uint8_t cacert_path =
        resolve_path(config_dir, slice_uint8_t_from_str(elem->value.string->str), true, home);
      vec_slice_uint8_t_push(&config->cacert_paths, &cacert_path);
      LOG_ERROR_ARGS(
        ERR_LEVEL_DEBUG,
        "%s: cacert-paths vector: added %.*s",
        path,
        (int)cacert_path.len,
        cacert_path.data
      );
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
      slice_uint8_t cacert_path =
        resolve_path(config_dir, slice_uint8_t_from_str(elem->value.string->str), true, home);
      vec_slice_uint8_t_push(&config->cacert_paths, &cacert_path);
      LOG_ERROR_ARGS(
        ERR_LEVEL_DEBUG,
        "%s: cacert-paths vector: added %.*s",
        path,
        (int)cacert_path.len,
        cacert_path.data
      );
    }
  }

  toml_table_free(table);
}

// splits a comma-separated environment value and appends each entry to `out`.
// expand tildes

static void push_env_pathlist(
  vec_slice_uint8_t *out,
  const char *env,
  const char *delim,
  slice_uint8_t cwd,
  slice_uint8_t home,
  bool uri
) {
  char *dup = strdup(env);
  for (char *tok = strtok(dup, delim); tok != NULL; tok = strtok(NULL, delim)) {
    if (*tok == '\0') {
      continue;
    }
    slice_uint8_t expanded = uri ? resolve_path_uri(cwd, slice_uint8_t_from_str(tok), true, home)
                                 : resolve_path(cwd, slice_uint8_t_from_str(tok), true, home);
    vec_slice_uint8_t_push(out, &expanded);
  }
  free(dup);
}

static void apply_env_config(ZpkConfiguration *config, slice_uint8_t cwd, slice_uint8_t home) {
  char *env = getenv("ZPK_SYSROOT");
  if (env != NULL) {
    free(config->sysroot.data);
    config->sysroot = resolve_path(cwd, slice_uint8_t_from_str(env), true, home);
  }

  env = getenv("ZPK_INSTALLED_PKGS_PATH");
  if (env != NULL) {
    free(config->installed_pkgs_path.data);
    config->installed_pkgs_path = resolve_path(cwd, slice_uint8_t_from_str(env), true, home);
  }

  env = getenv("ZPK_CACHED_PKGS_PATH");
  if (env != NULL) {
    free(config->cached_pkgs_path.data);
    config->cached_pkgs_path = resolve_path(cwd, slice_uint8_t_from_str(env), true, home);
  }

  env = getenv("ZPK_REPOSITORIES");
  if (env != NULL) {
    vec_slice_uint8_t_clear_and_freeowned(&config->repositories);
    push_env_pathlist(&config->repositories, env, ",", cwd, home, true);
  }

  env = getenv("ZPK_EXTRA_REPOSITORIES");
  if (env != NULL) {
    push_env_pathlist(&config->repositories, env, ",", cwd, home, true);
  }

  env = getenv("ZPK_CACERT_PATHS");
  if (env != NULL) {
    vec_slice_uint8_t_clear_and_freeowned(&config->cacert_paths);
    push_env_pathlist(&config->cacert_paths, env, ",", cwd, home, false);
  }

  env = getenv("ZPK_EXTRA_CACERT_PATHS");
  if (env != NULL) {
    push_env_pathlist(&config->cacert_paths, env, ",", cwd, home, false);
  }
}

static void resolve_configuration(
  ZpkConfiguration *config,
  slice_uint8_t cwd,
  slice_uint8_t home,
  char *cli_config,
  char *cli_sysroot,
  bool *cli_check_certificate,
  bool *cli_simulate,
  vec_slice_uint8_t *cli_extra_repositories
) {
  config->sysroot = (slice_uint8_t){.data = NULL, .len = 0};
  config->installed_pkgs_path = (slice_uint8_t){.data = NULL, .len = 0};
  config->cached_pkgs_path = (slice_uint8_t){.data = NULL, .len = 0};
  config->strict_ssl = true;
  config->download_only = false;
  vec_slice_uint8_t_init(&config->repositories);
  vec_slice_uint8_t_init(&config->protected_paths);
  vec_slice_uint8_t_init(&config->cacert_paths);

  // we check in reverse order of precedence, so that later sources override
  // earlier ones.

  // 6. system config file
  if (!cli_config) {
    maybe_apply_config_file(config, SYSTEM_CONFIG_PATH, false, home);
  }

  // 5. user config file
  if (!cli_config) {
    char *user_config_path_expanded = expandtilde(USER_CONFIG_PATH);
    maybe_apply_config_file(config, user_config_path_expanded, false, home);
    free(user_config_path_expanded);
  }

  // 4. path walk local config files (eg, local directory and up the tree)
  // we're doing reverse precedence, so we start at the root and work our way
  // down to the current directory, so that the current directory's config
  // overrides any parent directories.
  if (!cli_config) {
    for (size_t i = 0; i <= cwd.len; i++) {
      bool at_end = i == cwd.len;
      if (!at_end && cwd.data[i] != '/') {
        continue;
      }
      // a cwd of "/" is already covered by the i == 0 prefix
      if (at_end && cwd.len > 0 && cwd.data[cwd.len - 1] == '/') {
        break;
      }
      char *config_path;
      asprintf(&config_path, "%.*s/%s", (int)(i), cwd.data, CONFIGURATION_FILE_NAME);
      maybe_apply_config_file(config, config_path, false, home);
      free(config_path);
    }
  }

  // 3. cli specified config file
  // no need to expand tilde, because the shell should have done that for us
  // when it passed the path to us.
  if (cli_config != NULL) {
    maybe_apply_config_file(config, cli_config, true, home);
  }

  // 2. environment variables
  apply_env_config(config, cwd, home);

  // 1. cli specified sysroot + repositories
  if (cli_sysroot != NULL) {
    free(config->sysroot.data);
    config->sysroot = resolve_path(cwd, slice_uint8_t_from_str(cli_sysroot), false, home);
  }

  if (cli_check_certificate != NULL) {
    config->strict_ssl = *cli_check_certificate;
  }
  if (cli_simulate != NULL) {
    config->download_only = *cli_simulate;
  }

  if (config->sysroot.data == NULL) {
    config->sysroot = resolve_path(cwd, slice_uint8_t_from_str("/"), false, home);
  }

  if (config->installed_pkgs_path.data == NULL) {
    config->installed_pkgs_path = joinpath_slice(config->sysroot, slice_uint8_t_from_str("pkg"));
  }

  if (config->cached_pkgs_path.data == NULL) {
    config->cached_pkgs_path = joinpath_slice(config->sysroot, slice_uint8_t_from_str("pkgcache"));
  }

  if (cli_extra_repositories != NULL) {
    // -X/--repository appends (like in apk)
    for (uint32_t i = 0; i < vec_slice_uint8_t_len(cli_extra_repositories); i++) {
      slice_uint8_t tmp =
        resolve_path_uri(cwd, *vec_slice_uint8_t_at(cli_extra_repositories, i), false, home);
      if (vec_slice_uint8_t_push(&config->repositories, &tmp) != 0) {
        LOG_ERROR(ERR_LEVEL_FATAL, "ran out of memory adding repositories");
        PANIC();
      }
    }
  }
}

void delete_zpkconfiguration(ZpkConfiguration *config) {
  free(config->sysroot.data);
  free(config->installed_pkgs_path.data);
  free(config->cached_pkgs_path.data);
  vec_slice_uint8_t_delete_and_freeowned(&config->repositories);
  vec_slice_uint8_t_delete_and_freeowned(&config->protected_paths);
  vec_slice_uint8_t_delete_and_freeowned(&config->cacert_paths);
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
static char *match_value_flag(int argc, char **argv, int *i, char *shortopt, char *longopt) {
  char *arg = argv[*i];
  char *names[2] = {shortopt, longopt};
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
  char *cli_sysroot = NULL;
  char *cli_config = NULL;
  char *fetch_output = NULL;
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

  vec_slice_uint8_t cli_extra_repositories;
  // contents are not owned
  vec_slice_uint8_t_init(&cli_extra_repositories);
  vec_char_ptr targets;
  vec_char_ptr_init(&targets);

  // get cwd
  char *cwd = getcwd_portable();
  char *home = getenv_home_portable();

  for (int i = 1; i < argc; i++) {
    char *arg = argv[i];
    int nverbose = count_verbose_flag(arg);
    char *val = NULL;

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
      slice_uint8_t repo = slice_uint8_t_from_str(val);
      vec_slice_uint8_t_push(&cli_extra_repositories, &repo);
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
    slice_uint8_t_from_str(cwd),
    slice_uint8_t_from_str(home),
    cli_config,
    cli_sysroot,
    have_check_certificate ? &cli_check_certificate : NULL,
    have_simulate ? &cli_simulate : NULL,
    &cli_extra_repositories
  );
  vec_slice_uint8_t_delete(&cli_extra_repositories);
  free(cwd);
  free(home);

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
