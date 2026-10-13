#include "repository.h"

#include <asprintf/asprintf.h>
#include <assert.h>
#include <errno.h>
#include <stddefer.h>
#include <stdlib.h>
#include <string.h>

#include "apkver/apkver.h"
#include "error.h"
#include "fileutils.h"
#include "html_a_parser.h"
#include "http_client.h"
#include "instances/llrb_char_ptr_resolvedpackage.h"
#include "instances/slice_uint8_t.h"
#include "instances/vec_br_x509_trust_anchor.h"
#include "instances/vec_char_ptr.h"
#include "instances/vec_slice_uint8_t.h"
#include "oscompatlayer.h"
#include "pathutils.h"
#include "read_trust_anchor.h"
#include "resolvedpackage.h"
#include "tcpcompatlayer.h"
#include "tcpcompatlayer_error.h"
#include "uri.h"

bool version_is_greater(char *a, char *b) {
  return apk_version_compare_str(a, b) == APK_VERSION_GREATER;
}

// doesn't allocate
static apk_blob_t package_version_blob(char *entry, const char *suffix) {
  if (!endswith(entry, suffix)) {
    return APK_BLOB_NULL;
  }

  size_t entry_strlen = strlen(entry);
  size_t suffix_strlen = strlen(suffix);

  char *end = entry + (entry_strlen - suffix_strlen);
  for (char *vstart_m1 = end; vstart_m1 > entry; vstart_m1--) {
    if (*vstart_m1 != '-') {
      continue;
    }
    char *vstart = vstart_m1 + 1;
    apk_blob_t candidate_version = APK_BLOB_PTR_LEN(vstart, end - vstart);

    if (!apk_version_validate(candidate_version)) {
      continue;
    }
    return candidate_version;
  }
  return APK_BLOB_NULL;
}

// allocates

bool package_data(
  // if not NULL, allocates a string containing just the package name
  char **package_name,
  // if not NULL, allocates a string containing just the package name
  char **package_version,
  // package basename
  char *entry,
  // .zip, or .uninstalling.zip or something.
  // Will be ignored from the package name
  const char *suffix
) {
  apk_blob_t version = package_version_blob(entry, suffix);
  if (APK_BLOB_IS_NULL(version)) {
    return false;
  }
  if (package_version != NULL) {
    *package_version = strndup(version.ptr, version.len);
  }
  if (package_name != NULL) {
    *package_name = strndup(entry, strlen(entry) - (1 + version.len + strlen(suffix)));
  }
  return true;
}

typedef struct {
  slice_uint8_t uri;
  ErrVal status;

  // networking stuff
  vec_br_x509_trust_anchor *anchors;
  bool strict_ssl;

  // the only owned portion of this struct
  bool initialized;
  vec_char_ptr entries;
} RepositoryCallbackData;

// getting the newest package
static ErrVal resolve_newest_packages(
  llrb_char_ptr_resolvedpackage *resolved_packages,
  size_t n_repositories,
  RepositoryCallbackData repositories[],
  vec_char_ptr *packages,
  bool none_is_all
) {
  size_t n_packages;
  if (packages == NULL) {
    n_packages = 0;
    assert(none_is_all);
  } else {
    n_packages = vec_char_ptr_len(packages);
  }
  bool insert_all = none_is_all && (n_packages == 0);

  for (size_t r = 0; r < n_repositories; r++) {
    if (repositories[r].status != ERR_OK) {
      continue;
    }
    slice_uint8_t repository = repositories[r].uri;
    vec_char_ptr *entries = &repositories[r].entries;
    size_t n_entries = vec_char_ptr_len(entries);
    for (size_t e = 0; e < n_entries; e++) {
      char *entry = *vec_char_ptr_at(entries, e);

      char *entrypackagename;
      char *entryversion;
      if (!package_data(&entrypackagename, &entryversion, entry, ".zip")) {
        continue;
      }
      defer free(entrypackagename);
      defer free(entryversion);

      ResolvedPackage *brp;
      if (!llrb_char_ptr_resolvedpackage_get_ref(resolved_packages, &entrypackagename, &brp)) {
        ResolvedPackage br = {
          .entry = strdup(entry),
          .package = strdup(entrypackagename),
          .version = strdup(entryversion),
          .repository = slice_uint8_t_to_allocated_str(repository),
          .package_path = NULL
        };
        char *key = strdup(entrypackagename);
        brp = llrb_char_ptr_resolvedpackage_insert(resolved_packages, &key, &br);
        assert(brp != NULL);

      } else {
        if (
          apk_version_compare(APK_BLOB_STR(entryversion), APK_BLOB_STR(brp->version))
          == APK_VERSION_GREATER
        ) {
          // free existing version and repository
          free(brp->version);
          free(brp->repository);

          // take ownership of entry and entryversion
          brp->version = strdup(entryversion);
          brp->repository = slice_uint8_t_to_allocated_str(repository);
        }
      }
    }
  }

  if (!insert_all) {
    bool should_error = false;

    // get the packages that weren't specified in the packages vector
    vec_char_ptr todelete;
    vec_char_ptr_init(&todelete);
    defer vec_char_ptr_delete(&todelete);
    {
      llrb_char_ptr_resolvedpackage_iter iter;
      llrb_char_ptr_resolvedpackage_iter_begin(resolved_packages, &iter);
      char *key;
      ResolvedPackage rp;
      while (llrb_char_ptr_resolvedpackage_iter_next(&iter, &key, &rp)) {
        bool keep = false;
        for (size_t i = 0; i < n_packages; i++) {
          char *package = *vec_char_ptr_at(packages, i);
          if (strcmp(key, package) == 0) {
            keep = true;
            break;
          }
        }
        if (!keep) {
          vec_char_ptr_push(&todelete, &key);
        }
      }
    }

    // now actually remove those from the tree
    for (size_t i = 0; i < vec_char_ptr_len(&todelete); i++) {
      char *key = *vec_char_ptr_at(&todelete, i);
      ResolvedPackage value;
      bool removed = llrb_char_ptr_resolvedpackage_remove(resolved_packages, &key, NULL, &value);
      assert(removed);
      free(key);
      delete_ResolvedPackage(&value);
    }

    // if there exists a package in the package vector not present in the llrb,
    // raise an error
    for (size_t i = 0; i < n_packages; i++) {
      char *package = *vec_char_ptr_at(packages, i);
      if (!llrb_char_ptr_resolvedpackage_get(resolved_packages, &package, NULL)) {
        LOG_ERROR_ARGS(
          ERR_LEVEL_ERROR,
          "resolve: did not find validly named package %s in any repository",
          package
        );
        should_error = true;
        continue;
      }
    }
    if (should_error) {
      return ERR_NOSUCHFILE;
    }
  }
  return ERR_OK;
}

static void populate_file(RepositoryCallbackData *r, slice_uint8_t path) {
  char *path_asciiz = slice_uint8_t_to_allocated_str(path);
  defer free(path_asciiz);
  if (listdir_portable(path_asciiz, &r->entries, NULL) != 0) {
    LOG_ERROR_ARGS(
      ERR_LEVEL_ERROR,
      "resolve: unable to list files in %s: %s",
      path_asciiz,
      strerror(errno)
    );
    r->status = ERR_NOSUCHFILE;
    return;
  }
  r->status = ERR_OK;
  return;
}

static HttpCallbackError linkacc_callback(void *context, const uint8_t *buf, size_t buflen) {
  LinkAccError e = link_acc(context, buf, buflen);
  switch (e) {
    case LINK_ACC_ERR_OK:
      return HTTP_CALLBACK_ERR_OK;
    case LINK_ACC_ERR_NOMEM:
      return HTTP_CALLBACK_ERR_OUT_OF_MEMORY;
  }
}

static void populate_repository(RepositoryCallbackData *r) {
  uri_parse_t parsed_uri;
  bool parse_succeeded = decode_uri(r->uri, &parsed_uri);
  // should have been caught earlier
  assert(parse_succeeded);

  HttpProtocol protocol;
  if (slice_uint8_t_eq(parsed_uri.scheme, slice_uint8_t_from_str("file"))) {
    populate_file(r, parsed_uri.path);
    return;
  } else if (slice_uint8_t_eq(parsed_uri.scheme, slice_uint8_t_from_str("http"))) {
    protocol = HTTP_PROTOCOL_HTTP;
  } else if (slice_uint8_t_eq(parsed_uri.scheme, slice_uint8_t_from_str("https"))) {
    protocol = HTTP_PROTOCOL_HTTPS;
  } else {
    LOG_ERROR_ARGS(
      ERR_LEVEL_ERROR,
      "resolve: unknown scheme %*.s (only file:, http:, and https: permitted)",
      (int)parsed_uri.scheme.len,
      parsed_uri.scheme.data
    );
    r->status = ERR_NOTSUPPORTED;
    return;
  }

  char *host_asciiz = slice_uint8_t_to_allocated_str(parsed_uri.host);
  defer free(host_asciiz);

  char *port_asciiz;
  asprintf(
    &port_asciiz,
    "%d",
    parsed_uri.has_port                ? parsed_uri.port
      : protocol == HTTP_PROTOCOL_HTTP ? 80
                                       : 443
  );
  defer free(port_asciiz);

  char *path_asciiz = slice_uint8_t_to_allocated_str(parsed_uri.path);
  defer free(path_asciiz);

  LinkAccContext ctx = {};

  HttpClientError hce = http_client_get(
    protocol,
    host_asciiz,
    port_asciiz,
    path_asciiz,
    r->anchors,
    r->strict_ssl,
    &ctx,
    linkacc_callback
  );

  switch (hce) {
    case HTTP_CLIENT_ERR_OUT_OF_MEMORY:
      r->status = ERR_MEMORY;
      return;
    default:
      LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "resolve: http get failed: %s", httpstrerror(hce));
      r->status = ERR_UNKNOWN;
      return;
  }
}

typedef struct {
  ErrVal status;

  // networking settings
  vec_br_x509_trust_anchor *anchors;
  bool strict_ssl;

  ResolvedPackage *rp;

} FileDownloadCallbackData;

static HttpCallbackError tofile_callback(void *context, const uint8_t *buf, size_t buflen) {
  FILE *f = context;
  size_t written = fwrite(buf, 1, buflen, f);
  // error if written < buflen
  return written == buflen ? HTTP_CALLBACK_ERR_OK : HTTP_CALLBACK_ERR_IO;
}

static void download_file(FileDownloadCallbackData *d) {
  // parse uri and branch on repository
  uri_parse_t parsed_uri;
  bool parse_succeeded = decode_uri(slice_uint8_t_from_str(d->rp->repository), &parsed_uri);
  // should have been caught earlier
  assert(parse_succeeded);

  char *path_asciiz = slice_uint8_t_to_allocated_str(parsed_uri.path);
  defer free(path_asciiz);

  HttpProtocol protocol;
  if (slice_uint8_t_eq(parsed_uri.scheme, slice_uint8_t_from_str("file"))) {
    if (copy_file(path_asciiz, d->rp->package_path) != 0) {
      LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "download: copy file: %s", strerror(errno));
      d->status = ERR_IO;
      return;
    }
    d->status = ERR_OK;
    return;
  } else if (slice_uint8_t_eq(parsed_uri.scheme, slice_uint8_t_from_str("http"))) {
    protocol = HTTP_PROTOCOL_HTTP;
  } else if (slice_uint8_t_eq(parsed_uri.scheme, slice_uint8_t_from_str("https"))) {
    protocol = HTTP_PROTOCOL_HTTPS;
  } else {
    LOG_ERROR_ARGS(
      ERR_LEVEL_ERROR,
      "download: unknown scheme %*.s (only file:, http:, and https: permitted)",
      (int)parsed_uri.scheme.len,
      parsed_uri.scheme.data
    );
    d->status = ERR_NOTSUPPORTED;
    return;
  }

  char *host_asciiz = slice_uint8_t_to_allocated_str(parsed_uri.host);
  defer free(host_asciiz);

  char *port_asciiz;
  asprintf(
    &port_asciiz,
    "%d",
    parsed_uri.has_port                ? parsed_uri.port
      : protocol == HTTP_PROTOCOL_HTTP ? 80
                                       : 443
  );
  defer free(port_asciiz);

  FILE *file = fopen(d->rp->package_path, "wb");
  if (file == NULL) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "download: couldn't open destination: %s", strerror(errno));
    d->status = ERR_IO;
    return;
  }
  defer fclose(file);

  HttpClientError hce = http_client_get(
    protocol,
    host_asciiz,
    port_asciiz,
    path_asciiz,
    d->anchors,
    d->strict_ssl,
    file,
    tofile_callback
  );

  switch (hce) {
    case HTTP_CLIENT_ERR_OUT_OF_MEMORY:
      d->status = ERR_MEMORY;
      return;
    default:
      LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "download: http get failed: %s", httpstrerror(hce));
      d->status = ERR_UNKNOWN;
      return;
  }
}

ErrVal resolve_package_paths_installed(
  llrb_char_ptr_resolvedpackage *resolved_packages,
  slice_uint8_t directory,
  vec_char_ptr *packages,
  bool none_is_all
) {
  RepositoryCallbackData installedrepo;
  vec_char_ptr_init(&installedrepo.entries);
  defer vec_char_ptr_delete_and_freeowned(&installedrepo.entries);
  populate_file(&installedrepo, directory);
  if (installedrepo.status != ERR_OK) {
    return installedrepo.status;
  }

  ErrVal rnpe =
    resolve_newest_packages(resolved_packages, 1, &installedrepo, packages, none_is_all);
  if (rnpe != 0) {
    return rnpe;
  }

  llrb_char_ptr_resolvedpackage_iter iter;
  llrb_char_ptr_resolvedpackage_iter_begin(resolved_packages, &iter);
  ResolvedPackage *rp;
  while (llrb_char_ptr_resolvedpackage_iter_next_ref(&iter, NULL, &rp)) {
    rp->package_path = joinpath(rp->repository, rp->entry);
  }
  return ERR_OK;
}

// this is the only repository-external func to expose networking.
// network init is lazy till here
ErrVal resolve_and_fetch_package_paths_repositories(
  llrb_char_ptr_resolvedpackage *resolved_packages,
  vec_slice_uint8_t *repository_uris,
  vec_char_ptr *packages,
  slice_uint8_t directory,
  bool none_is_all,
  bool download,
  vec_slice_uint8_t *cacert_paths,
  bool strict_ssl
) {
  // prepare tcp
  TcpError tir = tcp_init_portable();
  if (tir != TCP_ERR_OK) {
    LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "networking error: %s", tcpstrerror(tir));
    return ERR_UNKNOWN;
  }
  defer tcp_cleanup_portable();

  // prepare tls context
  vec_br_x509_trust_anchor anchors;
  vec_br_x509_trust_anchor_init(&anchors);
  defer vec_br_x509_trust_anchor_delete_and_freeowned(&anchors);
  for (size_t i = 0; i < vec_slice_uint8_t_len(cacert_paths); i++) {
    slice_uint8_t path = *vec_slice_uint8_t_at(cacert_paths, i);
    LOG_ERROR_ARGS(
      ERR_LEVEL_DEBUG,
      "tls setup: reading certificate file %.*s",
      (int)path.len,
      path.data
    );
    ReadTrustAnchorError e = read_trust_anchors(&anchors, path);
    switch (e) {
      case READ_TRUST_ANCHOR_ERR_OK:
        LOG_ERROR_ARGS(
          ERR_LEVEL_DEBUG,
          "tls setup: successfully read certificate file %.*s",
          (int)path.len,
          path.data
        );
        break;
      case READ_TRUST_ANCHOR_NOTFOUND:
        LOG_ERROR_ARGS(
          ERR_LEVEL_DEBUG,
          "tls setup: did not find certificate file %.*s",
          (int)path.len,
          path.data
        );
        break;
      default:
        LOG_ERROR_ARGS(
          ERR_LEVEL_ERROR,
          "tls setup: error parsing certificate file %.*s: %s",
          (int)path.len,
          path.data,
          readtrustanchor_strerror(e)
        );
        return ERR_UNKNOWN;
    }
  }

  // fetch from each of the sources
  size_t n_repositories = vec_slice_uint8_t_len(repository_uris);
  RepositoryCallbackData *repositories = calloc(n_repositories, sizeof(RepositoryCallbackData));
  defer {
    for (size_t i = 0; i < n_repositories; i++) {
      if (repositories[i].initialized) {
        vec_char_ptr_delete_and_freeowned(&repositories[i].entries);
      }
    }
    free(repositories);
  }

  // fetch from all repos in sequence for now.
  // later we'll run all of them in parallel via threadpool
  for (size_t i = 0; i < n_repositories; i++) {
    repositories[i] = (RepositoryCallbackData){
      .status = ERR_UNKNOWN,
      .initialized = true,
      .uri = *vec_slice_uint8_t_at(repository_uris, i),
      .strict_ssl = strict_ssl,
      .anchors = &anchors,
    };
    vec_char_ptr_init(&repositories->entries);
    populate_repository(&repositories[i]);
  }

  ErrVal v1 =
    resolve_newest_packages(resolved_packages, n_repositories, repositories, packages, none_is_all);
  if (v1 != ERR_OK) {
    return v1;
  }

  if (!download) {
    return ERR_OK;
  }

  // eventually we'll download in parallel as well
  size_t n_downloads = llrb_char_ptr_resolvedpackage_len(resolved_packages);
  FileDownloadCallbackData *downloads = calloc(n_downloads, sizeof(RepositoryCallbackData));
  defer free(downloads);

  size_t i = 0;
  llrb_char_ptr_resolvedpackage_iter iter;
  llrb_char_ptr_resolvedpackage_iter_begin(resolved_packages, &iter);
  char *key;
  ResolvedPackage *rp;
  while (llrb_char_ptr_resolvedpackage_iter_next_ref(&iter, &key, &rp)) {
    downloads[i] = (FileDownloadCallbackData){
      .status = ERR_UNKNOWN,
      .anchors = &anchors,
      .strict_ssl = strict_ssl,
      .rp = rp
    };
    // for now we just do in sequence
    download_file(&downloads[i]);
    i++;
  }

  return ERR_OK;
}
