#ifndef POTATO_JSON_H
#define POTATO_JSON_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "potato_parser.h"


extern int do_debug;
extern int do_info;
extern int do_error;

#define PP_JSON_CHAR_OBJECT_START "{"
#define PP_JSON_CHAR_OBJECT_END   "}"
#define PP_JSON_CHAR_ARRAY_START  "["
#define PP_JSON_CHAR_ARRAY_END    "]"
#define PP_JSON_CHAR_STRING_START "\""
#define PP_JSON_CHAR_STRING_END   "\""
#define PP_JSON_CHAR_STRING2_START "'"
#define PP_JSON_CHAR_STRING2_END   "'"
#define PP_JSON_CHAR_VALUE         ":"


struct PP pp_json_init(handle_data_cb data_cb);
void pp_json_handle_data_cb(struct PP *pp, enum PPDtype dtype, void *user_data);

#endif
