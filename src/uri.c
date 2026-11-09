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

LOOP:
  for (; i < input.len; i++) {
    switch (input.data[i]) {
      case 'a' ... 'z':
        break;
      case ':':
        i++;
        // : terminates every scheme
        break LOOP;
      default:
        return false;
    }
  }
  if (i == oi || i == oi + 1) {
    // 0 or 1 length is bad scheme
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

bool resolve_path(slice_uint8_t* out, slice_uint8_t* ref, slice_uint8_t* in) {
  // we do not handle .. or . paths yet
  slice_uint8_t ref_prefix = {};
  for(size_t i_r = ref->len; i_r > 0; i_r--) {
    size_t i = ref->len-i_r;
    if(ref->data[i] == '/') {
      ref_prefix = (slice_uint8_t){
        .data=ref->data,
        .len=i
      };
    }
  }
  if(ref_prefix.data == NULL) {
    return false;
  }
  // join 
  *out = joinpath_slice(ref_prefix, *in);
  return true;
}

bool resolve_uri_ref(uri_parse_t *ref, uri_parse_t *in, slice_uint8_t *output) {
  assert(ref->has_scheme);
  
  bool allocated_path = false;
  
  uri_parse_t out;
  if (in->has_scheme) {
    // wholesale copy the entire thing
    out = *in;
  } else if (in->has_authority) {
    out.has_scheme = true;
    out.scheme = ref->scheme;
    out.has_authority = in->has_authority;
    out.host = in->host;
    out.has_port = in->has_port;
    out.port = in->port;
    out.path = in->path;
  } else {
    out.has_scheme = true;
    out.scheme = ref->scheme;
    out.has_authority = ref->has_authority;
    out.host = ref->host;
    out.has_port = ref->has_port;
    out.port = ref->port;
    // absolute path
    if (in->path.len > 0 && in->path.data[0] == '/') {
      out.path = in->path;
    } else {
      if (!resolve_path(&out.path, &ref->path, &in->path)) {
        return false;
      }
      allocated_path=true;
    }
  }
  defer {
    if(allocated_path) {
      free(out.path.data);
    }
  }

  return encode_uri(output, out);
}
