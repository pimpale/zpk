#ifndef html_a_parser_INCLUDED
#define html_a_parser_INCLUDED

#include "instances/vec_uint8_t.h"

typedef enum {
  // in the middle of a comment
  LINK_ACC_HTML_COMMENT,
  // awaiting the -> that will end the comment
  LINK_ACC_HTML_COMMENTEND_AWAIT_DASH_RANGLE,
  // awaiting the > that will end the comment
  LINK_ACC_HTML_COMMENTEND_AWAIT_RANGLE,
  LINK_ACC_HTML_SAW_LANGLE,
  LINK_ACC_HTML_COMMENTSTART_AWAIT_DASH_DASH,
  LINK_ACC_HTML_COMMENTSTART_AWAIT_DASH,
  LINK_ACC_HTML_AWAIT_TAGEND,
  LINK_ACC_HTML_AWAIT_

} LinkAccState;

typedef struct {
  vec_uint8_t tag;
  vec_uint8_t attribute;

} LinkAccumulatorContext;


#endif
