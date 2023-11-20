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

#define PP_JSON_OBJECT_OPEN "{"
#define PP_JSON_OBJECT_CLOSE   "}"
#define PP_JSON_ARRAY_OPEN  "["
#define PP_JSON_ARRAY_CLOSE    "]"
#define PP_JSON_STRING_OPEN "\""
#define PP_JSON_STRING_CLOSE   "\""


struct PP pp_json_init(handle_data_cb data_cb);
void pp_json_handle_data_cb(struct PP *pp, enum PPDtype dtype, void *user_data);

#endif
