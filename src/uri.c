#include "uri.h"
#include "instances/slice_uint8_t.h"
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
            *pi = i+1;
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
  uint16_t *num = &out->port;
  *num = 0;
  size_t oi = *pi;
  size_t i = oi;
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
    if (*num > (UINT16_MAX - digit) / 10) {
      return false;
    }
    *num = *num * 10 + digit;
  }
  if (i == oi) {
    return false;
  }
  *pi = i;
  return true;
}

static bool parse_authority(slice_uint8_t input, size_t *i, uri_parse_t *out) {
  size_t oi = *i;

  // must begin with //
  if (!(oi + 1 < input.len && input.data[oi] == '/' && input.data[oi + 1] == '/')) {
    *i = oi;
    return false;
  }
  *i+=2;

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
      default:
        break LOOP;
    }
  }
  if (i == oi) {
    // zero length is bad scheme
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

static bool parse_uri(slice_uint8_t input, size_t *i, uri_parse_t *out) {
  size_t oi = *i;
  if (!parse_scheme(input, i, out)) {
    *i = oi;
    return false;
  }

  // must contain colon
  if (!(*i < input.len && input.data[*i] == ':')) {
    *i = oi;
    return false;
  }
  *i+=1;

  // must have authority
  if (!parse_authority(input, i, out)) {
    *i = oi;
    return false;
  }

  if (!parse_path(input, i, out)) {
    *i = oi;
    return false;
  }

  return true;
}

bool decode_uri(slice_uint8_t input, uri_parse_t *out) {
  *out = (uri_parse_t){0};
  size_t i = 0;
  return parse_uri(input, &i, out);
}
