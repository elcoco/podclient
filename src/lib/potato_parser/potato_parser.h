#ifndef POTATO_PARSER_H
#define POTATO_PARSER_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>


// NOTE If PP_MAX_DATA is too small, the tag is cut off when putting the data in a PPItem.
//      this MAY cause the string to split on '/', which with XML indicates that the tag should be closed.
//      This will trigger an error later when the actual closing tag appears.
//      Super corner case but this actually did happen once with the RSS tag!
// Max data length that can be in a XMLItem to hold data like: strings, numbers or bool
// If too small, strings will be cut off. Streams will not become corrupted.
#define PP_MAX_DATA 512

// The stack holds XMLItems and represents the path from root to the currently parsed item
// eg: {object, key, array, string}
// Everytime the last object is done parsing, it is removed from the stack.
// When a new object is found, it is pushed onto the stack.
#define PP_MAX_STACK 15

// The buffer where, to be parsed chars, are copied to while iterating stream.
// A string/tag/cdata etc can not be bigger than the parse buffer.
// This is needed to not flood memory in low memory environments.
// If it IS bigger, the stream will skip until the closing char is found
// and the data will be ignored.
// While parsing the contents of this buffer are copied to XMLItem.data
// And can be cropped depending on XML_MAX_DATA.
#define PP_MAX_PARSE_BUFFER 1024

#define PP_MAX_SKIP_STR         8
#define PP_MAX_PARSER_ENTRIES  32

// don't save these chars when looking for strings
#define PP_STR_SEARCH_IGNORE_CHARS "\r\t\n"
#define PP_STR_SEARCH_IGNORE_LEADING "\r\t "

// buffer that holds string while searching for substring.
// Must be at least as big as the search string
#define PP_MAX_SEARCH_BUF 32+1

#define PP_MAX_SEARCH_IGNORE_CHARS 10

#define PP_BUFFER_OVERFLOW_PLACEHOLDER "BUFFER_OVERFLOW!!!"

// How many chars to look behind/ahead the error in the parse string when displaying
// the error message.
#define PP_ERR_CHARS_CONTEXT 50
#define XRESET   "\x1B[0m"
#define XRED     "\x1B[31m"
#define XGREEN   "\x1B[32m"
#define XYELLOW  "\x1B[33m"
#define XBLUE    "\x1B[34m"
#define XMAGENTA "\x1B[35m"
#define XCYAN    "\x1B[36m"
#define XWHITE   "\x1B[37m"

enum PPSearchResult {
    PP_SEARCH_RESULT_SYNTAX_ERROR,       // eg. an unexpected tag. closing a tag that wasn't previously opened
    PP_SEARCH_RESULT_END_OF_DATA,          // end of data (position), didn't find result in data
    PP_SEARCH_RESULT_SUCCESS
};

enum PPParseResult {
    PP_PARSE_RESULT_ERROR,       // eg. an unexpected tag. closing a tag that wasn't previously opened
    PP_PARSE_RESULT_INCOMPLETE,           // when eg closing quote is not found, this is common when streaming data
    PP_PARSE_RESULT_NO_MATCH,
    PP_PARSE_RESULT_SUCCESS
};

/* Consume PPParserEntry.end char or not? */
enum PPParseMethod {
    PP_METHOD_NON_GREEDY,
    PP_METHOD_GREEDY
};

struct PPPosition {
    int npos;           // char counter
    char *c;            // pointer to current char in chunk
    int length;         // length of current chunk
    char **chunks;
    size_t max_chunks;
    size_t cur_chunk;   // index of current chunk
};

/* Datatype if PPItem and PPParserEntry */
enum PPDtype {
    PP_DTYPE_UNKNOWN,
    PP_DTYPE_STRING,
    PP_DTYPE_CDATA,
    PP_DTYPE_TAG_OPEN,
    PP_DTYPE_TAG_CLOSE,
    PP_DTYPE_COMMENT,
    PP_DTYPE_HEADER
};

/* Holds the actual data, eg: tag, string, object etc... */
struct PPItem {
    enum PPDtype dtype;
    char data[PP_MAX_DATA];
    char *param;
};

struct PPStack {
    struct PPItem stack[PP_MAX_STACK];
    int pos;
};

struct PP;

/* Defines a parser term that will eventually create a PPItem */
struct PPParserEntry {
    const char *start;
    const char *end;
    enum PPDtype dtype;

    // callback handles sanity check, and adding/removing from stack
    enum PPParseResult(*cb)(struct PP *pp, struct PPParserEntry *pe, struct PPItem *item);

    enum PPParseMethod greedy;
};

struct PP {
    struct PPPosition pos;
    struct PPStack stack;
    struct PPParserEntry entries[PP_MAX_PARSER_ENTRIES];

    void(*handle_data_cb)(struct PP *pp, enum PPDtype dtype, void *user_data);
    void *user_data;

    int max_entries;

    // When a bufferoverflow occurs, instead of creating a larger buffer we save the entry
    // that defines to which string we should skip.
    // Then keep on looking for this string and add a placeholder PPItem to the stack
    // It is a potato parser after all ;)
    struct PPParserEntry skip;
    // indicate that kip has an intentional value
    unsigned char skip_is_set;
    // How many times we returned zero chars parsed, this indicates wether we should look for the above skip PPParserEntry
    int zero_rd_cnt;
};

typedef enum PPParseResult(*entry_cb)(struct PP *pp, struct PPParserEntry *pe, struct PPItem *item);
typedef void(*handle_data_cb)(struct PP *pp, enum PPDtype dtype, void *user_data);

void pp_handle_data_cb(struct PP *pp, enum PPDtype dtype, void *user_data);

void pp_add_parse_entry(struct PP *pp, const char *start, const char *end, enum PPDtype dtype, entry_cb cb, enum PPParseMethod pm);


// helpers
int str_ends_with(const char *str, const char *substr);
int pp_str_split_at_char(char *str, char c, char **rstr);
void pp_print_spaces(int n);


void pp_stack_init(struct PPStack *stack);
int pp_stack_put(struct PPStack *stack, struct PPItem ji);
int pp_stack_pop(struct PPStack *stack);
void pp_stack_debug(struct PPStack *stack);
struct PPItem* pp_stack_get_from_end(struct PP *pp, int offset);

size_t pp_parse(struct PP *pp, char **chunks, size_t nchunks);
void pp_xml_stack_debug(struct PPStack *stack);

#endif
