#include "potato_regex.h"

#define DEBUG(M, ...) if(do_debug){fprintf(stdout, "[DEBUG] " M, ##__VA_ARGS__);}
#define INFO(M, ...) if(do_info){fprintf(stdout, M, ##__VA_ARGS__);}
#define ERROR(M, ...) if(do_error){fprintf(stderr, "[ERROR] (%s:%d) " M, __FILE__, __LINE__, ##__VA_ARGS__);}



int re_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

int re_is_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int re_is_upper(char c)
{
    return c >= 'A' && c <= 'Z';
}

int re_is_lower(char c)
{
    return c >= 'a' && c <= 'z';
}

int re_is_whitespace(char c)
{
    for (int i=0 ; i<strlen(RE_SPACE_CHARS) ; i++) {
        if (c == RE_SPACE_CHARS[i])
            return 1;
    }
    return 0;
}

int re_is_in_range(char c, char lc, char rc)
{
    // check range validity
    if (re_is_digit(lc) != re_is_digit(rc)) {
        ERROR("Bad range: %c-%c\n", lc, rc);
        return -1;
    }
    if (re_is_alpha(lc) && (re_is_lower(lc) != re_is_lower(rc))) {
        ERROR("Bad alpha range: %c-%c\n", lc, rc);
        return -1;
    }
    return c >= lc && c <= rc;
}

enum REMetaChar re_get_meta_char(char *s)
{
    /* reads first meta char from  string */
    assert(strlen(s) > 0);

    char c = *s;

    if (strlen(s) > 1 && *s == '\\') {
        c = s[1];
        DEBUG("2 char meta: \\%c\n", c);
        switch (c) {
            case 'd':
                return RE_MATCH_DIGIT;
            case 'D':
                return RE_MATCH_NON_DIGIT;
            case 'w':
                return RE_MATCH_ALPHA_NUM;
            case 'W':
                return RE_MATCH_ALPHA_NUM;
            case 's':
                return RE_MATCH_SPACE;
            case 'S':
                return RE_MATCH_NON_SPACE;
            default:
                return RE_CHAR;
        }
    }

    DEBUG("1 char meta: %c\n", c);

    switch (c) {
        case '*':
            return RE_META_STAR;
        case '+':
            return RE_META_PLUS;
        case '?':
            return RE_META_QUESTION;
        case '{':
            return RE_META_RANGE_START;
        case '}':
            return RE_META_RANGE_END;
        case '(':
            return RE_META_GROUP_START;
        case ')':
            return RE_META_GROUP_END;
        case '[':
            return RE_META_CCLASS_START;
        case ']':
            return RE_META_CCLASS_END;
        case '|':
            return RE_META_PIPE;
        case '\\':
            return RE_META_BACKSLASH;
        // decide between BEGIN and NEGATE
        //case '^':
        //    return RE_META_NEGATE;
        case '^':
            return RE_META_BEGIN;
        case '$':
            return RE_META_END;
        case '.':
            return RE_META_DOT;
        default:
            return RE_CHAR;
    }
}

enum REResult re_tokenize(const char *re_str, enum REMetaChar *tokens)
{
    return RE_RES_SUCCES;
}

int re_pos_next(struct REPosition *pos)
{
    if (pos->npos >= strlen(pos->pattern))
        return -1;

    pos->c++;
    pos->npos++;
    return 0;
}

int re_pos_prev(struct REPosition *pos)
{
    if (pos->npos <= 0)
        return -1;

    pos->c--;
    pos->npos--;
    return 0;
}

struct REPosition re_pos_init(const char *pattern)
{
    struct REPosition pos;
    memset(&pos, 0, sizeof(struct REPosition));
    strncpy(pos.pattern, pattern, RE_MAX_PATTERN);
    pos.c = pos.pattern;
    return pos;
}

struct RERegex re_regex_init(const char *pattern)
{
    struct RERegex r;
    memset(&r, 0, sizeof(struct RERegex));
    r.pos = re_pos_init(pattern);
    return r;
}

void re_test()
{
    DEBUG("range: %d\n", re_is_in_range('3', 'a', 'z'));
    DEBUG("range: %d\n", re_is_in_range('c', 'A', '0'));
    DEBUG("range: %d\n", re_is_in_range('c', 'a', 'z'));
    DEBUG("range: %d\n", re_is_in_range('c', 'a', 'Z'));
    DEBUG("range: %d\n", re_is_in_range('c', 'a', '0'));
    DEBUG("islower: %d\n", re_is_lower('C'));
    DEBUG("isupper: %d\n", re_is_upper('C'));
    DEBUG("isdigit: %d\n", re_is_digit('C'));
    DEBUG("isdigit: %d\n", re_is_digit('3'));

    DEBUG("iswhitespace: %d\n", re_is_whitespace(' '));
    DEBUG("iswhitespace: %d\n", re_is_whitespace('\n'));
    DEBUG("iswhitespace: %d\n", re_is_whitespace('x'));

    DEBUG("meta: \\w %d\n", re_get_meta_char("\\w"));
    DEBUG("meta: \\w %d\n", re_get_meta_char("\\wlkjlkjlk"));
    DEBUG("meta: \\s %d\n", re_get_meta_char("\\s"));
    DEBUG("meta: \\ %d\n", re_get_meta_char("\\"));

    DEBUG("meta: \\+ %d\n", re_get_meta_char("\\+"));
    DEBUG("meta: + %d\n", re_get_meta_char("+"));
    struct RERegex r = re_regex_init("\\w\\s");
}

