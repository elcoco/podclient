#ifndef POTATO_REGEX_H
#define POTATO_REGEX_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define RE_MAX_TOKENS 50
#define RE_MAX_TOKEN_SIZE 2+1
#define RE_MAX_PATTERN    256

#define RE_SPACE_CHARS           " \n\r\t"

extern int do_debug;
extern int do_info;
extern int do_error;

enum REMetaChar {
    // QUANTIFIERS
    RE_META_UNDEFINED,
    RE_META_STAR,       //  *   GREEDY     match preceding 0 or more times
    RE_META_PLUS,       //  +   GREEDY     match preceding 1 or more times
    RE_META_QUESTION,   //  ?   NON GREEDY match preceding 1 time            when combined with another quantifier it makes it non greedy
                         //
    RE_META_RANGE_START,  // {n}  NON GREEDY match preceding n times
    RE_META_RANGE_END,    // {n}  NON GREEDY match preceding n times
    RE_META_GROUP_START,  // (
    RE_META_GROUP_END,    // )
    RE_META_CCLASS_START, // [
    RE_META_CCLASS_END,   // ]
                         
    // OPERATORS
    RE_META_PIPE,         //  |   OR
    RE_META_NEGATE,       // ^
                          //
    RE_META_BACKSLASH,    // \ backreference, not going to implement
    RE_META_BEGIN,        // ^
    RE_META_END,          // $
    RE_META_DOT,          // .    any char except ' '
                          
    RE_CHAR,             // literal char

                         
    RE_MATCH_DIGIT,            // \d   [0-9]
    RE_MATCH_NON_DIGIT,        // \D   [^0-9]
    RE_MATCH_ALPHA_NUM,        // \w   [a-bA-B0-9]
    RE_MATCH_NON_ALPHA_NUM,    // \W   [^a-bA-B0-9]
    RE_MATCH_SPACE,            // \s   ' ', \n, \t, \r
    RE_MATCH_NON_SPACE,        // \S   ^' '
                             //
};

//enum REState {
//};

// we keep the segments here until we process it because we found a quantifier
struct REStack {
};

struct REPosition {
    int npos;
    char *c;
    char pattern[RE_MAX_PATTERN];
};

struct REToken {
    enum REMetaChar type;

    // value in case of RE_CHAR
    char value;
};

struct RERegex {
    struct REToken tokens[RE_MAX_TOKENS];
    struct REPosition pos;
};

enum RETokenType {
    RE_TYPE_UNDEFINED,
    RE_TYPE_QUANTIFIER,
    RE_TYPE_SET,
    RE_TYPE_CHARACTER,
};

enum REResult {
    RE_RES_UNDEFINED,
    RE_RES_ERROR,
    RE_RES_SUCCES
};



enum REResult re_tokenize(const char *re_str, enum REMetaChar *tokens);
void re_test();

int re_is_digit(char c);
int re_is_alpha(char c);
int re_is_in_range(char c, char lc, char rc);



#endif // !POTATO_REGEX_H
