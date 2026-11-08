#include "html_a_parser.h"
#include "instances/slice_uint8_t.h"

static LinkAccError push_link(LinkAccContext *context) {
  slice_uint8_t slice = {
    .data = context->linkbuf.pData,
    .len = context->linkbuf.len,
  };
  if (vec_slice_uint8_t_push(context->links, &slice) != VEC_ERR_OK) {
    return LINK_ACC_ERR_NOMEM;
  }
  if (vec_uint8_t_init(&context->linkbuf)) {
    return LINK_ACC_ERR_NOMEM;
  }
  return LINK_ACC_ERR_OK;
}

LinkAccError link_acc(LinkAccContext *context, const uint8_t *buf, size_t len) {
  if (context->failed) {
    return LINK_ACC_ERR_NOMEM;
  }
  for (size_t i = 0; i < len; i++) {
    switch (context->state) {
      case LINK_ACC_HTML_TEXT:
        switch (buf[i]) {
          case '<':
            context->state = LINK_ACC_HTML_AFTER_LT;
            break;
          default:
            break;
        }
        break;
      case LINK_ACC_HTML_AFTER_LT:
        switch (buf[i]) {
          case '/':
            context->state = LINK_ACC_HTML_SKIP_TAG;
            break;
          case '!':
            context->state = LINK_ACC_HTML_DECL_SAW_BANG;
            break;
          case 'a':
            context->state = LINK_ACC_HTML_TAG_NAME_SAW_A;
            break;
          case '>':
            context->state = LINK_ACC_HTML_TEXT;
            break;
          default:
            context->state = LINK_ACC_HTML_SKIP_TAG;
            break;
        }
        break;
      case LINK_ACC_HTML_DECL_SAW_BANG:
        switch (buf[i]) {
          case '-':
            context->state = LINK_ACC_HTML_DECL_SAW_BANG_DASH;
            break;
          case '>':
            context->state = LINK_ACC_HTML_TEXT;
            break;
          default:
            context->state = LINK_ACC_HTML_SKIP_TAG;
            break;
        }
        break;
      case LINK_ACC_HTML_DECL_SAW_BANG_DASH:
        switch (buf[i]) {
          case '-':
            context->state = LINK_ACC_HTML_IN_COMMENT;
            break;
          case '>':
            context->state = LINK_ACC_HTML_TEXT;
            break;
          default:
            context->state = LINK_ACC_HTML_SKIP_TAG;
            break;
        }
        break;

      case LINK_ACC_HTML_IN_COMMENT:
        switch (buf[i]) {
          case '-':
            context->state = LINK_ACC_HTML_COMMENT_SAW_DASH;
            break;
          default:
            break;
        }
        break;
      case LINK_ACC_HTML_COMMENT_SAW_DASH:
        switch (buf[i]) {
          case '-':
            context->state = LINK_ACC_HTML_COMMENT_SAW_DASH_DASH;
            break;
          default:
            context->state = LINK_ACC_HTML_IN_COMMENT;
            break;
        }
        break;
      case LINK_ACC_HTML_COMMENT_SAW_DASH_DASH:
        switch (buf[i]) {
          case '>':
            context->state = LINK_ACC_HTML_TEXT;
            break;
          case '-':
            context->state = LINK_ACC_HTML_COMMENT_SAW_DASH_DASH;
            break;
          default:
            context->state = LINK_ACC_HTML_IN_COMMENT;
            break;
        }
        break;
      case LINK_ACC_HTML_SKIP_TAG:
        switch (buf[i]) {
          case '>':
            context->state = LINK_ACC_HTML_TEXT;
            break;
          case '\'':
            context->state = LINK_ACC_HTML_SKIP_TAG_SINGLE_QUOTED;
            break;
          case '"':
            context->state = LINK_ACC_HTML_SKIP_TAG_DOUBLE_QUOTED;
            break;
          default:
            context->state = LINK_ACC_HTML_SKIP_TAG;
            break;
        }
        break;

      case LINK_ACC_HTML_SKIP_TAG_SINGLE_QUOTED:
        switch (buf[i]) {
          case '\'':
            context->state = LINK_ACC_HTML_SKIP_TAG;
            break;
          default:
            context->state = LINK_ACC_HTML_SKIP_TAG_SINGLE_QUOTED;
            break;
        }
        break;
      case LINK_ACC_HTML_SKIP_TAG_DOUBLE_QUOTED:
        switch (buf[i]) {
          case '"':
            context->state = LINK_ACC_HTML_SKIP_TAG;
            break;
          default:
            context->state = LINK_ACC_HTML_SKIP_TAG_DOUBLE_QUOTED;
            break;
        }
        break;
      case LINK_ACC_HTML_TAG_NAME_SAW_A:
        switch (buf[i]) {
          case ' ':
          case '\n':
          case '\r':
          case '\t':
            context->state = LINK_ACC_HTML_A_BEFORE_ATTR_NAME;
            break;
          case '>':
            context->state = LINK_ACC_HTML_TEXT;
            break;
          default:
            context->state = LINK_ACC_HTML_SKIP_TAG;
            break;
        }
        break;
      case LINK_ACC_HTML_A_BEFORE_ATTR_NAME:
        switch (buf[i]) {
          case ' ':
          case '\n':
          case '\r':
          case '\t':
            context->state = LINK_ACC_HTML_A_BEFORE_ATTR_NAME;
            break;
          case 'h':
            context->state = LINK_ACC_HTML_A_ATTR_NAME_SAW_H;
            break;
          case '=':
            context->state = LINK_ACC_HTML_A_BEFORE_OTHER_ATTR_VALUE;
            break;
          case '>':
            context->state = LINK_ACC_HTML_TEXT;
            break;
          default:
            context->state = LINK_ACC_HTML_A_SKIP_ATTR_NAME;
            break;
        }
        break;
      case LINK_ACC_HTML_A_ATTR_NAME_SAW_H:
        switch (buf[i]) {
          case ' ':
          case '\n':
          case '\r':
          case '\t':
            context->state = LINK_ACC_HTML_A_AFTER_OTHER_ATTR_NAME;
            break;
          case 'r':
            context->state = LINK_ACC_HTML_A_ATTR_NAME_SAW_HR;
            break;
          case '=':
            context->state = LINK_ACC_HTML_A_BEFORE_OTHER_ATTR_VALUE;
            break;
          case '>':
            context->state = LINK_ACC_HTML_TEXT;
            break;
          default:
            context->state = LINK_ACC_HTML_A_SKIP_ATTR_NAME;
            break;
        }
        break;
      case LINK_ACC_HTML_A_ATTR_NAME_SAW_HR:
        switch (buf[i]) {
          case ' ':
          case '\n':
          case '\r':
          case '\t':
            context->state = LINK_ACC_HTML_A_AFTER_OTHER_ATTR_NAME;
            break;
          case 'e':
            context->state = LINK_ACC_HTML_A_ATTR_NAME_SAW_HRE;
            break;
          case '=':
            context->state = LINK_ACC_HTML_A_BEFORE_OTHER_ATTR_VALUE;
            break;
          case '>':
            context->state = LINK_ACC_HTML_TEXT;
            break;
          default:
            context->state = LINK_ACC_HTML_A_SKIP_ATTR_NAME;
            break;
        }
        break;
      case LINK_ACC_HTML_A_ATTR_NAME_SAW_HRE:
        switch (buf[i]) {
          case ' ':
          case '\n':
          case '\r':
          case '\t':
            context->state = LINK_ACC_HTML_A_AFTER_OTHER_ATTR_NAME;
            break;
          case 'f':
            context->state = LINK_ACC_HTML_A_ATTR_NAME_SAW_HREF;
            break;
          case '=':
            context->state = LINK_ACC_HTML_A_BEFORE_OTHER_ATTR_VALUE;
            break;
          case '>':
            context->state = LINK_ACC_HTML_TEXT;
            break;
          default:
            context->state = LINK_ACC_HTML_A_SKIP_ATTR_NAME;
            break;
        }
        break;
      case LINK_ACC_HTML_A_ATTR_NAME_SAW_HREF:
        switch (buf[i]) {
          case ' ':
          case '\n':
          case '\r':
          case '\t':
            context->state = LINK_ACC_HTML_A_AFTER_HREF_NAME;
            break;
          case '=':
            context->state = LINK_ACC_HTML_A_BEFORE_HREF_VALUE;
            break;
          case '>':
            context->state = LINK_ACC_HTML_TEXT;
            break;
          default:
            context->state = LINK_ACC_HTML_A_SKIP_ATTR_NAME;
            break;
        }
        break;
      case LINK_ACC_HTML_A_SKIP_ATTR_NAME:
        switch (buf[i]) {
          case ' ':
          case '\n':
          case '\r':
          case '\t':
            context->state = LINK_ACC_HTML_A_AFTER_OTHER_ATTR_NAME;
            break;
          case '=':
            context->state = LINK_ACC_HTML_A_BEFORE_OTHER_ATTR_VALUE;
            break;
          case '>':
            context->state = LINK_ACC_HTML_TEXT;
            break;
          default:
            context->state = LINK_ACC_HTML_A_SKIP_ATTR_NAME;
            break;
        }
        break;
      case LINK_ACC_HTML_A_AFTER_HREF_NAME:
        switch (buf[i]) {
          case ' ':
          case '\n':
          case '\r':
          case '\t':
            context->state = LINK_ACC_HTML_A_AFTER_HREF_NAME;
            break;
          case '=':
            context->state = LINK_ACC_HTML_A_BEFORE_HREF_VALUE;
            break;
          case '>':
            context->state = LINK_ACC_HTML_TEXT;
            break;
          case 'h':
            context->state = LINK_ACC_HTML_A_ATTR_NAME_SAW_H;
            break;
          default:
            context->state = LINK_ACC_HTML_A_SKIP_ATTR_NAME;
            break;
        }
        break;
      case LINK_ACC_HTML_A_BEFORE_HREF_VALUE:
        switch (buf[i]) {
          case ' ':
          case '\n':
          case '\r':
          case '\t':
            context->state = LINK_ACC_HTML_A_BEFORE_HREF_VALUE;
            break;
          case '\'':
            context->state = LINK_ACC_HTML_A_IN_HREF_SINGLE_QUOTED;
            break;
          case '"':
            context->state = LINK_ACC_HTML_A_IN_HREF_DOUBLE_QUOTED;
            break;
          case '>':
            context->state = LINK_ACC_HTML_TEXT;
            break;
          default:
            context->state = LINK_ACC_HTML_A_SKIP_VALUE_UNQUOTED;
            break;
        }
        break;
      case LINK_ACC_HTML_A_IN_HREF_SINGLE_QUOTED:
        switch (buf[i]) {
          case '\'':
            if (push_link(context) != LINK_ACC_ERR_OK) {
              context->failed = true;
              return LINK_ACC_ERR_NOMEM;
            }
            context->state = LINK_ACC_HTML_A_BEFORE_ATTR_NAME;
            break;
          default:
            if (vec_uint8_t_push(&context->linkbuf, buf + i) != VEC_ERR_OK) {
              context->failed = true;
              return LINK_ACC_ERR_NOMEM;
            }
            context->state = LINK_ACC_HTML_A_IN_HREF_SINGLE_QUOTED;
            break;
        }
        break;
      case LINK_ACC_HTML_A_IN_HREF_DOUBLE_QUOTED:
        switch (buf[i]) {
          case '"':
            if (push_link(context) != LINK_ACC_ERR_OK) {
              context->failed = true;
              return LINK_ACC_ERR_NOMEM;
            }
            context->state = LINK_ACC_HTML_A_BEFORE_ATTR_NAME;
            break;
          default:
            if (vec_uint8_t_push(&context->linkbuf, buf + i) != VEC_ERR_OK) {
              context->failed = true;
              return LINK_ACC_ERR_NOMEM;
            }
            context->state = LINK_ACC_HTML_A_IN_HREF_DOUBLE_QUOTED;
            break;
        }
        break;
      case LINK_ACC_HTML_A_AFTER_OTHER_ATTR_NAME:
        switch (buf[i]) {
          case ' ':
          case '\n':
          case '\r':
          case '\t':
            context->state = LINK_ACC_HTML_A_AFTER_OTHER_ATTR_NAME;
            break;
          case '=':
            context->state = LINK_ACC_HTML_A_BEFORE_OTHER_ATTR_VALUE;
            break;
          case '>':
            context->state = LINK_ACC_HTML_TEXT;
            break;
          case 'h':
            context->state = LINK_ACC_HTML_A_ATTR_NAME_SAW_H;
            break;
          default:
            context->state = LINK_ACC_HTML_A_SKIP_ATTR_NAME;
            break;
        }
        break;
      case LINK_ACC_HTML_A_BEFORE_OTHER_ATTR_VALUE:
        switch (buf[i]) {
          case ' ':
          case '\n':
          case '\r':
          case '\t':
            context->state = LINK_ACC_HTML_A_BEFORE_OTHER_ATTR_VALUE;
            break;
          case '\'':
            context->state = LINK_ACC_HTML_A_SKIP_VALUE_SINGLE_QUOTED;
            break;
          case '"':
            context->state = LINK_ACC_HTML_A_SKIP_VALUE_DOUBLE_QUOTED;
            break;
          case '>':
            context->state = LINK_ACC_HTML_TEXT;
            break;
          default:
            context->state = LINK_ACC_HTML_A_SKIP_VALUE_UNQUOTED;
            break;
        }
        break;
      case LINK_ACC_HTML_A_SKIP_VALUE_SINGLE_QUOTED:
        switch (buf[i]) {
          case '\'':
            context->state = LINK_ACC_HTML_A_BEFORE_ATTR_NAME;
            break;
          default:
            context->state = LINK_ACC_HTML_A_SKIP_VALUE_SINGLE_QUOTED;
            break;
        }
        break;
      case LINK_ACC_HTML_A_SKIP_VALUE_DOUBLE_QUOTED:
        switch (buf[i]) {
          case '"':
            context->state = LINK_ACC_HTML_A_BEFORE_ATTR_NAME;
            break;
          default:
            context->state = LINK_ACC_HTML_A_SKIP_VALUE_DOUBLE_QUOTED;
            break;
        }
        break;
      case LINK_ACC_HTML_A_SKIP_VALUE_UNQUOTED:
        switch (buf[i]) {
          case ' ':
          case '\n':
          case '\r':
          case '\t':
            context->state = LINK_ACC_HTML_A_BEFORE_ATTR_NAME;
            break;
          case '>':
            context->state = LINK_ACC_HTML_TEXT;
            break;
          default:
            context->state = LINK_ACC_HTML_A_SKIP_VALUE_UNQUOTED;
            break;
        }
        break;
    }
  }
  return LINK_ACC_ERR_OK;
}
