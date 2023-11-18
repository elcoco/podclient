#ifndef XML_H
#define XML_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>


// Max data length that can be in a XMLItem to hold data like: strings, numbers or bool
// If too small, strings will be cut off. Streams will not become corrupted.
#define XML_MAX_DATA 256

// The stack holds XMLItems and represents the path from root to the currently parsed item
// eg: {object, key, array, string}
// Everytime the last object is done parsing, it is removed from the stack.
// When a new object is found, it is pushed onto the stack.
#define XML_MAX_STACK 15

// The buffer where, to be parsed chars, are copied to while iterating stream.
// A string/tag/cdata etc can not be bigger than the parse buffer.
// This is needed to not flood memory in low memory environments.
// If it IS bigger, the stream will skip until the closing char is found
// and the data will be ignored.
// While parsing the contents of this buffer are copied to XMLItem.data
// And can be cropped depending on XML_MAX_DATA.
#define XML_MAX_PARSE_BUFFER 1 * 1024

#define XML_MAX_ATTR_KEY 256
#define XML_MAX_ATTR_VALUE 256
#define XML_MAX_SKIP_STR 8

#define XML_CHAR_LESS_THAN      "&lt"
#define XML_CHAR_GREATER_THAN   "&gt"
#define XML_CHAR_AMPERSAND      "&amp"
#define XML_CHAR_APASTROPHE     "&apos"
#define XML_CHAR_QUOTE          "&quot"
#define XML_CHAR_COMMENT_START  "!--"
#define XML_CHAR_COMMENT_END    "-->"
#define XML_CHAR_CDATA_START    "![CDATA["
#define XML_CHAR_CDATA_END      "]]>"
#define XML_CHAR_HEADER_START   "?xml "

// How many chars to look behind/ahead the error in the parse string when displaying
// the error message.
#define XML_ERR_CHARS_CONTEXT 50
#define XRESET   "\x1B[0m"
#define XRED     "\x1B[31m"
#define XGREEN   "\x1B[32m"
#define XYELLOW  "\x1B[33m"
#define XBLUE    "\x1B[34m"
#define XMAGENTA "\x1B[35m"
#define XCYAN    "\x1B[36m"
#define XWHITE   "\x1B[37m"

enum XMLDtype {
    XML_DTYPE_UNKNOWN,
    XML_DTYPE_STRING,
    XML_DTYPE_TAG,
    XML_DTYPE_COMMENT
};

// Event is passed to callback when data is found
enum XMLEvent {
    XML_EV_HEADER,
    XML_EV_STRING,
    XML_EV_TAG_START,       // before signaling start event, the attributes must be parsed and sent with it
    XML_EV_TAG_END,
    XML_EV_COMMENT
};

enum XMLSearchResult {
    XML_SEARCH_SYNTAX_ERROR,       // eg. an unexpected tag. closing a tag that wasn't previously opened
    XML_SEARCH_END_OF_DATA,          // end of data (position), didn't find result in data
    XML_SEARCH_SUCCESS
};

enum XMLParseResult {
    XML_PARSE_ERROR,       // eg. an unexpected tag. closing a tag that wasn't previously opened
    XML_PARSE_INCOMPLETE,           // when eg closing quote is not found, this is common when streaming data
    XML_PARSE_SUCCESS
};
struct XMLPosition {
    int npos;      // char counter
    char *c;            // pointer to current char in xml string
    int length;
    char **chunks;
    size_t max_chunks;
    size_t cur_chunk;
};

struct XMLItem {
    enum XMLDtype dtype;
    char data[XML_MAX_DATA];
    char *param;
};

// the stuff in the opening tag eg: <book category="bla">
struct XMLAttr {
    char key[XML_MAX_ATTR_KEY];
    char value[XML_MAX_ATTR_VALUE];
};

struct XML {
    // call this callback everytime a new XMLItem is discovered
    void(*handle_data_cb)(struct XML *xml, enum XMLEvent ev, void *user_data);

    // traceback back to root item
    struct XMLItem stack[XML_MAX_STACK];

    // index of current position in stack
    int stack_pos;

    // pointer to userdata is passed to callback
    void *user_data;

    // If set, this look for this string before parsing anything.
    // This way we can stop storing super long strings that we can not store anyway
    // because of small memory on MCU
    char skip_until[XML_MAX_SKIP_STR];

    // how many times didn't we parse anything from given chunks
    // This is used to see when we should just look for skip_string.
    int zero_rd_cnt;
};


struct XML xml_init(void(*handle_data_cb)(struct XML *xml, enum XMLEvent ev, void *user_data));
void xml_handle_data_cb(struct XML *xml, enum XMLEvent ev, void *user_data);
size_t xml_parse(struct XML *xml, char **chunks, size_t nchunks);

struct XMLItem* xml_stack_get_from_end(struct XML *xml, int offset);

#endif
