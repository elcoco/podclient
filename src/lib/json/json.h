#ifndef JSON_H
#define JSON_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define JSON_MAX_DATA 256
#define JSON_MAX_STACK 16
#define JSON_MAX_BUF  20000

#define JSON_ERR_CHARS_CONTEXT 50

#define JRESET   "\x1B[0m"
#define JRED     "\x1B[31m"
#define JGREEN   "\x1B[32m"
#define JYELLOW  "\x1B[33m"
#define JBLUE    "\x1B[34m"
#define JMAGENTA "\x1B[35m"
#define JCYAN    "\x1B[36m"
#define JWHITE   "\x1B[37m"

enum JSONDtype {
    JSON_UNKNOWN,
    JSON_STRING,
    JSON_NUMBER,
    JSON_BOOL,
    JSON_OBJECT,
    JSON_ARRAY,
    JSON_KEY
};

enum JSONParseResult {
    JSON_PARSE_ERROR,
    JSON_PARSE_UNEXPECTED_CHAR,
    JSON_PARSE_ILLEGAL_CHAR,
    JSON_PARSE_END_OF_DATA,
    JSON_PARSE_SUCCESS
};

struct JSONItem {
    enum JSONDtype dtype;
    char data[JSON_MAX_DATA];
};

struct Position {
    int npos;      // char counter
    char *c;            // pointer to current char in json string
    int length;
    char **chunks;
    size_t max_chunks;
    size_t cur_chunk;
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
//size_t json_parse(struct JSON *json, char *chunk_old, char *chunk_new);
size_t json_parse(struct JSON *json, char **chunks, size_t nchunks);




#endif
