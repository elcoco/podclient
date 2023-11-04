#ifndef JSON_H
#define JSON_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define JSON_MAX_DATA 256
#define JSON_MAX_STACK 16

enum JSONDtype {
    JSON_UNKNOWN,
    JSON_STRING,
    JSON_NUMBER,
    JSON_BOOL,
    JSON_OBJECT,
    JSON_ARRAY,
    JSON_KEY
};

struct JSONItem {
    enum JSONDtype dtype;
    char data[JSON_MAX_DATA];
};

struct Position {
    int npos;      // char counter
    char *c;            // pointer to current char in json string
};

struct JSON {
    // call this callback everytime a new JSONItem is discovered
    void(*handle_data_cb)(struct JSON *json, struct JSONItem *ji);

    // traceback back to root item
    struct JSONItem stack[JSON_MAX_STACK];

    // index of current position in stack
    int stack_pos;
    struct JSONItem *stack_ptr;
};


struct JSON json_init(void(*handle_data_cb)(struct JSON *json, struct JSONItem *ji));

// Pass in string and parse.
// When a JSONItem is found the handle_data_cb() callback is ran.
// If something is found, the amount of bytes read is returned.
// This is useful so we can continue reading when new data is available next time
size_t json_parse_str(struct JSON *json, char *data);




#endif
