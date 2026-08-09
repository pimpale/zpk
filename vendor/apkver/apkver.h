/* apkver.h - minimal shim for apkver.c (apk-tools version.c)
 *
 * Reimplements the small subset of apk-tools' apk_blob/apk_ctype API that
 * version.c depends on, so the comparison code can stay byte-identical to
 * upstream below its includes. Semantics match apk-tools:
 *   - apk_blob_compare: length-first exact comparison (used for suffix match)
 *   - apk_blob_sort: lexicographic memcmp, shorter-is-less on common prefix
 *   - apk_blob_pull_uint: consume leading digits, advancing the blob cursor
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef apkver_h_INCLUDED
#define apkver_h_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct apk_blob {
  unsigned long len;
  char *ptr;
} apk_blob_t;

#define APK_BLOB_PTR_LEN(beg, length) ((apk_blob_t){(unsigned long)(length), (beg)})
#define APK_BLOB_STR(str) APK_BLOB_PTR_LEN((char *)(str), strlen(str))
#define APK_BLOB_STRLIT(lit) APK_BLOB_PTR_LEN((char *)(lit), sizeof(lit) - 1)
#define APK_BLOB_NULL ((apk_blob_t){0, NULL})
#define APK_BLOB_IS_NULL(blob) ((blob).ptr == NULL)

/* only used by the dbg_printf paths in apkver.c */
#define BLOB_FMT "%.*s"
#define BLOB_PRINTF(blob) (int)(blob).len, (blob).ptr

/* version comparison result flags (bitmask, as in apk_version.h) */
#define APK_VERSION_UNKNOWN 0
#define APK_VERSION_EQUAL 1
#define APK_VERSION_LESS 2
#define APK_VERSION_GREATER 4
#define APK_VERSION_FUZZY 8
#define APK_VERSION_CONFLICT 16
#define APK_DEPMASK_ANY (APK_VERSION_EQUAL | APK_VERSION_LESS | APK_VERSION_GREATER)
#define APK_DEPMASK_CHECKSUM (APK_VERSION_LESS | APK_VERSION_GREATER)

/* character classes used by the version tokenizer */
enum {
  APK_CTYPE_HEXDIGIT,
  APK_CTYPE_VERSION_SUFFIX,
};

static inline int apk_ctype_isin(unsigned char c, int ctype) {
  switch (ctype) {
  case APK_CTYPE_HEXDIGIT:
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  case APK_CTYPE_VERSION_SUFFIX:
    return c >= 'a' && c <= 'z';
  default:
    return 0;
  }
}

/* split blob at the longest prefix of chars in the class: *l = prefix,
 * *r = remainder; returns nonzero if the prefix is non-empty */
static inline int apk_blob_spn(apk_blob_t blob, int ctype, apk_blob_t *l,
                               apk_blob_t *r) {
  unsigned long i = 0;
  while (i < blob.len && apk_ctype_isin((unsigned char)blob.ptr[i], ctype)) {
    i++;
  }
  *l = APK_BLOB_PTR_LEN(blob.ptr, i);
  *r = APK_BLOB_PTR_LEN(blob.ptr + i, blob.len - i);
  return i != 0;
}

static inline int apk_blob_compare(apk_blob_t a, apk_blob_t b) {
  if (a.len == b.len) {
    return memcmp(a.ptr, b.ptr, a.len);
  }
  return a.len < b.len ? -1 : 1;
}

static inline int apk_blob_sort(apk_blob_t a, apk_blob_t b) {
  unsigned long n = a.len < b.len ? a.len : b.len;
  int c = memcmp(a.ptr, b.ptr, n);
  if (c != 0) {
    return c;
  }
  if (a.len != b.len) {
    return a.len < b.len ? -1 : 1;
  }
  return 0;
}

/* parse an unsigned integer off the front of the blob, advancing it past
 * the consumed digits; stops at the first char not valid in the radix */
static inline uint64_t apk_blob_pull_uint(apk_blob_t *b, int radix) {
  uint64_t val = 0;
  while (b->len > 0) {
    unsigned char ch = (unsigned char)b->ptr[0];
    int digit;
    if (ch >= '0' && ch <= '9') {
      digit = ch - '0';
    } else if (ch >= 'a' && ch <= 'z') {
      digit = ch - 'a' + 10;
    } else if (ch >= 'A' && ch <= 'Z') {
      digit = ch - 'A' + 10;
    } else {
      break;
    }
    if (digit >= radix) {
      break;
    }
    val = val * (uint64_t)radix + (uint64_t)digit;
    b->ptr++;
    b->len--;
  }
  return val;
}

/* if the blob starts with token, consume it and return nonzero */
static inline int apk_blob_pull_blob_match(apk_blob_t *b, apk_blob_t token) {
  if (b->len < token.len || memcmp(b->ptr, token.ptr, token.len) != 0) {
    return 0;
  }
  b->ptr += token.len;
  b->len -= token.len;
  return 1;
}

/* public API implemented in apkver.c */
const char *apk_version_op_string(int op);
int apk_version_result_mask_blob(apk_blob_t op);
int apk_version_result_mask(const char *op);
int apk_version_validate(apk_blob_t ver);
int apk_version_compare(apk_blob_t a, apk_blob_t b);
int apk_version_match(apk_blob_t a, int op, apk_blob_t b);

/* convenience wrapper for NUL-terminated strings; returns the
 * APK_VERSION_* flags like apk_version_compare */
static inline int apk_version_compare_str(const char *a, const char *b) {
  return apk_version_compare(APK_BLOB_STR(a), APK_BLOB_STR(b));
}

#endif // apkver_h_INCLUDED
