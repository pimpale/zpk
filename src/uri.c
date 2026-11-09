#include "uri.h"
#include "instances/slice_uint8_t.h"
#include "instances/vec_uint8_t.h"
#include "pathutils.h"
#include <assert.h>
#include <stddef.h>
#include <stddefer.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool parse_hostname(slice_uint8_t input, size_t *pi, uri_parse_t *out) {
  size_t oi = *pi;
  size_t i = oi;

  // init
  out->host.data = input.data + oi;

LOOP:
  for (; i < input.len; i++) {
    switch (input.data[i]) {
      case 'a' ... 'z':
      case 'A' ... 'Z':
      case '0' ... '9':
      case '-':
      case '.':
        break;
      default:
        break LOOP;
    }
  }
  if (i == oi) {
    // zero length is bad hostname
    return false;
  }
  // otherwise good
  *pi = i;
  out->host.len = i - oi;
  return true;
}

static bool parse_ipv6(slice_uint8_t input, size_t *pi, uri_parse_t *out) {
  size_t oi = *pi;
  size_t i = oi;

  enum Ipv6ParseState {
    IPV6_STATE_BEGIN,
    IPV6_STATE_MIDDLE
  };
  enum Ipv6ParseState state = IPV6_STATE_BEGIN;

  for (; i < input.len; i++) {
    switch (state) {
      case IPV6_STATE_BEGIN:
        if (input.data[i] == '[') {
          state = IPV6_STATE_MIDDLE;
          out->host.data = input.data + i + 1;
        } else {
          return false;
        }
        break;
      case IPV6_STATE_MIDDLE:
        switch (input.data[i]) {
          // rn we dont care about the internal structure, will do later.
          case 'a' ... 'f':
          case 'A' ... 'F':
          case '0' ... '9':
          case ':':
          case '.':
            break;
          case ']':
            out->host.len = i - oi - 1;
            *pi = i + 1;
            return true;
          default:
            return false;
        }
    }
  }
  // must exit through ] branch
  // dont yet check length idk
  return false;
}

static bool parse_ipv4(slice_uint8_t input, size_t *pi, uri_parse_t *out) {
  // we're not yet going to do any checking about the internal structure.
  // just gonna digit and dot it
  size_t oi = *pi;
  size_t i = oi;

  // init
  out->host.data = input.data + oi;

LOOP:
  for (; i < input.len; i++) {
    switch (input.data[i]) {
      case '0' ... '9':
      case '.':
        break;
      default:
        break LOOP;
    }
  }
  if (i == oi) {
    // zero length is bad ipv4
    return false;
  }
  // otherwise good
  *pi = i;
  out->host.len = i - oi;
  return true;
}

static bool parse_host(slice_uint8_t input, size_t *i, uri_parse_t *out) {
  if (parse_ipv4(input, i, out)) {
    return true;
  }
  if (parse_ipv6(input, i, out)) {
    return true;
  }
  if (parse_hostname(input, i, out)) {
    return true;
  }
  return false;
}

static bool parse_port(slice_uint8_t input, size_t *pi, uri_parse_t *out) {
  uint16_t num = 0;
  size_t oi = *pi;
  size_t i = oi;

  out->port.data = input.data + oi;

LOOP:
  for (; i < input.len; i++) {
    uint8_t digit = 0;
    switch (input.data[i]) {
      case '0' ... '9':
        digit = input.data[i] - '0';
        break;
      default:
        // number ends

        break LOOP;
    }
    if (num > (UINT16_MAX - digit) / 10) {
      return false;
    }
    num = num * 10 + digit;
  }
  if (i == oi) {
    return false;
  }
  *pi = i;
  out->port.len = i - oi;
  return true;
}

static bool parse_authority(slice_uint8_t input, size_t *i, uri_parse_t *out) {
  size_t oi = *i;

  // must begin with //
  if (!(oi + 1 < input.len && input.data[oi] == '/' && input.data[oi + 1] == '/')) {
    *i = oi;
    return false;
  }
  *i += 2;

  if (!parse_host(input, i, out)) {
    *i = oi;
    return false;
  }
  if (*i < input.len && input.data[*i] == ':') {
    out->has_port = true;
    (*i)++;

    if (!parse_port(input, i, out)) {
      *i = oi;
      return false;
    }
  }
  return true;
}

static bool parse_scheme(slice_uint8_t input, size_t *pi, uri_parse_t *out) {
  size_t oi = *pi;
  size_t i = oi;

  out->scheme.data = input.data + oi;

  bool saw_colon = false;
  for (; i < input.len; i++) {
    if (saw_colon) {
      break;
    }
    switch (input.data[i]) {
      case 'a' ... 'z':
        break;
      case ':':
        saw_colon = true;
        break;
      default:
        return false;
    }
  }
  if (!saw_colon) {
    return false;
  }
  // otherwise good
  *pi = i;
  out->scheme.len = i - oi;
  return true;
}

// rest of the thing till we hit a space or newline idk
static bool parse_path(slice_uint8_t input, size_t *pi, uri_parse_t *out) {
  size_t oi = *pi;
  size_t i = oi;

  out->path.data = input.data + oi;

LOOP:
  for (; i < input.len; i++) {
    switch (input.data[i]) {
      case ' ':
      case '\t':
      case '\n':
        break LOOP;
      default:
        // we absorb everything else (for now)
        break;
    }
  }

  // path may be zero length
  *pi = i;
  out->path.len = i - oi;
  return true;
}

static bool parse_uri_internal(slice_uint8_t input, size_t *i, uri_parse_t *out) {
  size_t oi = *i;
  // may or may not have scheme
  if (parse_scheme(input, i, out)) {
    out->has_scheme = true;
  }

  // may or may not have authority
  if (parse_authority(input, i, out)) {
    out->has_authority = true;
  }

  if (!parse_path(input, i, out)) {
    *i = oi;
    return false;
  }

  return true;
}

bool parse_uri(slice_uint8_t input, uri_parse_t *out, bool absolute) {
  *out = (uri_parse_t){};
  size_t i = 0;
  bool success = parse_uri_internal(input, &i, out);
  if (absolute && !out->has_scheme) {
    return false;
  }
  return success;
}

bool encode_uri(slice_uint8_t *output, uri_parse_t parsed) {
  vec_uint8_t out;
  if (vec_uint8_t_init(&out) != VEC_ERR_OK) {
    return false;
  }
  defer vec_uint8_t_delete(&out);

  if (vec_uint8_t_pushv(&out, parsed.scheme.data, parsed.scheme.len) != VEC_ERR_OK) {
    return false;
  }
  if (parsed.has_authority) {
    if (vec_uint8_t_pushv(&out, (const uint8_t *)"//", 2) != VEC_ERR_OK) {
      return false;
    }
    if (vec_uint8_t_pushv(&out, parsed.host.data, parsed.host.len) != VEC_ERR_OK) {
      return false;
    }
    if (parsed.has_port) {
      uint8_t c = ':';
      if (vec_uint8_t_push(&out, &c) != VEC_ERR_OK) {
        return false;
      }
      if (vec_uint8_t_pushv(&out, parsed.port.data, parsed.port.len) != VEC_ERR_OK) {
        return false;
      }
    }
  }

  if (vec_uint8_t_pushv(&out, parsed.path.data, parsed.path.len) != VEC_ERR_OK) {
    return false;
  }
  // transfer ownership to the slice
  output->data = out.pData;
  output->len = out.len;
  out.pData = NULL;
  out.len = 0;
  return true;
}

// 5.2.4.  Remove Dot Segments

//    The pseudocode also refers to a "remove_dot_segments" routine for
//    interpreting and removing the special "." and ".." complete path
//    segments from a referenced path.  This is done after the path is
//    extracted from a reference, whether or not the path was relative, in
//    order to remove any invalid or extraneous dot-segments prior to
//    forming the target URI.  Although there are many ways to accomplish
//    this removal process, we describe a simple method using two string
//    buffers.

//    1.  The input buffer is initialized with the now-appended path
//        components and the output buffer is initialized to the empty
//        string.

//    2.  While the input buffer is not empty, loop as follows:

//        A.  If the input buffer begins with a prefix of "../" or "./",
//            then remove that prefix from the input buffer; otherwise,

//        B.  if the input buffer begins with a prefix of "/./" or "/.",
//            where "." is a complete path segment, then replace that
//            prefix with "/" in the input buffer; otherwise,

//        C.  if the input buffer begins with a prefix of "/../" or "/..",
//            where ".." is a complete path segment, then replace that
//            prefix with "/" in the input buffer and remove the last
//            segment and its preceding "/" (if any) from the output
//            buffer; otherwise,

//        D.  if the input buffer consists only of "." or "..", then remove
//            that from the input buffer; otherwise,

//        E.  move the first path segment in the input buffer to the end of
//            the output buffer, including the initial "/" character (if
//            any) and any subsequent characters up to, but not including,
//            the next "/" character or the end of the input buffer.

//    3.  Finally, the output buffer is returned as the result of
//        remove_dot_segments.

//    Note that dot-segments are intended for use in URI references to
//    express an identifier relative to the hierarchy of names in the base
//    URI.  The remove_dot_segments algorithm respects that hierarchy by
//    removing extra dot-segments rather than treat them as an error or
//    leaving them to be misinterpreted by dereference implementations.

//    The following illustrates how the above steps are applied for two
//    examples of merged paths, showing the state of the two buffers after
//    each step.

//       STEP   OUTPUT BUFFER         INPUT BUFFER

//        1 :                         /a/b/c/./../../g
//        2E:   /a                    /b/c/./../../g
//        2E:   /a/b                  /c/./../../g
//        2E:   /a/b/c                /./../../g
//        2B:   /a/b/c                /../../g
//        2C:   /a/b                  /../g
//        2C:   /a                    /g
//        2E:   /a/g

//       STEP   OUTPUT BUFFER         INPUT BUFFER

//        1 :                         mid/content=5/../6
//        2E:   mid                   /content=5/../6
//        2E:   mid/content=5         /../6
//        2C:   mid                   /6
//        2E:   mid/6

//    Some applications may find it more efficient to implement the
//    remove_dot_segments algorithm by using two segment stacks rather than
//    strings.

//       Note: Beware that some older, erroneous implementations will fail
//       to separate a reference's query component from its path component
//       prior to merging the base and reference paths, resulting in an
//       interoperability failure if the query component contains the
//       strings "/../" or "/./".

static void drop_segment(vec_uint8_t *o) {
  while (o->len > 0) {
    o->len--;
    if (o->pData[o->len] == '/') {
      break;
    }
  }
}

static bool normalize_uri_path(slice_uint8_t *in, slice_uint8_t *out) {
  vec_uint8_t o;
  vec_uint8_t_init_cap(&o, in->len);

  size_t i = 0;
  while (i < in->len) {
    size_t rem = in->len - i;
    uint8_t *p = in->data + i;
    // A
    if (rem >= 2 && memcmp(p, "./", 2) == 0) {
      i += 2;
    } else if (rem >= 3 && memcmp(p, "../", 3) == 0) {
      i += 3;
    }
    // B
    else if (rem >= 3 && memcmp(p, "/./", 3) == 0) {
      i += 2;
    } else if (rem == 2 && memcmp(p, "/.", 2) == 0) {
      vec_uint8_t_pushv(&o, (uint8_t *)"/", 1);
      break;
    }
    // C
    else if (rem >= 4 && memcmp(p, "/../", 4) == 0) {
      drop_segment(&o);
      i += 3;
    } else if (rem == 3 && memcmp(p, "/..", 3) == 0) {
      drop_segment(&o);
      vec_uint8_t_pushv(&o, (uint8_t *)"/", 1);
      break;
    }
    // D
    else if ((rem == 1 && memcmp(p, ".", 1) == 0) || (rem == 2 && memcmp(p, "..", 2) == 0)) {
      break;
    }
    // E
    else {
      // scan forward to determine how many to append
      size_t n = p[0] == '/' ? 1 : 0;
      while (n < rem && p[n] != '/') {
        n++;
      }
      vec_uint8_t_pushv(&o, p, n);
      i += n;
    }
  }
  *out = (slice_uint8_t){.data = o.pData, .len = o.len};
  return true;
}

static bool
resolve_path(slice_uint8_t *base, bool base_has_authority, slice_uint8_t *ref, slice_uint8_t *out) {
  if (ref->len == 0) {
    return slice_uint8_t_dup(base, out) == SLICE_ERR_OK;
  } else if (ref->data[0] == '/') {
    return normalize_uri_path(ref, out);
  } else {
    slice_uint8_t merged;
    if (base_has_authority && base->len == 0) {
      merged = joinpath_slice(slice_uint8_t_from_str("/"), *ref);
    } else {
      slice_uint8_t base_prefix = {};
      for (size_t i_r = base->len; i_r > 0; i_r--) {
        size_t i = base->len - i_r;
        if (base->data[i] == '/') {
          base_prefix = (slice_uint8_t){.data = base->data, .len = i + 1};
        }
      }
      merged = joinslice(base_prefix, *ref);
    }
    defer free(merged.data);
    return normalize_uri_path(&merged, out);
  }
}

bool resolve_uri_ref(uri_parse_t *base, uri_parse_t *ref, slice_uint8_t *output) {
  assert(base->has_scheme);

  uri_parse_t out;
  if (ref->has_scheme) {
    out.has_scheme = true;
    out.scheme = ref->scheme;
    out.has_authority = ref->has_authority;
    out.host = ref->host;
    out.has_port = ref->has_port;
    out.port = ref->port;
    normalize_uri_path(&ref->path, &out.path);
  } else if (ref->has_authority) {
    out.has_scheme = true;
    out.scheme = base->scheme;
    out.has_authority = ref->has_authority;
    out.host = ref->host;
    out.has_port = ref->has_port;
    out.port = ref->port;
    normalize_uri_path(&ref->path, &out.path);
  } else {
    out.has_scheme = true;
    out.scheme = base->scheme;
    out.has_authority = base->has_authority;
    out.host = base->host;
    out.has_port = base->has_port;
    out.port = base->port;
    if (!resolve_path(&base->path, base->has_authority, &ref->path, &out.path)) {
      return false;
    }
  }
  defer {
    free(out.path.data);
  }

  return encode_uri(output, out);
}
