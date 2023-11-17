#ifndef POTATO_XML_H
#define POTATO_XML_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "potato_parser.h"

#define PP_XML_CHAR_LESS_THAN      "&lt"
#define PP_XML_CHAR_GREATER_THAN   "&gt"
#define PP_XML_CHAR_AMPERSAND      "&amp"
#define PP_XML_CHAR_APASTROPHE     "&apos"
#define PP_XML_CHAR_QUOTE          "&quot"
#define PP_XML_CHAR_COMMENT_START  "<!--"
#define PP_XML_CHAR_COMMENT_END    "-->"
#define PP_XML_CHAR_CDATA_START    "<![CDATA["
#define PP_XML_CHAR_CDATA_END      "]]>"
#define PP_XML_CHAR_HEADER_START   "<?xml"
#define PP_XML_CHAR_HEADER_END     "?>"
#define PP_XML_CHAR_TAG_OPEN_START  "<"
#define PP_XML_CHAR_TAG_OPEN_END    ">"
#define PP_XML_CHAR_TAG_CLOSE_START "</"
#define PP_XML_CHAR_TAG_CLOSE_END   ">"
#define PP_XML_CHAR_TAG_SIGNLE_LINE_CLOSE_END   "/>"

#define PP_MAX_ATTR_KEY       256
#define PP_MAX_ATTR_VALUE     256


// the stuff in the opening tag eg: <book category="bla">
// TODO, not implemented yet
struct PPAttr {
    char key[PP_MAX_ATTR_KEY];
    char value[PP_MAX_ATTR_VALUE];
};


struct PP pp_xml_init(handle_data_cb data_cb);
void pp_xml_handle_data_cb(struct PP *pp, enum PPDtype dtype, void *user_data);

#endif
