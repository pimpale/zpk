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

bool version_is_greater(slice_uint8_t a, slice_uint8_t b) {
  return apk_version_compare(APK_BLOB_PTR_LEN(a.data, a.len), APK_BLOB_PTR_LEN(b.data, b.len))
    == APK_VERSION_GREATER;
}

// doesn't allocate
static slice_uint8_t package_version_blob(slice_uint8_t entry) {
  uint8_t *end = entry.data + entry.len;
  for (uint8_t *vstart_m1 = end; vstart_m1 > entry.data; vstart_m1--) {
    if (*vstart_m1 != '-') {
      continue;
    }
    uint8_t *vstart = vstart_m1 + 1;
    apk_blob_t candidate_version = APK_BLOB_PTR_LEN(vstart, end - vstart);

    if (!apk_version_validate(candidate_version)) {
      continue;
    }
    return (slice_uint8_t){.data = (uint8_t *)candidate_version.ptr, .len = candidate_version.len};
  }
  return (slice_uint8_t){};
}

// doesnt allocate
static bool package_data(
  parsed_package_t *parsed,
  // path
  slice_uint8_t path,
  // .zip, or .uninstalling.zip or something.
  // Will be ignored from the package name
  const char *suffix
) {
  size_t suflen = strlen(suffix);

  // get entry
  parsed->entry = basename_slice(path);
  if (parsed->entry.data == NULL) {
    return false;
  }

  // validate entry ends with suffix
  if (parsed->entry.len < suflen) {
    return false;
  }
  if (memcmp(parsed->entry.data + (parsed->entry.len - suflen), suffix, suflen) != 0) {
    return false;
  }
  slice_uint8_t suffixless = {.data = parsed->entry.data, .len = parsed->entry.len - suflen};

  parsed->version = package_version_blob(suffixless);
  if (parsed->version.data == NULL) {
    return false;
  }
  parsed->name = (slice_uint8_t){
    .data = parsed->entry.data,
    .len = parsed->entry.len - (1 + parsed->version.len + suflen)
  };
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
  vec_slice_uint8_t package_uris;
} RepositoryCallbackData;

// getting the newest package
static ErrVal resolve_newest_packages(
  llrb_char_ptr_resolvedpackage *resolved_packages,
  size_t n_repositories,
  RepositoryCallbackData repositories[],
  vec_char_ptr *packages,
  bool none_is_all,
  slice_uint8_t directory
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
    vec_slice_uint8_t *entries = &repositories[r].package_uris;
    size_t n_entries = vec_slice_uint8_t_len(entries);
    for (size_t e = 0; e < n_entries; e++) {
      ResolvedPackage br = {};
      if (
        slice_uint8_t_dup(vec_slice_uint8_t_at(entries, e), &br.source_uri_storage) != SLICE_ERR_OK
      ) {
        return ERR_ALLOCFAIL;
      }
      if (!decode_uri(br.source_uri_storage, &br.source_uri)) {
        free(br.source_uri_storage.data);
        continue;
      }
      if (!package_data(&br.package, br.source_uri.path, ".zip")) {
        free(br.source_uri_storage.data);
        continue;
      }

      char *key = slice_uint8_t_to_allocated_str(br.package.name);

      {
        ResolvedPackage *brp;
        if (!llrb_char_ptr_resolvedpackage_get_ref(resolved_packages, &key, &brp)) {
          // own key, br
          ResolvedPackage *new_brp =
            llrb_char_ptr_resolvedpackage_insert(resolved_packages, &key, &br);
          assert(new_brp != NULL);
        } else {
          free(key);

          if (version_is_greater(br.package.version,brp->package.version)) {
            // own br
            delete_ResolvedPackage(brp);
            *brp = br;
          } else {
            free(br.source_uri_storage.data);
          }
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

  // for the packages that are left in the tree, initialize their target path via directory
  llrb_char_ptr_resolvedpackage_iter iter;
  llrb_char_ptr_resolvedpackage_iter_begin(resolved_packages, &iter);
  char *key;
  ResolvedPackage *rp;
  while (llrb_char_ptr_resolvedpackage_iter_next_ref(&iter, &key, &rp)) {
    rp->local_path = joinpath_slice_str(directory, rp->package.entry);
  }

  return ERR_OK;
}

static void populate_file(RepositoryCallbackData *r, slice_uint8_t path) {
  char *path_asciiz = slice_uint8_t_to_allocated_str(path);
  defer free(path_asciiz);

  vec_char_ptr entries;
  vec_char_ptr_init(&entries);
  defer vec_char_ptr_delete_and_freeowned(&entries);

  if (listdir_portable(path_asciiz, &entries, NULL) != 0) {
    LOG_ERROR_ARGS(
      ERR_LEVEL_ERROR,
      "resolve: unable to list files in %s: %s",
      path_asciiz,
      strerror(errno)
    );
    r->status = ERR_NOSUCHFILE;
    return;
  }

  // now iterate through and create the uris
  slice_uint8_t file_uri_path = joinslice(slice_uint8_t_from_str("file:"), path);
  defer free(file_uri_path.data);
  for (size_t i = 0; i < vec_char_ptr_len(&entries); i++) {
    slice_uint8_t output =
      joinpath_slice(file_uri_path, slice_uint8_t_from_str(*vec_char_ptr_at(&entries, i)));
    vec_slice_uint8_t_push(&r->package_uris, &output);
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
      "resolve: unknown scheme %.*s (only file:, http:, and https: permitted)",
      (int)parsed_uri.scheme.len,
      parsed_uri.scheme.data
    );
    r->status = ERR_NOTSUPPORTED;
    return;
  }

  char *host_asciiz = slice_uint8_t_to_allocated_str(parsed_uri.host);
  defer free(host_asciiz);

  char *port_asciiz = slice_uint8_t_to_allocated_str(
    parsed_uri.has_port ? parsed_uri.port
                        : slice_uint8_t_from_str(protocol == HTTP_PROTOCOL_HTTP ? "80" : "443")
  );
  defer free(port_asciiz);

  char *path_asciiz = slice_uint8_t_to_allocated_str(parsed_uri.path);
  defer free(path_asciiz);

  LinkAccContext ctx = {.state = LINK_ACC_HTML_TEXT, .failed = false, .links = &r->package_uris};
  vec_uint8_t_init(&ctx.linkbuf);

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

  vec_uint8_t_delete(&ctx.linkbuf);

  switch (hce) {
    case HTTP_CLIENT_ERR_OK:
      r->status = ERR_OK;
      return;
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
  char *path_asciiz = slice_uint8_t_to_allocated_str(d->rp->source_uri.path);
  defer free(path_asciiz);

  HttpProtocol protocol;
  if (slice_uint8_t_eq(d->rp->source_uri.scheme, slice_uint8_t_from_str("file"))) {
    LOG_ERROR_ARGS(
      ERR_LEVEL_DEBUG,
      "download: copying file: from %s to %s",
      path_asciiz,
      d->rp->local_path
    );
    if (copy_file(path_asciiz, d->rp->local_path) != 0) {
      LOG_ERROR_ARGS(ERR_LEVEL_ERROR, "download: copy file: %s", strerror(errno));
      d->status = ERR_IO;
      return;
    }
    d->status = ERR_OK;
    return;
  } else if (slice_uint8_t_eq(d->rp->source_uri.scheme, slice_uint8_t_from_str("http"))) {
    protocol = HTTP_PROTOCOL_HTTP;
  } else if (slice_uint8_t_eq(d->rp->source_uri.scheme, slice_uint8_t_from_str("https"))) {
    protocol = HTTP_PROTOCOL_HTTPS;
  } else {
    LOG_ERROR_ARGS(
      ERR_LEVEL_ERROR,
      "download: unknown scheme %.*s (only file:, http:, and https: permitted)",
      (int)d->rp->source_uri.scheme.len,
      d->rp->source_uri.scheme.data
    );
    d->status = ERR_NOTSUPPORTED;
    return;
  }

  char *host_asciiz = slice_uint8_t_to_allocated_str(d->rp->source_uri.host);
  defer free(host_asciiz);

  char *port_asciiz = slice_uint8_t_to_allocated_str(
    d->rp->source_uri.has_port
      ? d->rp->source_uri.port
      : slice_uint8_t_from_str(protocol == HTTP_PROTOCOL_HTTP ? "80" : "443")
  );
  defer free(port_asciiz);

  FILE *file = fopen(d->rp->local_path, "wb");
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
    case HTTP_CLIENT_ERR_OK:
      d->status = ERR_OK;
      return;
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
  vec_slice_uint8_t_init(&installedrepo.package_uris);
  defer vec_slice_uint8_t_delete_and_freeowned(&installedrepo.package_uris);
  populate_file(&installedrepo, directory);
  if (installedrepo.status != ERR_OK) {
    return installedrepo.status;
  }

  ErrVal rnpe =
    resolve_newest_packages(resolved_packages, 1, &installedrepo, packages, none_is_all, directory);
  if (rnpe != 0) {
    return rnpe;
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
        vec_slice_uint8_t_delete_and_freeowned(&repositories[i].package_uris);
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
    vec_slice_uint8_t_init(&repositories[i].package_uris);
    populate_repository(&repositories[i]);
  }

  ErrVal v1 = resolve_newest_packages(
    resolved_packages,
    n_repositories,
    repositories,
    packages,
    none_is_all,
    directory
  );
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

  {
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
  }

  // on download error, bail
  for (size_t i = 0; i < n_downloads; i++) {
    if (downloads[i].status != ERR_OK) {
      return downloads[i].status;
    }
  }
  return ERR_OK;
}
