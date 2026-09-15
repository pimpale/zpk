#include "uri.h"
#include "instances/slice_uint8_t.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  URI_SCHEME,
  URI_COLON_SLASH_SLASH_AWAIT_SLASH_SLASH,
  URI_COLON_SLASH_SLASH_AWAIT_SLASH,
  URI_HOST_OR_PATH,
  URI_HOST,
  URI_PORT,
  URI_PATH
} UriDecodeState;

bool decode_uri(slice_uint8_t input, Uri *out) {
  *out = (Uri){0};

  UriDecodeState state = URI_SCHEME;

  out->scheme.data = input.data;

  for (size_t i = 0; i < input.len; i++) {
    switch (state) {
      case URI_SCHEME:
        switch (input.data[i]) {
          case 'a' ... 'z':
            break;
          case ':':
            state = URI_COLON_SLASH_SLASH_AWAIT_SLASH_SLASH;
            out->scheme.len = (size_t)((input.data + i) - out->scheme.data);
            break;
          default:
            return false;
        }
        break;
      case URI_COLON_SLASH_SLASH_AWAIT_SLASH_SLASH:
        switch (input.data[i]) {
          case '/':
            state = URI_COLON_SLASH_SLASH_AWAIT_SLASH;
            break;
          default:
            return false;
        }
        break;
      case URI_COLON_SLASH_SLASH_AWAIT_SLASH:
        switch (input.data[i]) {
          case '/':
            state = URI_HOST_OR_PATH;
            break;
          default:
            return false;
        }
        break;
      case URI_HOST_OR_PATH:
        switch (input.data[i]) {
          case '/':
            state = URI_PATH;
            out->path.data = input.data + i;
            break;
          case 'a' ... 'z':
          case '0' ... '9':
          case '-':
          case '.':
            state = URI_HOST;
            out->host.data = input.data + i;
            break;
          default:
            return false;
        }
        break;
      case URI_HOST:
      switch(input.data[i]) {
        case '/':
          state = URI_PATH;
          out
         
      case 'a' ... 'z':
          case '0' ... '9':
          case '-':
          case '.':
                case URI_PORT:
      case URI_PATH:
        break;
    }
  }
  return true;
}

// file:///blah
// http://example.com/path
