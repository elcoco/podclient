#include "potato_parser.h"

#define DO_DEBUG 1
#define DO_INFO  1
#define DO_ERROR 1

#define DEBUG(M, ...) if(do_debug){fprintf(stdout, "[DEBUG] " M, ##__VA_ARGS__);}
#define INFO(M, ...) if(do_info){fprintf(stdout, M, ##__VA_ARGS__);}
#define ERROR(M, ...) if(do_error){fprintf(stderr, "[ERROR] (%s:%d) " M, __FILE__, __LINE__, ##__VA_ARGS__);}

#define ASSERTF(A, M, ...) if(!(A)) {ERROR(M, ##__VA_ARGS__); assert(A); }
#define ARR_SIZE(X) {sizeof(X) / sizeof(*X)}

// TODO: add a way to differentiate between Buffer overflow and success in fforward_skip_escaped() and str_search()
//

static struct PPPosition pp_pos_init(char **chunks, size_t nchunks);
static int pp_pos_next(struct PPPosition *pos);
static int pp_pos_prev(struct PPPosition *pos);
static struct PPPosition pp_pos_copy(struct PPPosition *src);
static void pp_pos_debug(struct PPPosition *pos);
static void pp_token_strip(struct PPToken *t);

// HELPERS ////////////////////////////
static void pp_str_reverse(char *str)
{
    char *lptr = str;
    char *rptr = str+strlen(str)-1;
    char buf;

    for (int i=0 ; i<strlen(str)/2 ; i++) {
        buf = *lptr;
        *lptr = *rptr;
        *rptr = buf;
        lptr++;
        rptr--;
    }
}

static void pp_pos_debug(struct PPPosition *pos)
{
    /* Print out current buffer position with some context.
     * Does look in chunks before and after.
     *      *  = unprintable chars
     * \t\n\r  = replaced by strings
     * SOD/EOD = start/end of data */
    char lctext[PP_ERR_CHARS_CONTEXT+1] = "";       // buffer for string left from current char
    char rctext[PP_ERR_CHARS_CONTEXT+1] = "";       // buffer for string right from current char

    char *lptr = lctext;
    char *rptr = rctext;

    int sod = 0;
    int eod = 0;

    struct PPPosition pos_cpy = pp_pos_copy(pos);

    // get context
    for (int i=0 ; i<PP_ERR_CHARS_CONTEXT ; i++, lptr++) {
        if (pp_pos_prev(&pos_cpy) < 0) {
            sod = 1;
            break;
        }
        *lptr = *(pos_cpy.c);
        *(lptr+1) = '\0';
    }

    pp_str_reverse(lctext);

    pos_cpy = pp_pos_copy(pos);

    for (int i=0 ; i<PP_ERR_CHARS_CONTEXT ; i++, rptr++) {
        if (pp_pos_next(&pos_cpy) < 0) {
            eod = 1;
            break;
        }
        *rptr = *(pos_cpy.c);
        *(rptr+1) = '\0';
    }

    char buf[16] = "";

    if (*(pos->c) == '\n')
        strncpy(buf, "\n\\n", sizeof(buf));
    else if (*(pos->c) == '\t')
        strncpy(buf, "\t\\n", sizeof(buf));
    else if (*(pos->c) == '\r')
        strncpy(buf, "\r\\r", sizeof(buf));
    // check if printable
    else if (*(pos->c) < 33 || *(pos->c) > 126)
        strncpy(buf, "*", 1);
    else
        strncpy(buf, pos->c, 1);

    if (sod)
        printf("%sSOD%s", XBLUE, XRESET);
    printf("\n%s%s%s%s%s", lctext, XRED, buf, XRESET, rctext);
    if (eod)
        printf("%sEOD%s", XBLUE, XRESET);
    printf("\n");
}

static int pp_strstr_nth(const char *haystack, const char *needle, int nth)
{
    int count = 0;
    const char *next = haystack;
    while ((next = strstr(next, needle)) != NULL) {
        ++count;
        ++next;
        DEBUG("count: %d, nth: %d\n", count, nth);
        if (count == nth) {
            DEBUG("FOund %dnth\n", nth);
            return 1;
        }
    }
    return -1;
}

static char* pp_str_remove_leading_chars(char *buf, const char *chars)
{
    char *ptr = buf;
    for (int i=0 ; i<strlen(buf) ; i++) {

            if (strchr(chars, *ptr))
                ptr++;
            else
                break;
    }
    return ptr;
}

static void pp_add_to_buf(char *buf, int len, char c)
{
    if (strlen(buf) >= len-1) {
        // shift string one place to the left
        char *lptr = buf;
        char *rptr = buf+1;
        while (*rptr != '\0')
            *lptr++ = *rptr++;

        // add char
        *lptr = c;
        //*(lptr+1) = '\0';
    }
    else {
        buf[strlen(buf)] = c;
        buf[strlen(buf)+1] = '\0';
    }
}

void pp_print_spaces(int n)
{
    for (int i=0 ; i<n ; i++)
        printf(" ");
}

int pp_str_split_at_char(char *str, char c, char **rstr)
{
    /* Replace first occurance of c in str with '\0' char.
     * *rstr is a pointer to the char after '\0'
     * If char is last char in str, *rstr == NULL
     */

    /*
    char *pos;
    if ((pos = strchr(str, c)) == NULL) {
        *rstr = NULL;
        return -1;
    }
    *pos = '\0';
    return 1;
    */



    *rstr = NULL;
    int size = strlen(str);

    for (int i=0 ; i<size ; i++) {
        if (str[i] == c) {
            str[i] = '\0';
            if (i+1 < size)
                *rstr = str + (i+1);

            return 1;
        }
    }
    return -1;
}

int str_ends_with(const char *str, const char *substr)
{
    size_t offset_start = strlen(str)-strlen(substr);
    return strncmp(str+offset_start, substr, strlen(substr)) == 0;
}

char* pp_get_chr_repr(char c, char *buf) {
    switch (c) {
        case '\n':
            strcpy(buf, "\\n");
            break;
        case '\t':
            strcpy(buf, "\\t");
            break;
        case '\r':
            strcpy(buf, "\\r");
            break;
        case 32 ... 126:
            buf[0] = ' ';
            buf[1] = c;
            buf[2] = '\0';
            break;
        default:
            sprintf(buf, "%d", c);
    }
    return buf;
}

// TOKEN ////////////////////////////
static enum PPSearchResult pp_token_search(struct PPToken *t, struct PPPosition *pos, size_t buf_size, enum PPParserState s, int reuse_skip_data)
{
    const char *start   = t->start_str;
    const char *end     = t->end_str;
    const char *allow_leading   = t->allow_leading;    // allow these chars when itering. If NULL: allow all
                                             
    const char *delim   = t->delim_chars;    // stop when a char from this list is found
    const char *allow   = t->allow_chars;    // allow these chars when itering. If NULL: allow all
    const char *save    = t->save_chars;     // save these chars to buf
    const char *illegal = t->illegal_chars;  // error on any of these chars. if NULL: allow all
                                             //
    int triggered = 0;

    if (s == PSTATE_UNDEFINED) {
        if (start)
            s = PSTATE_FIND_START;
        else if(delim)
            s = PSTATE_FIND_DELIM;
        else
            return PP_SEARCH_RESULT_SYNTAX_ERROR;
    }

    // Scrolling buffer to find strings in
    char search_buf[PP_MAX_SEARCH_BUF] = "";

    assert(PP_MAX_SEARCH_BUF >= PP_MAX_SKIP_DATA);

    if (reuse_skip_data)
        strncpy(search_buf, t->skip_data, PP_MAX_SEARCH_BUF);
    else
        memset(t->skip_data, 0, PP_MAX_SKIP_DATA);

    char *psave = t->data;

    // for debugging
    char chr_buf[32] = "";

    int save_count = 0;
    int first = 1;
    int buffer_overflow = 0;

    while (s != PSTATE_REJECT_EOD && s != PSTATE_REJECT_EOD_TRIGG && s != PSTATE_ACCEPT) {

        if (first--<= 0 && pp_pos_next(pos) < 0) {

            if (triggered)
                s = PSTATE_REJECT_EOD_TRIGG;
            else
                s = PSTATE_REJECT_EOD;
            continue;
        }

        switch(s) {

            // Handle finding of chars and strings
            case PSTATE_FIND_START:
                DEBUG("[%s] STATE: FIND_START: '%s'\n", pp_get_chr_repr(*pos->c, chr_buf), start);

                pp_add_to_buf(search_buf, PP_MAX_SEARCH_BUF, *pos->c);
                if (strstr(search_buf, start) != NULL) {
                    DEBUG("FOUND START\n");
                    strncpy(t->data, start, buf_size);
                    psave = t->data + strlen(start);
                    search_buf[0] = '\0';;
                    pp_add_to_buf(search_buf, PP_MAX_SEARCH_BUF, *pos->c);
                    triggered = 1;

                    if (end)
                        s = PSTATE_FIND_END;
                    else if (delim)
                        s = PSTATE_FIND_DELIM_ALLOW_ALL;
                    else if (!delim && !end)
                        s = PSTATE_ACCEPT;
                    else
                        assert(!"STATE: Token config error\n");
                    continue;
                }
                break;

            case PSTATE_FIND_END:
                DEBUG("[%s] STATE: FIND_END: '%s'\n", pp_get_chr_repr(*pos->c, chr_buf), end);
                pp_add_to_buf(search_buf, PP_MAX_SEARCH_BUF, *pos->c);

                if (strstr(search_buf, end) != NULL) {
                    DEBUG("FOUND END: %s\n", end);
                    //pp_pos_debug(pos);
                    if (!buffer_overflow) {
                        *psave++ = *pos->c;
                        *psave   = '\0';
                    }
                    s = PSTATE_ACCEPT;
                    continue;
                }
                break;


            case PSTATE_FIND_DELIM_ALLOW_ALL:
            case PSTATE_FIND_DELIM:
                DEBUG("[%s] STATE: FIND_DELIM: '%s'\n", pp_get_chr_repr(*pos->c, chr_buf), delim);
                if (strchr(delim, *pos->c) != NULL) {
                    DEBUG("Found DELIM char: '%c'\n", *pos->c);
                    s = PSTATE_ACCEPT;
                    if (save_count <= buf_size-1) {
                        *psave++ = *pos->c;
                        *psave   = '\0';
                    }
                    continue;
                }
                break;

            default:
                assert(!"INVALID STATE: find\n");
        }


        // Handle allowed chars, error on disallowed chars
        switch(s) {
            case PSTATE_FIND_START:
                if (allow_leading && !strchr(allow_leading, *pos->c) && !strchr(start, *pos->c)) {
                    DEBUG("DISALOWED LEADING CHAR: '%c'\n", *pos->c);
                    return PP_SEARCH_RESULT_SYNTAX_ERROR;
                }
                break;


            case PSTATE_FIND_DELIM_ALLOW_ALL:
            case PSTATE_FIND_END:
                // allow all chars until end is found
                break;

            case PSTATE_FIND_DELIM:

                if (allow && !strchr(allow, *pos->c)) {
                    DEBUG("DISALOWED CHAR: '%c'\n", *pos->c);
                    return PP_SEARCH_RESULT_SYNTAX_ERROR;
                }
                triggered = 1;
                break;
            default:
                assert(!"INVALID STATE: allow\n");
                break;
        }


        // Handle char saving
        switch(s) {
            case PSTATE_FIND_START:
                // don't save
                break;

            case PSTATE_FIND_DELIM_ALLOW_ALL:
            case PSTATE_FIND_END:
            case PSTATE_FIND_DELIM:
                if (!save || (save && strchr(save, *pos->c) != NULL)) {
                    if (save_count >= buf_size-1) {
                        buffer_overflow = 1;
                        DEBUG("BUFFER OVERFLOW buf_size: %ld, count: %d\n", buf_size, save_count);
                        strncpy(t->data, PP_BUFFER_OVERFLOW_PLACEHOLDER, PP_MAX_TOKEN_DATA-1);
                    }
                    else {
                        //DEBUG("SAVE CHR: %c\n", *pos->c);
                        DEBUG("[%s] SAVE\n", pp_get_chr_repr(*pos->c, chr_buf));
                        save_count++;
                        *psave++ = *pos->c;
                        *psave   = '\0';
                    }
                    pp_add_to_buf(t->skip_data, PP_MAX_SKIP_DATA, *pos->c);
                    //DEBUG("[%c] BUF: '%s'\n", *pos->c, t->skip_data);
                }
                break;

            default:
                assert(!"INVALID STATE: save\n");
        }
    }

    if (s == PSTATE_REJECT_EOD)
        return PP_SEARCH_RESULT_END_OF_DATA;
    else if (s == PSTATE_REJECT_EOD_TRIGG)
        return PP_SEARCH_RESULT_END_OF_DATA_TRIGGERED;

    assert(s == PSTATE_ACCEPT);

    if (t->greedy == PP_METHOD_NON_GREEDY && !buffer_overflow)
        pp_token_strip(t);

    if (t->step_over)
        pp_pos_next(pos);

    assert(strcmp(t->data, "<BLOCK>") != 0);

    DEBUG("END SEARCH: '%s'\n", t->data);
    return PP_SEARCH_RESULT_SUCCESS;
}

static void pp_token_strip(struct PPToken *t)
{
    if (t->end_str) {
        int index = strlen(t->data)-strlen(t->end_str);
        t->data[index] = '\0';
    }
    if (t->delim_chars)
        t->data[strlen(t->data)-1] = '\0';

    if (t->start_str) {
        char *l = t->data;
        char *r = t->data+strlen(t->start_str);
        while (*r != '\0')
            *l++ = *r++;
        *l = '\0';
    }
}


// STACK ////////////////////////////
void pp_stack_init(struct PPStack *stack)
{
    memset(stack, 0, sizeof(struct PPToken) * PP_MAX_STACK);
    stack->pos = -1;
}

int pp_stack_put(struct PPStack *stack, struct PPToken ji)
{
    ASSERTF(stack->pos <= PP_MAX_STACK -1, "Can't PUT, stack is full!\n");
    ASSERTF(ji.dtype != PP_DTYPE_UNKNOWN, "item has no datatype!\n");
    (stack->pos)++;
    //DEBUG("[%d] PUT: %s\n", stack->pos, ji.data);
    memcpy(&(stack->stack[stack->pos]), &ji, sizeof(struct PPToken));
    return 0;
}

int pp_stack_pop(struct PPStack *stack)
{
    ASSERTF(stack->pos >= 0, "Can't POP, stack is empty!\n");
    //DEBUG("[%d] POP: %s\n", stack->pos, stack->stack[stack->pos].data);
    memset(&(stack->stack[stack->pos]), 0, sizeof(struct PPToken));
    (stack->pos)--;
    return 0;
}

struct PPToken* pp_stack_get_from_end(struct PP *pp, int offset)
{
    if (offset > pp->stack.pos+1)
        return NULL;
    return &(pp->stack.stack[pp->stack.pos - offset]);
}

void pp_xml_stack_debug(struct PPStack *stack)
{
    INFO("STACK CONTENTS\n");
    struct PPToken *t = stack->stack;

    for (int i=0 ; i<PP_MAX_STACK ; i++, t++) {
        char dtype[16] = "";
        switch (t->dtype) {
            case PP_DTYPE_TAG_OPEN:
                strcpy(dtype, "TAG_OPEN  ");
                break;
            case PP_DTYPE_TAG_CLOSE:
                strcpy(dtype, "TAG_CLOSE ");
                break;
            case PP_DTYPE_HEADER:
                strcpy(dtype, "HEADER    ");
                break;
            case PP_DTYPE_STRING:
                strcpy(dtype, "STRING    ");
                break;
            case PP_DTYPE_CDATA:
                strcpy(dtype, "CDATA     ");
                break;
            case PP_DTYPE_COMMENT:
                strcpy(dtype, "COMMENT   ");
                break;
            case PP_DTYPE_UNKNOWN:
                strcpy(dtype, "EMPTY     ");
                break;
        }

        if (strlen(t->data) > 0) {
            INFO("%d: dtype: %s  =>  %s\n", i, dtype, t->data);
        }
        else {
            INFO("%d: dtype: %s\n", i, dtype);
        }
    }
}


// POSITION ////////////////////////////
static struct PPPosition pp_pos_init(char **chunks, size_t nchunks)
{
    struct PPPosition pos;
    pos.max_chunks = nchunks;
    pos.chunks = chunks;
    pos.c = pos.chunks[0];
    pos.npos = 0;
    pos.length = strlen(pos.c);
    pos.cur_chunk = 0;
    return pos;
}

static int pp_pos_next(struct PPPosition *pos)
{
    /* Iter over chunks, one char at a time.
     * Move to next chunk if all data in chunk is read
     */

    if (pos->npos >= pos->length-1) {
        if (pos->cur_chunk < pos->max_chunks-1 &&  pos->chunks[pos->cur_chunk+1] != NULL) {
            // goto next chunk
            pos->cur_chunk++;
            pos->npos = 0;
            pos->c = pos->chunks[pos->cur_chunk];
            pos->length = strlen(pos->chunks[pos->cur_chunk]);
            return 0;
        }
        else {
            // no more chunks
            return -1;
        }
    }
    (pos->c)++;
    (pos->npos)++;
    return 0;
}

static int pp_pos_prev(struct PPPosition *pos)
{
    /* Iter over chunks, one char at a time.
     * Move to next chunk if all data in chunk is read
     */
    if (pos->npos == 0) {
        if (pos->cur_chunk != 0) {
            // goto prev chunk
            pos->cur_chunk--;
            pos->npos = strlen(pos->chunks[pos->cur_chunk]) -1;
            pos->c = pos->chunks[pos->cur_chunk] + pos->npos;
            pos->length = strlen(pos->chunks[pos->cur_chunk]);
            return 0;
        }
        else {
            // we are on first chunk
            return -1;
        }
    }
    (pos->c)--;
    (pos->npos)--;
    return 0;
}

static struct PPPosition pp_pos_copy(struct PPPosition *src)
{
    /* Copy struct to new struct */
    struct PPPosition pos_cpy;
    memcpy(&pos_cpy, src, sizeof(struct PPPosition));
    return pos_cpy;
}

static void pp_pos_print_chunks(struct PPPosition *pos)
{
    struct PPPosition pos_cpy = pp_pos_copy(pos);
    printf("\n** CHUNKS *********************************\n");
    int cur_chunk = -1;
    while (1) {
        if (cur_chunk != pos_cpy.cur_chunk) {
            cur_chunk = pos_cpy.cur_chunk;
            printf("%sSOD_%d%s\n", XRED, cur_chunk, XRESET);
        }
        printf("%c", *pos_cpy.c);
        
        if (pp_pos_next(&pos_cpy) < 0)
            break;
    }
    printf("%sEOD%s\n", XBLUE, XRESET);
    printf("\n*******************************************\n");
}


// TOKEN ///////////////////////////////
struct PPToken pp_token_init()
{
    struct PPToken t;
    memset(&t, 0, sizeof(struct PPToken));
    t.delim_chars = NULL;
    t.save_chars = NULL;
    t.allow_chars = NULL;
    t.illegal_chars = NULL;

    t.allow_leading = NULL;
    t.start_str = NULL;
    t.end_str = NULL;

    //t.any = NULL;
    return t;
}

void pp_add_parse_token(struct PP *pp, struct PPToken pe)
{
    /* Add a parse token to the pp struct.
     * Parse tokens define the start/end strings when parsing a file
     * eg: when searching for cdata in an XML file this means
     *       start: <![CDATA[
     *       end:   ]]>
     *       capture everything inbetween.
     */
    assert(pp->max_tokens+1 <= PP_MAX_PARSER_TOKENS); // tokens max reached!
    pp->max_tokens++;
    pp->tokens[pp->max_tokens-1] = pe;
}


// PARSER //////////////////////////////
static void pp_print_parse_error(struct PP *pp, const char *msg)
{
    if (msg != NULL)
        ERROR("%s", msg);

    ERROR("PP syntax error: '%s%c%s' @ %d\n", XRED, *(pp->pos.c), XRESET, pp->pos.npos);
    pp_pos_debug(&pp->pos);
}

static struct PPToken pp_item_init(enum PPDtype dtype, char *data)
{
    assert(dtype != PP_DTYPE_UNKNOWN); // PPItem should always have a datatype
    struct PPToken item;
    item.dtype = dtype;
    strncpy(item.data, data, PP_MAX_TOKEN_DATA);
    //item.param = NULL;
    memset(&(item.param), 0, PP_XML_MAX_PARAM * sizeof(struct PPXMLParam));
    return item;
}

void pp_token_set_data(struct PPToken *p, const char *data)
{
    strncpy(p->data, data, PP_MAX_TOKEN_DATA);
}

enum PPParseResult pp_parse_token(struct PP *pp, struct PPPosition *pos_cpy, struct PPToken *t)
{
    DEBUG("\n");
    switch(t->dtype) {
        case PP_DTYPE_STRING:
            DEBUG("** TRYING STRING: '%s' <-> '%s'\n", t->start_str, t->end_str);
            break;
        case PP_DTYPE_OBJECT_OPEN:
            DEBUG("** TRYING OBJECT_OPEN\n");
            break;
        case PP_DTYPE_OBJECT_CLOSE:
            DEBUG("** TRYING OBJECT_CLOSE\n");
            break;
        case PP_DTYPE_ARRAY_OPEN:
            DEBUG("** TRYING ARRAY_OPEN\n");
            break;
        case PP_DTYPE_ARRAY_CLOSE:
            DEBUG("** TRYING ARRAY_CLOSE\n");
            break;
        case PP_DTYPE_NUMBER:
            DEBUG("** TRYING NUMBER\n");
            break;
        case PP_DTYPE_BOOL:
            DEBUG("** TRYING BOOL\n");
            break;
        case PP_DTYPE_KEY:
            DEBUG("** TRYING KEY\n");
            break;

        case PP_DTYPE_TAG_OPEN:
            DEBUG("** TRYING TAG_OPEN\n");
            break;
        case PP_DTYPE_TAG_CLOSE:
            DEBUG("** TRYING TAG_CLOSE\n");
            break;
        case PP_DTYPE_COMMENT:
            DEBUG("** TRYING COMMENT\n");
            break;
        case PP_DTYPE_CDATA:
            DEBUG("** TRYING CDATA\n");
            break;
        case PP_DTYPE_HEADER:
            DEBUG("** TRYING HEADER\n");
            break;
        default:
            DEBUG("** TRYING UNKNOWN: %d\n", t->dtype);
    }

    // search will return end of data, which will trigger an incomplete
    // when just a couple of allowed chars are found.
    // So in case of a buffer overflow, a search will be set for a token that shouldn't trigger
    // in the first place
    enum PPSearchResult res_end = pp_token_search(t, &pp->pos, PP_MAX_TOKEN_DATA, PSTATE_UNDEFINED, 0);

    if (res_end == PP_SEARCH_RESULT_SYNTAX_ERROR)
        return PP_PARSE_RESULT_NO_MATCH;

    else if (res_end == PP_SEARCH_RESULT_END_OF_DATA_TRIGGERED) {
        pp->t_skip = *t;
        pp->t_skip_is_set = 1;
        return PP_PARSE_RESULT_INCOMPLETE;
    }
    else if (res_end == PP_SEARCH_RESULT_END_OF_DATA) {
        return PP_PARSE_RESULT_NO_MATCH;
    }

    pp->t_skip_is_set = 0;

    if (t->cb == NULL) {
        assert(t->dtype != PP_DTYPE_UNKNOWN);  // trying to use uninitialised item
        pp_stack_put(&(pp->stack), *t);
        pp->handle_data_cb(pp, t->dtype, pp->user_data);
        pp_stack_pop(&(pp->stack));
    }
    else {
        enum PPParseResult cb_res = t->cb(pp, t);
        if  (cb_res < PP_PARSE_RESULT_SUCCESS)
            return cb_res;
    }

    return PP_PARSE_RESULT_SUCCESS;
}

size_t pp_parse(struct PP *pp, char **chunks, size_t nchunks)
{
    DEBUG("\n");
    DEBUG("** STARTING PASS **********************\n");

    pp->pos = pp_pos_init(chunks, nchunks);
    size_t nread = 0;

    if (pp->zero_rd_cnt >= pp->pos.max_chunks-1) {
        assert(pp->t_skip_is_set == 1);                // trying to skip to skip char but it has no intentional value
                                                     
        enum PPSearchResult res;


        if (pp->t_skip.end_str) {
            INFO("[%d] Skip to: '%s'\n", pp->zero_rd_cnt, pp->t_skip.end_str);
            res = pp_token_search(&(pp->t_skip), &(pp->pos), PP_MAX_TOKEN_DATA, PSTATE_FIND_END, 1);
        }
        else if (pp->t_skip.delim_chars) {
            INFO("[%d] Skip to: '%s'\n", pp->zero_rd_cnt, pp->t_skip.delim_chars);
            res = pp_token_search(&(pp->t_skip), &(pp->pos), PP_MAX_TOKEN_DATA, PSTATE_FIND_DELIM, 1);
        }
        else {
            DEBUG("end: %s, delim: %s\n", pp->t_skip.end_str, pp->t_skip.delim_chars);
            assert(!"Don't know where to skip to!\n");
        }

        if (res == PP_SEARCH_RESULT_SUCCESS) {
            // add a placeholder item that represents the item with missing data due to buffer overflow
            pp_token_set_data(&(pp->t_skip), PP_BUFFER_OVERFLOW_PLACEHOLDER);

            if (pp->t_skip.cb != NULL) {
                enum PPParseResult cb_res = pp->t_skip.cb(pp, &(pp->t_skip));
                if  (cb_res < PP_PARSE_RESULT_SUCCESS)
                    return cb_res;
            }
            else {
                pp_stack_put(&(pp->stack), pp->t_skip);
                pp->handle_data_cb(pp, pp->t_skip.dtype, pp->user_data);
                pp_stack_pop(&(pp->stack));
            }
            memset(pp->t_skip.skip_data, 0, PP_MAX_SKIP_DATA);

            pp->t_skip_is_set = 0;
            pp->zero_rd_cnt = 0;
            nread = pp->pos.npos;

        }
        else if (res == PP_SEARCH_RESULT_SYNTAX_ERROR) {
            pp_print_parse_error(pp, "Failed to parse string\n");
            return -1;
        }
        else {
            pp->zero_rd_cnt++;
            return pp->pos.npos;
        }
    }

    while (1) {
        struct PPPosition pos_cpy = pp_pos_copy(&(pp->pos));
        struct PPToken *pe = pp->tokens;

        for (int i=0 ; i<pp->max_tokens ; i++, pe++) {
            //pp_pos_debug(&(pp->pos));
            enum  PPParseResult res;
            if ((res = pp_parse_token(pp, &pos_cpy, pe)) == PP_PARSE_RESULT_INCOMPLETE) {
                if (nread == 0)
                    pp->zero_rd_cnt++;
                else if (nread > 0)
                    pp->zero_rd_cnt = 0;
                return nread;
            }
            else if (res == PP_PARSE_RESULT_SUCCESS) {
                nread = pp->pos.npos;
                break;
            }
            else if (res == PP_PARSE_RESULT_NO_MATCH) {
                if (i == pp->max_tokens-1) {
                    DEBUG("No match was found\n");
                    return nread;
                }
                assert(i != pp->max_tokens-1 && "endless loop!");
                pp->pos = pp_pos_copy(&pos_cpy);
                continue;
            }
            else {
                pp_print_parse_error(pp, "Failed to parse string\n");
                return -1;
            }
        }
    }
}
