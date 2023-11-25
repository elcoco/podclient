#ifndef POTATO_PARSER_H
#define POTATO_PARSER_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

// NOTE If PP_MAX_TOKEN_DATA is too small, the tag is cut off when putting the data in a PPItem.
//      this MAY cause the string to split on '/', which with XML indicates that the tag should be closed.
//      This will trigger an error later when the actual closing tag appears.
//      Super corner case but this actually did happen once with the RSS tag!
// Max data length that can be in a XMLItem to hold data like: strings, numbers or bool
// If too small, strings will be cut off. Streams will not become corrupted.
#define PP_MAX_TOKEN_DATA 128

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
#define PP_MAX_PARSE_BUFFER 128

#define PP_MAX_SKIP_DATA 32

// When we search for a tag that is larger than the parse buffer, we will set the PP.t_skip token.
// This will force the parser to look for a specific end string on every pass until it is found.
// When found, a token of the correct type will be added to the stack. Data will be set to PP_BUFFER_OVERFLOW_PLACEHOLDER
// CORNER CASE:
// A tag can end up being split on two passes. We will never find it. This will occure a lot when using small buffers
// By reporting back PP->pos.npos-PP_N_SKIP_CHARS chars back to the caller we will receive PP_N_SKIP_CHARS
// back on the next pass. This will give us the chance to still find it
#define PP_N_SKIP_CHARS 5

#define PP_MAX_PARSER_TOKENS  16

// Parameters are an XML thing
#define PP_XML_MAX_PARAM         16
#define PP_XML_MAX_PARAM_KEY     256
#define PP_XML_MAX_PARAM_VALUE   256

// don't save these chars when looking for strings
#define PP_STR_SEARCH_IGNORE_CHARS "\r\t\n"
#define PP_STR_SEARCH_IGNORE_LEADING "\r\t "

// buffer that holds string while searching for substring.
// Must be at least as big as the search string
// Will probably not be very large. eg for xml: "\n<>"
#define PP_MAX_SEARCH_BUF 32+1
#define PP_MAX_SEARCH_IGNORE_CHARS 10

// NOTE: don't put any spaces here cause this will be split into data/parameters when parsing tag
#define PP_BUFFER_OVERFLOW_PLACEHOLDER "BUFFER_OVERFLOW!!!"

// How many chars to look behind/ahead the error in the parse string when displaying
// the error message.
// Current limitation is that it only looks in current chunk.
#define PP_ERR_CHARS_CONTEXT 100
#define XRESET   "\x1B[0m"
#define XRED     "\x1B[31m"
#define XGREEN   "\x1B[32m"
#define XYELLOW  "\x1B[33m"
#define XBLUE    "\x1B[34m"
#define XMAGENTA "\x1B[35m"
#define XCYAN    "\x1B[36m"
#define XWHITE   "\x1B[37m"

extern int do_debug;
extern int do_info;
extern int do_error;


enum PPSearchResult {
    PP_SEARCH_RESULT_SYNTAX_ERROR,       // eg. an unexpected tag. closing a tag that wasn't previously opened
    PP_SEARCH_RESULT_END_OF_DATA,          // end of data (position), didn't find result in data
    PP_SEARCH_RESULT_END_OF_DATA_TRIGGERED, // end of data (position), did find start string or a char we're looking for
    PP_SEARCH_RESULT_SUCCESS
};

enum PPParseResult {
    PP_PARSE_RESULT_ERROR,       // eg. an unexpected tag. closing a tag that wasn't previously opened
    PP_PARSE_RESULT_INCOMPLETE,           // when eg closing quote is not found, this is common when streaming data
    PP_PARSE_RESULT_NO_MATCH,
    PP_PARSE_RESULT_SUCCESS
};

/* Include start/end string in result */
enum PPParseMethod {
    PP_METHOD_NON_GREEDY,
    PP_METHOD_GREEDY
};

/* Datatype if PPItem and PPToken */
enum PPDtype {
    PP_DTYPE_UNKNOWN,
    PP_DTYPE_STRING,
    PP_DTYPE_CDATA,
    PP_DTYPE_TAG_OPEN,
    PP_DTYPE_TAG_CLOSE,
    PP_DTYPE_COMMENT,
    PP_DTYPE_HEADER,
    PP_DTYPE_OBJECT_OPEN,
    PP_DTYPE_OBJECT_CLOSE,
    PP_DTYPE_ARRAY_OPEN,
    PP_DTYPE_ARRAY_CLOSE,
    PP_DTYPE_KEY,
    PP_DTYPE_NUMBER,
    PP_DTYPE_BOOL
};

enum PPMatchType {
    PP_MATCH_UNKNOWN,
    PP_MATCH_START,
    PP_MATCH_END,
    PP_MATCH_START_END,
    PP_MATCH_ANY
};

// Struct used internally by pp_token_search()
struct PPSearchState {
    int triggered;

    int save_enabled;
    int save_count;
    char search_buf[PP_MAX_SEARCH_BUF];

    int start_found;
};

enum PPParserState {
    PSTATE_UNDEFINED,
    PSTATE_FIND_START,
    PSTATE_FIND_END,
    PSTATE_FIND_DELIM,
    PSTATE_FIND_DELIM_ALLOW_ALL,

    PSTATE_ACCEPT,
    PSTATE_REJECT_EOD,
    PSTATE_REJECT_EOD_TRIGG,
};

struct PPPosition {
    int npos;           // char counter
    char *c;            // pointer to current char in chunk
    int length;         // length of current chunk
    char **chunks;
    size_t max_chunks;
    size_t cur_chunk;   // index of current chunk
};

// XML specific. The stuff in the opening tag eg: <book category="bla">
struct PPXMLParam {
    char *key;
    char *value;
};

struct PP;

/* Defines how a bunch of chars should be parsed into a token.
 * It also holds the actual data */
struct PPToken {

    enum PPMatchType match_type;

    const char *delim_chars;        // stop searching when one of these chars is found
    const char *allow_chars;        // allow these chars
    const char *illegal_chars;     // error on any of these chars. error on none if NULL
    const char *save_chars;         // save any of these chars. Save all if NULL
                                    //
                                    
    const char *allow_leading;  // is only effective when searching for start string
    const char *start_str;     // capture between delimiters
    const char *end_str;

    enum PPDtype dtype;

    // callback handles sanity check, and adding/removing from stack
    enum PPParseResult(*cb)(struct PP *pp, struct PPToken *t);

    // include start/end strings in result
    enum PPParseMethod greedy;

    // Step over last char
    int step_over;

    // Below should possibly be stored in a struct that is cast to *void
    // holds the data for the token
    char data[PP_MAX_TOKEN_DATA];

    // something that writes to data is overwriting skip_data
    // fix this and remove bever
    int bever;

    char skip_data[PP_MAX_SKIP_DATA];

    // parameters are usefull for xml
    struct PPXMLParam param[PP_XML_MAX_PARAM];
};

struct PPStack {
    struct PPToken stack[PP_MAX_STACK];
    int pos;
};


struct PP {
    struct PPPosition pos;
    struct PPStack stack;
    struct PPToken tokens[PP_MAX_PARSER_TOKENS];

    void(*handle_data_cb)(struct PP *pp, enum PPDtype dtype, void *user_data);
    void *user_data;

    int max_tokens;

    // When a bufferoverflow occurs, instead of creating a larger buffer we save the token
    // that defines to which string we should skip.
    // Then keep on looking for this string and add a placeholder PPItem to the stack
    // It is a potato parser after all ;)
    struct PPToken t_skip;
    // indicate that kip has an intentional value
    unsigned char t_skip_is_set;
    // How many times we returned zero chars parsed, this indicates wether we should look for the above t_skip PPToken
    int zero_rd_cnt;
};

// Callbacks
//typedef enum PPParseResult(*token_cb)(struct PP *pp, struct PPToken *t);
typedef void(*handle_data_cb)(struct PP *pp, enum PPDtype dtype, void *user_data);


void pp_handle_data_cb(struct PP *pp, enum PPDtype dtype, void *user_data);
void pp_add_parse_token(struct PP *pp, struct PPToken pe);

// helpers
int str_ends_with(const char *str, const char *substr);
int pp_str_split_at_char(char *str, char c, char **rstr);
void pp_print_spaces(int n);


void pp_stack_init(struct PPStack *stack);
int pp_stack_put(struct PPStack *stack, struct PPToken ji);
int pp_stack_pop(struct PPStack *stack);
struct PPToken* pp_stack_get_from_end(struct PP *pp, int offset);

size_t pp_parse(struct PP *pp, char **chunks, size_t nchunks);

void pp_xml_stack_debug(struct PPStack *stack);
struct PPToken pp_token_init();

#endif
