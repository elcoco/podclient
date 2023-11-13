#ifndef JSON_H
#define JSON_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

// Max data length that can be in a JSONItem to hold data like: strings, numbers or bool
// If too small, strings will be cut off. Streams will not become corrupted.
#define JSON_MAX_DATA 256

// Holds JSONItems and represents the path from root to the currently parsed item
// eg: {object, key, array, string}
// Everytime the last object is done parsing, it is removed from the stack.
// When a new object is found, it is pushed onto the stack.
#define JSON_MAX_STACK 8

// The buffer that holds the temporary data that is copied to the JSONItem while parsing
// If it is too small, the stream data will probably become corrupt
#define JSON_MAX_PARSE_BUFFER 1024

#define JSON_ERR_CHARS_CONTEXT 50

#define JRESET   "\x1B[0m"
#define JRED     "\x1B[31m"
#define JGREEN   "\x1B[32m"
#define JYELLOW  "\x1B[33m"
#define JBLUE    "\x1B[34m"
#define JMAGENTA "\x1B[35m"
#define JCYAN    "\x1B[36m"
#define JWHITE   "\x1B[37m"

#define ASSERTF(A, M, ...) if(!(A)) {ERROR(M, ##__VA_ARGS__); assert(A); }

enum JSONDtype {
    JSON_DTYPE_UNKNOWN,
    JSON_DTYPE_STRING,
    JSON_DTYPE_NUMBER,
    JSON_DTYPE_BOOL,
    JSON_DTYPE_OBJECT,
    JSON_DTYPE_ARRAY,
    JSON_DTYPE_KEY
};

static const char *dtype_map[] = {
    "UNKNOWN",
    "STRING",
    "NUMBER",
    "BOOL",
    "OBJECT",
    "ARRAY",
    "KEY"
};

// Event is passed to callback when data is found
enum JSONEvent {
    JSON_EV_KEY,
    JSON_EV_STRING,
    JSON_EV_NUMBER,
    JSON_EV_BOOL,
    JSON_EV_OBJECT_START,
    JSON_EV_OBJECT_END,
    JSON_EV_ARRAY_START,
    JSON_EV_ARRAY_END
};

enum JSONParseResult {
    JSON_PARSE_ILLEGAL_CHAR,
    JSON_PARSE_INCOMPLETE,      // when eg closing quote is not found, this is common when streaming data
    JSON_PARSE_END_OF_DATA,
    JSON_PARSE_SUCCESS_END_OF_DATA,     // data is successfully parsed but end of data is found
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
    void(*handle_data_cb)(struct JSON *json, enum JSONEvent ev, void *user_data);

    // traceback back to root item
    struct JSONItem stack[JSON_MAX_STACK];

    // index of current position in stack
    int stack_pos;

    // pointer to userdata is passed to callback
    void *user_data;
};


struct JSON json_init(void(*handle_data_cb)(struct JSON *json, enum JSONEvent ev, void *user_data));

// Pass in string and parse.
// When a JSONItem is found the handle_data_cb() callback is ran.
// If something is found, the amount of bytes read is returned.
// This is useful so we can continue reading when new data is available next time
//size_t json_parse(struct JSON *json, char *chunk_old, char *chunk_new);
size_t json_parse(struct JSON *json, char **chunks, size_t nchunks);

struct JSONItem* stack_get_from_end(struct JSON *json, int offset);

void json_handle_data_cb(struct JSON *json, enum JSONEvent ev, void *user_data);

int stack_item_is_type(struct JSON *json, int offset, enum JSONDtype dtype);

#endif
