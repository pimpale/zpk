// Adapted from BearSSL tools/files.c and tools/certs.c:
// https://www.bearssl.org/gitweb/?p=BearSSL;a=blob;f=tools/files.c
// https://www.bearssl.org/gitweb/?p=BearSSL;a=blob;f=tools/certs.c
/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "read_trust_anchor.h"
#include "fileutils.h"
#include "instances/vec_br_x509_trust_anchor.h"
#include "instances/vec_uint8_t.h"
#include <bearssl/inc/bearssl_pem.h>
#include <errno.h>
#include <stddefer.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Recognize one complete DER SEQUENCE, independently of the file extension.
static bool looks_like_der(const uint8_t *buf, size_t len) {
  if (len < 2 || buf[0] != 0x30) {
    return false;
  }
  size_t length_bytes = buf[1];
  buf += 2;
  len -= 2;
  if (length_bytes < 0x80) {
    return length_bytes == len;
  }
  length_bytes -= 0x80;
  if (length_bytes == 0 || length_bytes > sizeof(size_t) || length_bytes > len || buf[0] == 0) {
    return false;
  }
  size_t content_len = 0;
  for (size_t i = 0; i < length_bytes; i++) {
    if (content_len > (SIZE_MAX - buf[i]) / 256) {
      return false;
    }
    content_len = content_len * 256 + buf[i];
  }
  return content_len >= 0x80 && content_len == len - length_bytes;
}

typedef struct {
  vec_uint8_t bytes;
  bool failed;
} ByteCollector;

static void blob_append(void *context, const void *buf, size_t len) {
  ByteCollector *collector = context;
  if (collector->failed) {
    return;
  }
  if (vec_uint8_t_pushv(&collector->bytes, buf, len) != 0) {
    collector->failed = true;
  }
}

static void *memdup(const void *mem, size_t size) {
  void *out = malloc(size);

  if (out != NULL) {
    memcpy(out, mem, size);
  }
  return out;
}

static ReadTrustAnchorError
certificate_to_trust_anchor(br_x509_trust_anchor *anchor, const uint8_t *der, size_t len) {
  *anchor = (br_x509_trust_anchor){0};
  if (!looks_like_der(der, len)) {
    return READ_TRUST_ANCHOR_ERR_CERTIFICATE;
  }
  ByteCollector dn = {0};
  if (vec_uint8_t_init(&dn.bytes) != 0) {
    return READ_TRUST_ANCHOR_ERR_OUT_OF_MEMORY;
  }
  defer vec_uint8_t_delete(&dn.bytes);
  br_x509_decoder_context decoder;
  br_x509_decoder_init(&decoder, blob_append, &dn);
  br_x509_decoder_push(&decoder, der, len);
  if (dn.failed) {
    return READ_TRUST_ANCHOR_ERR_OUT_OF_MEMORY;
  }
  const br_x509_pkey *key = br_x509_decoder_get_pkey(&decoder);
  if (key == NULL) {
    return READ_TRUST_ANCHOR_ERR_CERTIFICATE;
  }
  anchor->flags = br_x509_decoder_isCA(&decoder) ? BR_X509_TA_CA : 0;
  anchor->pkey.key_type = key->key_type;
  switch (key->key_type) {
    case BR_KEYTYPE_RSA:
      anchor->pkey.key.rsa.nlen = key->key.rsa.nlen;
      anchor->pkey.key.rsa.elen = key->key.rsa.elen;
      anchor->pkey.key.rsa.n = memdup(key->key.rsa.n, key->key.rsa.nlen);
      anchor->pkey.key.rsa.e = memdup(key->key.rsa.e, key->key.rsa.elen);
      if (anchor->pkey.key.rsa.n == NULL || anchor->pkey.key.rsa.e == NULL) {
        free_trust_anchor(anchor);
        return READ_TRUST_ANCHOR_ERR_OUT_OF_MEMORY;
      }
      break;
    case BR_KEYTYPE_EC:
      anchor->pkey.key.ec.curve = key->key.ec.curve;
      anchor->pkey.key.ec.qlen = key->key.ec.qlen;
      anchor->pkey.key.ec.q = memdup(key->key.ec.q, key->key.ec.qlen);
      if (anchor->pkey.key.ec.q == NULL) {
        free_trust_anchor(anchor);
        return READ_TRUST_ANCHOR_ERR_OUT_OF_MEMORY;
      }
      break;
    default:
      return READ_TRUST_ANCHOR_ERR_CERTIFICATE;
  }
  // Transfer the DN allocation; public key bytes above are independent of decoder.
  anchor->dn.data = dn.bytes.pData;
  anchor->dn.len = dn.bytes.len;
  dn.bytes = (vec_uint8_t){0};
  return READ_TRUST_ANCHOR_ERR_OK;
}

static ReadTrustAnchorError
append_certificate(vec_br_x509_trust_anchor *anchors, const uint8_t *der, size_t len) {
  br_x509_trust_anchor anchor;
  ReadTrustAnchorError error = certificate_to_trust_anchor(&anchor, der, len);
  if (error != READ_TRUST_ANCHOR_ERR_OK) {
    return error;
  }
  if (vec_br_x509_trust_anchor_push(anchors, &anchor) != 0) {
    free_trust_anchor(&anchor);
    return READ_TRUST_ANCHOR_ERR_OUT_OF_MEMORY;
  }
  return READ_TRUST_ANCHOR_ERR_OK;
}

// Adapted decode_pem/read_certificates: buffer only the current certificate,
// converting directly to anchors instead of retaining a PEM-object list.
static ReadTrustAnchorError
decode_pem(vec_br_x509_trust_anchor *anchors, const uint8_t *buf, size_t len) {
  size_t initial_count = anchors->len;
  br_pem_decoder_context decoder;
  br_pem_decoder_init(&decoder);
  ByteCollector object = {0};
  if (vec_uint8_t_init(&object.bytes) != 0) {
    return READ_TRUST_ANCHOR_ERR_OUT_OF_MEMORY;
  }
  defer vec_uint8_t_delete(&object.bytes);
  bool in_object = false;
  bool certificate = false;
  bool extra_newline = true;
  while (len > 0 || extra_newline) {
    // Match brssl's handling of PEM END banners without a final newline.
    if (len == 0) {
      buf = (const uint8_t *)"\n";
      len = 1;
      extra_newline = false;
    }
    size_t consumed = br_pem_decoder_push(&decoder, buf, len);
    buf += consumed;
    len -= consumed;
    int event = br_pem_decoder_event(&decoder);
    if (object.failed) {
      return READ_TRUST_ANCHOR_ERR_OUT_OF_MEMORY;
    }
    switch (event) {
      case BR_PEM_BEGIN_OBJ: {
        if (in_object) {
          return READ_TRUST_ANCHOR_ERR_PEM;
        }
        const char *name = br_pem_decoder_name(&decoder);
        certificate = strcmp(name, "CERTIFICATE") == 0 || strcmp(name, "X509 CERTIFICATE") == 0;
        in_object = true;
        vec_uint8_t_clear(&object.bytes);
        br_pem_decoder_setdest(&decoder, certificate ? blob_append : NULL, &object);
        break;
      }
      case BR_PEM_END_OBJ:
        if (!in_object) {
          return READ_TRUST_ANCHOR_ERR_PEM;
        }
        if (certificate) {
          ReadTrustAnchorError error =
            append_certificate(anchors, object.bytes.pData, object.bytes.len);
          if (error != READ_TRUST_ANCHOR_ERR_OK) {
            return error;
          }
        }
        in_object = false;
        break;
      case BR_PEM_ERROR:
        return READ_TRUST_ANCHOR_ERR_PEM;
      default:
        if (consumed == 0) {
          return READ_TRUST_ANCHOR_ERR_PEM;
        }
        break;
    }
  }
  if (in_object) {
    return READ_TRUST_ANCHOR_ERR_PEM;
  }
  return anchors->len == initial_count ? READ_TRUST_ANCHOR_ERR_NO_CERTIFICATES
                                       : READ_TRUST_ANCHOR_ERR_OK;
}

ReadTrustAnchorError read_trust_anchors(vec_br_x509_trust_anchor *anchors, const char *path) {
  vec_uint8_t file;
  if (vec_uint8_t_init(&file) != 0) {
    return READ_TRUST_ANCHOR_ERR_OUT_OF_MEMORY;
  }
  defer vec_uint8_t_delete(&file);
  if (read_file(path, &file) != 0) {
    // no file is ok
    if (errno == ENOENT || errno == ENOTDIR) {
      return READ_TRUST_ANCHOR_NOTFOUND;
    }
    return errno == ENOMEM ? READ_TRUST_ANCHOR_ERR_OUT_OF_MEMORY : READ_TRUST_ANCHOR_ERR_IO;
  }
  if (file.len != 0 && file.pData[0] == 0x30) {
    return append_certificate(anchors, file.pData, file.len);
  }
  return decode_pem(anchors, file.pData, file.len);
}

const char *readtrustanchor_strerror(ReadTrustAnchorError e) {
  switch (e) {
    case READ_TRUST_ANCHOR_ERR_OK:
      return "OK";
    case READ_TRUST_ANCHOR_NOTFOUND:
      return "NOTFOUND";
    case READ_TRUST_ANCHOR_ERR_INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    case READ_TRUST_ANCHOR_ERR_IO:
      return "IO";
    case READ_TRUST_ANCHOR_ERR_OUT_OF_MEMORY:
      return "OUT_OF_MEMORY";
    case READ_TRUST_ANCHOR_ERR_PEM:
      return "PEM";
    case READ_TRUST_ANCHOR_ERR_CERTIFICATE:
      return "CERTIFICATE";
    case READ_TRUST_ANCHOR_ERR_NO_CERTIFICATES:
      return "NO_CERTIFICATES";
    case READ_TRUST_ANCHOR_ERR_VERIFICATION_UNSUPPORTED:
      return "VERIFICATION_UNSUPPORTED";
  }
}
