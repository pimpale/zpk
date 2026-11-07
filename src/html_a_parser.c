#include "html_a_parser.h"

LinkAccError link_acc(LinkAccContext *context, const uint8_t *buf, size_t len) {
  size_t i = 0;
  while (i < len) {
    if (context->failed) {
      break;
    }
    switch (context->state) {
      case LINK_ACC_HTML_TEXT:
        switch (buf[i]) {
          case '<':
            context->state = LINK_ACC_HTML_AFTER_LT;
            break;
          default:
            break;
        }
        i++;
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
          default:
            context->state = LINK_ACC_HTML_SKIP_TAG;
            break;
        }
        i++;
        break;
      case LINK_ACC_HTML_DECL_SAW_BANG:
        switch (buf[i]) {
          case '-':
            context->state = LINK_ACC_HTML_DECL_SAW_BANG_DASH;
            break;
          default:
            context->state = LINK_ACC_HTML_SKIP_TAG;
            break;
        }
        i++;
        break;
      case LINK_ACC_HTML_DECL_SAW_BANG_DASH:
        switch (buf[i]) {
          case '-':
            context->state = LINK_ACC_HTML_IN_COMMENT;
            break;
          default:
            context->state = LINK_ACC_HTML_SKIP_TAG;
            break;
        }
        i++;
        break;

      case LINK_ACC_HTML_IN_COMMENT:
        switch (buf[i]) {
          case '-':
            context->state = LINK_ACC_HTML_COMMENT_SAW_DASH;
            break;
          default:
            break;
        }
        i++;
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
        i++;
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
        i++;
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
        i++;
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
        i++;
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
        i++;
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
        i++;
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
          default:
            context->state = LINK_ACC_HTML_A_SKIP_ATTR_NAME;
            break;
        }
        i++;
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
          default:
            context->state = LINK_ACC_HTML_A_SKIP_ATTR_NAME;
            break;
        }
        i++;
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
          default:
            context->state = LINK_ACC_HTML_A_SKIP_ATTR_NAME;
            break;
        }
        i++;
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
          default:
            context->state = LINK_ACC_HTML_A_SKIP_ATTR_NAME;
            break;
        }
        i++;
        break;
      case LINK_ACC_HTML_A_ATTR_NAME_SAW_HREF:
        switch (buf[i]) {
          case ' ':
          case '\n':
          case '\r':
          case '\t':
            context->state = LINK_ACC_HTML_A_AFTER_HREF_NAME;
            break;
          default:
            context->state = LINK_ACC_HTML_A_SKIP_ATTR_NAME;
            break;
        }
        i++;
        break;
      case LINK_ACC_HTML_A_SKIP_ATTR_NAME:
        switch (buf[i]) {
          case ' ':
          case '\n':
          case '\r':
          case '\t':
            context->state = LINK_ACC_HTML_A_AFTER_OTHER_ATTR_NAME;
            break;
          default:
            context->state = LINK_ACC_HTML_A_SKIP_ATTR_NAME;
            break;
        }
        i++;
        break;
      case LINK_ACC_HTML_A_AFTER_HREF_NAME:
      case LINK_ACC_HTML_A_BEFORE_HREF_VALUE:
      case LINK_ACC_HTML_A_IN_HREF_SINGLE_QUOTED:
      case LINK_ACC_HTML_A_IN_HREF_DOUBLE_QUOTED:
      case LINK_ACC_HTML_A_IN_HREF_UNQUOTED:
      case LINK_ACC_HTML_A_AFTER_OTHER_ATTR_NAME:
      case LINK_ACC_HTML_A_BEFORE_OTHER_ATTR_VALUE:
      case LINK_ACC_HTML_A_SKIP_VALUE_SINGLE_QUOTED:
      case LINK_ACC_HTML_A_SKIP_VALUE_DOUBLE_QUOTED:
      case LINK_ACC_HTML_A_SKIP_VALUE_UNQUOTED:
        break;
    }
  }
}
