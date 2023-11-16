#include "potato_parser.h"

#define DO_DEBUG 1
#define DO_INFO  1
#define DO_ERROR 1

#define DEBUG(M, ...) if(DO_DEBUG){fprintf(stdout, "[DEBUG] " M, ##__VA_ARGS__);}
#define INFO(M, ...) if(DO_INFO){fprintf(stdout, M, ##__VA_ARGS__);}
#define ERROR(M, ...) if(DO_ERROR){fprintf(stderr, "[ERROR] (%s:%d) " M, __FILE__, __LINE__, ##__VA_ARGS__);}

#define ASSERTF(A, M, ...) if(!(A)) {ERROR(M, ##__VA_ARGS__); assert(A); }
#define ARR_SIZE(X) {sizeof(X) / sizeof(*X)}

// TODO: add a way to differentiate between Buffer overflow and success in fforward_skip_escaped() and str_search()
//
static void pp_stack_init(struct PPStack *stack);

static void string_cb(struct PP *pp, struct PPParserEntry *pe, enum PPEvent ev, void *user_data);

static struct PPPosition pp_pos_init(char **chunks, size_t nchunks);
static int pp_pos_next(struct PPPosition *pos);
static struct PPPosition pp_pos_copy(struct PPPosition *src);
static int pp_stack_pop(struct PPStack *stack);
static int pp_stack_put(struct PPStack *stack, struct PPItem ji);

void tag_cb(struct PP *pp, struct PPParserEntry *pe, enum PPParserEvent ev, void *user_data)
{
}


static void pp_print_chunks(struct PPPosition *pos)
{
    struct PPPosition pos_cpy = pp_pos_copy(pos);
    printf("\n** CHUNKS:\n");
    int cur_chunk = -1;
    while (1) {
        if (cur_chunk != pos_cpy.cur_chunk) {
            cur_chunk = pos_cpy.cur_chunk;
            printf("**** CHUNK %d:\n", cur_chunk);
        }
        printf("%c", *pos_cpy.c);
        
        if (pp_pos_next(&pos_cpy) < 0)
            break;
    }
    printf("END CHUNKS\n\n");
}

static void pp_print_spaces(int n)
{
    for (int i=0 ; i<n ; i++)
        printf(" ");
}

struct PPItem* pp_stack_get_from_end(struct PP *pp, int offset)
{
    if (offset > pp->stack.pos+1)
        return NULL;
    return &(pp->stack.stack[pp->stack.pos - offset]);
}


void pp_handle_data_cb(struct PP *pp, enum PPDtype dtype, void *user_data)
{
    /* Callback can be used, instead of custom callback, to display full xml data */
    const int spaces = 2;

    struct PPItem *xi = pp_stack_get_from_end(pp, 0);
    ASSERTF(xi != NULL, "Callback received empty stack!");

    switch (dtype) {
        case PP_DTYPE_TAG_OPEN:
            pp_print_spaces(spaces * pp->stack.pos);
            if (xi->param != NULL) {
                INFO("TAG_OPEN: %s, param: %s\n", xi->data, xi->param);
            }
            else {
                INFO("TAG_OPEN: %s\n", xi->data);
            }
            break;
        case PP_DTYPE_TAG_CLOSE:
            pp_print_spaces(spaces * pp->stack.pos - spaces);
            INFO("TAG_CLOSE: %s\n", xi->data);
            break;
        case PP_DTYPE_STRING:
            pp_print_spaces(spaces * pp->stack.pos);
            INFO("STRING: %s\n", xi->data);
            break;
        case PP_DTYPE_CDATA:
            pp_print_spaces(spaces * pp->stack.pos);
            INFO("CDATA: %s\n", xi->data);
            break;
        case PP_DTYPE_HEADER:
            pp_print_spaces(spaces * pp->stack.pos);
            INFO("HEADER: %s\n", xi->data);
            break;
        case PP_DTYPE_COMMENT:
            pp_print_spaces(spaces * pp->stack.pos);
            INFO("COMMENT: %s\n", xi->data);
            break;
    }
}

static int str_ends_with(const char *str, const char *substr)
{
    size_t offset_start = strlen(str)-strlen(substr);
    return strncmp(str+offset_start, substr, strlen(substr)) == 0;
}

void xml_tag_close_cb(struct PP *pp, struct PPParserEntry *pe, struct PPItem *item)
{
    struct PPItem *item_prev = pp_stack_get_from_end(pp, 0);
    if (item_prev != NULL && item_prev->dtype == PP_DTYPE_TAG_OPEN) {
        pp_stack_put(&(pp->stack), *item);
        pp->handle_data_cb(pp, item->dtype, pp->user_data);
        pp_stack_pop(&(pp->stack));
        pp_stack_pop(&(pp->stack));
        return;
    }
    else {
        ERROR("Unexpected closing tag found: %s\n", item->data);
    }
}

void xml_tag_open_cb(struct PP *pp, struct PPParserEntry *pe, struct PPItem *item)
{
    // TODO: check if tag ends with />, if so, remove from stack
    pp_stack_put(&(pp->stack), *item);
    pp->handle_data_cb(pp, item->dtype, pp->user_data);
    if (str_ends_with(item->data, "/")) {
        pp_stack_pop(&(pp->stack));
    }
    return;
}

static void pp_add_parse_entry(struct PP *pp, const char *start, const char *end, enum PPDtype dtype, entry_cb cb, enum PPParseMethod pm)
{
    assert(pp->max_entries+1 <= PP_MAX_PARSER_ENTRIES); // entries max reached!
    pp->max_entries++;
    pp->entries[pp->max_entries-1] = (struct PPParserEntry){start, end, dtype, cb, pm};
}

struct PP pp_xml_init(handle_data_cb data_cb)
{
    struct PP pp;
    pp_stack_init(&(pp.stack));

    pp.max_entries = 0;
    //pp.skip_str[0] = '\0';
    pp.zero_rd_cnt = 0;
    pp.user_data = NULL;
    pp.handle_data_cb = data_cb;

    pp_add_parse_entry(&pp, PP_CHAR_COMMENT_START,   PP_CHAR_COMMENT_END,               PP_DTYPE_COMMENT,   NULL,             PP_PARSE_METHOD_GREEDY);
    pp_add_parse_entry(&pp, PP_CHAR_CDATA_START,     PP_CHAR_CDATA_END,                 PP_DTYPE_CDATA,     NULL,             PP_PARSE_METHOD_GREEDY);
    pp_add_parse_entry(&pp, PP_CHAR_HEADER_START,    PP_CHAR_HEADER_END,                PP_DTYPE_HEADER,    NULL,             PP_PARSE_METHOD_GREEDY);
    pp_add_parse_entry(&pp, PP_CHAR_TAG_CLOSE_START, PP_CHAR_TAG_CLOSE_END,             PP_DTYPE_TAG_CLOSE, xml_tag_close_cb, PP_PARSE_METHOD_GREEDY);
    pp_add_parse_entry(&pp, PP_CHAR_TAG_OPEN_START,  PP_CHAR_TAG_OPEN_END,              PP_DTYPE_TAG_OPEN,  xml_tag_open_cb,  PP_PARSE_METHOD_GREEDY);
    pp_add_parse_entry(&pp, "",                    "<",                                 PP_DTYPE_STRING,    NULL,             PP_PARSE_METHOD_NON_GREEDY);

    return pp;
}

static void pp_stack_init(struct PPStack *stack)
{
    memset(stack, 0, sizeof(struct PPItem) * PP_MAX_STACK);
    stack->pos = 0;
}

static int pp_stack_put(struct PPStack *stack, struct PPItem ji)
{
    ASSERTF(stack->pos <= PP_MAX_STACK -1, "Can't PUT, stack is full!\n");

    (stack->pos)++;
    memcpy(&(stack->stack[stack->pos]), &ji, sizeof(struct PPItem));
    return 0;
}

static int pp_stack_pop(struct PPStack *stack)
{
    ASSERTF(stack->pos >= 0, "Can't POP, stack is empty!\n");
    memset(&(stack->stack[stack->pos]), 0, sizeof(struct PPItem));
    (stack->pos)--;
    return 0;
}

static void pp_stack_debug(struct PPStack *stack)
{
    ERROR("STACK CONTENTS\n");
    struct PPItem *xi = stack->stack;

    for (int i=0 ; i<PP_MAX_STACK ; i++, xi++) {
        char dtype[16] = "";
        switch (xi->dtype) {
            case PP_DTYPE_TAG:
                strcpy(dtype, "TAG   ");
                break;
            case PP_DTYPE_HEADER:
                strcpy(dtype, "HEADER   ");
                break;
            case PP_DTYPE_STRING:
                strcpy(dtype, "STRING");
                break;
            case PP_DTYPE_CDATA:
                strcpy(dtype, "CDATA");
                break;
            case PP_DTYPE_COMMENT:
                strcpy(dtype, "COMMENT");
                break;
            case PP_DTYPE_UNKNOWN:
                return;
        }

        if (strlen(xi->data) > 0) {
            ERROR("%d: dtype: %s  =>  %s\n", i, dtype, xi->data);
        }
        else {
            ERROR("%d: dtype: %s\n", i, dtype);
        }
    }
}

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
    //INFO("char: %c, pos: %d, len= %d\n", *pos->c, pos->npos, pos->length);
    if (pos->npos >= pos->length-1) {
        if (pos->cur_chunk < pos->max_chunks-1 &&  pos->chunks[pos->cur_chunk+1] != NULL) {
            //INFO("Move to next chunk, %ld!\n", pos->cur_chunk+1);
            pos->cur_chunk++;
            pos->npos = 0;
            pos->c = pos->chunks[pos->cur_chunk];
            pos->length = strlen(pos->chunks[pos->cur_chunk]);
            return 0;
        }
        else {
            //INFO("No more chunks!\n");
            return -1;
        }
    }
    (pos->c)++;
    (pos->npos)++;
    return 0;
}

static struct PPPosition pp_pos_copy(struct PPPosition *src)
{
    /* Copy struct to new struct */
    struct PPPosition pos_cpy;
    memcpy(&pos_cpy, src, sizeof(struct PPPosition));
    return pos_cpy;
}

static int pp_str_contains_char(const char *str, const char *chars)
{
    for (int i=0 ; i<strlen(chars) ; i++) {
        DEBUG("%s        %s\n", str, chars);
        if (strchr(str, chars[i]) != NULL)
            return 1;
    }
    return 0;
}

static enum PPSearchResult pp_str_search_no_buf(struct PPPosition *pos, const char *search_str, const char *ignore_chars, char *save_buf, int save_buf_size)
{
    /* Search for substring in string.
     * If another char is found that is not op ignore_string, exit with error
     * if ignore_chars == NULL, allow all chars */
    char buf[PP_MAX_SEARCH_BUF] = "";
    char *ptr = buf;

    char ignore_buf[PP_MAX_SEARCH_BUF+PP_MAX_SEARCH_IGNORE_CHARS+1] = "";

    if (ignore_chars != NULL) {
        strncat(ignore_buf, search_str, PP_MAX_SEARCH_BUF);
        strncat(ignore_buf, ignore_chars, PP_MAX_SEARCH_IGNORE_CHARS);
    }

    int save_buf_i = 0;


    while (1) {
        
        if (strlen(buf) == PP_MAX_SEARCH_BUF-1) {
            // shift string one place to the left
            char *lptr = buf;
            char *rptr = buf+1;
            while (*rptr != '\0')
                *lptr++ = *rptr++;

            // add char
            *lptr = *pos->c;
            *(lptr+1) = '\0';
        }
        else {
            *ptr++ = *pos->c;
        }

        // buffer overflow
        if (save_buf != NULL && save_buf_i >= save_buf_size-1) {
            DEBUG("Buffer overflow\n");
            save_buf = NULL;
        }

        if (save_buf != NULL) {
            *(save_buf + save_buf_i) = *pos->c;
            *(save_buf + save_buf_i +1) = '\0';
        }

        if (strstr(buf, search_str) != NULL)
            return PP_SEARCH_SUCCESS;

        //if (ignore_chars != NULL && !pp_str_contains_char(buf, ignore_buf))
        //    return PP_SEARCH_SYNTAX_ERROR;

        if (ignore_chars != NULL && !strchr(ignore_buf, *pos->c))
            return PP_SEARCH_SYNTAX_ERROR;


        if (pp_pos_next(pos) < 0)
            return PP_SEARCH_END_OF_DATA;

        save_buf_i++;
    }
}

static char* pp_str_rm_leading(char *str, const char *chars)
{
    /* Remove, replace and lower a string */
    char *str_ptr = str;

    for (int i=0 ; i<strlen(str) ; i++, str_ptr++) {
        if (strchr(chars, *str_ptr) != NULL) {
            //DEBUG("Found >%c< in >%s<\n", *str_ptr, chars);
            char *lptr = str+i;
            char *rptr = str+i+1;
            while (*rptr != '\0')
                *lptr++ = *rptr++;
            *lptr = '\0';
        }
    }
    return str;
}

static char* remove_leading_chars(char *buf, const char *chars)
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

static void pp_print_parse_error(struct PP *pp, const char *msg)
{
    if (msg != NULL)
        ERROR("%s", msg);

    char lctext[PP_ERR_CHARS_CONTEXT+1] = "";       // buffer for string left from current char
    char rctext[PP_ERR_CHARS_CONTEXT+1] = "";       // buffer for string right from current char

    char *lptr = lctext;
    char *rptr = rctext;

    int j = PP_ERR_CHARS_CONTEXT;

    // get context
    for (int i=0 ; i<PP_ERR_CHARS_CONTEXT ; i++, j--) {

        // check if we go out of left string bounds
        if ((pp->pos.npos - j) >= 0) {
            *lptr = *(pp->pos.c - j);                  // add char to string
            lptr++;
        }
        // TODO ltext and rtext doesn't look into chunks other than current chunk
        // check if we go out of right string bounds
        // BUG this is not bugfree
        if ((pp->pos.npos + i +1) < pp->pos.length) {
            *rptr = *(pp->pos.c + i +1);               // add char to string
            rptr++;
        }
    }
    rctext[PP_ERR_CHARS_CONTEXT] = '\0';
    lctext[PP_ERR_CHARS_CONTEXT] = '\0';

    ERROR("PP syntax error: >%c< @ %d\n", *(pp->pos.c), pp->pos.npos);
    ERROR("\n%s%s%c%s<--%s%s\n", lctext, XRED, *(pp->pos.c), XBLUE, XRESET, rctext);

    //stack_debug(pp);
}

static struct PPItem pp_item_init(enum PPDtype dtype, char *data)
{
    struct PPItem item;
    item.dtype = dtype;
    strncpy(item.data, data, PP_MAX_DATA);
    item.param = NULL;
    return item;
}

int pp_item_sanitize(struct PPItem *item, struct PPParserEntry *pe)
{
    /* Sanitize, clear tag chars etc from item data */
    // remove leading chars
    char *lptr = item->data;
    char *rptr = item->data+(strlen(pe->start));
    while (*rptr != '\0')
        *lptr++ = *rptr++;
    *lptr = '\0';

    // remove trailing chars
    item->data[strlen(item->data) - strlen(pe->end)] = '\0';
    return 0;
}

enum PPParseResult pp_parse_entry(struct PP *pp, struct PPPosition *pos_cpy, struct PPParserEntry *pe)
{
    enum PPSearchResult res_start = pp_str_search_no_buf(&(pp->pos), pe->start, " \n\t", NULL, -1);

    if (res_start != PP_SEARCH_SUCCESS)
        return PP_PARSE_NO_MATCH;

    //DEBUG("Found start string: %s\n", pe->start);

    // rewind to start of found item to capture the whole thing
    pp->pos = pp_pos_copy(pos_cpy);

    // Need to search with buffer here
    char parse_buf[PP_MAX_PARSE_BUFFER] = "";
    enum PPSearchResult res_end = pp_str_search_no_buf(&(pp->pos), pe->end, NULL, parse_buf, PP_MAX_PARSE_BUFFER);

    if (res_end == PP_SEARCH_END_OF_DATA) {
        pp->skip = *pe;
        return PP_PARSE_INCOMPLETE;
    }
    else if (res_end == PP_SEARCH_SYNTAX_ERROR) {
        return PP_PARSE_ERROR;
    }

    char *sanitised = remove_leading_chars(parse_buf, " \n\t");

    struct PPItem item = pp_item_init(pe->dtype, sanitised);

    pp_item_sanitize(&item, pe);

    if (pe->cb != NULL) {
        pe->cb(pp, pe, &item);
    }
    else {
        pp_stack_put(&(pp->stack), item);
        pp->handle_data_cb(pp, item.dtype, pp->user_data);
        pp_stack_pop(&(pp->stack));
    }

    if (pe->greedy == PP_PARSE_METHOD_GREEDY)
        pp_pos_next(&(pp->pos));

    return PP_PARSE_SUCCESS;
}

size_t pp_parse(struct PP *pp, char **chunks, size_t nchunks)
{
    // TODO: when tag ends on same line, trailing slash is still there
    // DONE: In case of buffer overflow, item should have a placeholder
    // TODO: Seperate XML specific stuff into it's own header file (subclass)
    // TODO: Add some integrity checking stuff
    // TODO: Create JSON version
    // TODO: Sererate parameters in XML version
    pp->pos = pp_pos_init(chunks, nchunks);
    size_t nread = 0;

    int passc = 0;


    if (pp->zero_rd_cnt >= pp->pos.max_chunks-1) {
        DEBUG("[%d] Skip to: >%s<\n", pp->zero_rd_cnt, pp->skip.end);

        enum PPSearchResult res = pp_str_search_no_buf(&(pp->pos), pp->skip.end, NULL, NULL, -1);
        if (res == PP_SEARCH_SUCCESS) {
            //INFO("Found: >%s<\n", pp->skip.end);

            // add a placeholder item that represents the item with missing data due to buffer overflow
            struct PPItem item = pp_item_init(pp->skip.dtype, PP_BUFFER_OVERFLOW_PLACEHOLDER);

            if (pp->skip.cb != NULL) {
                pp->skip.cb(pp, &(pp->skip), &item);
            }
            else {
                pp_stack_put(&(pp->stack), item);
                pp->handle_data_cb(pp, item.dtype, pp->user_data);
                pp_stack_pop(&(pp->stack));
            }

            if (pp->skip.greedy == PP_PARSE_METHOD_GREEDY)
                pp_pos_next(&(pp->pos));

            pp->zero_rd_cnt = 0;
            nread = pp->pos.npos;
        }
        else {
            pp->zero_rd_cnt++;
            return pp->pos.npos;
        }
    }

    while (1) {
        struct PPPosition pos_cpy = pp_pos_copy(&(pp->pos));
        struct PPParserEntry *pe = pp->entries;

        for (int i=0 ; i<pp->max_entries ; i++, pe++) {
            enum  PPParseResult res;
            if ((res = pp_parse_entry(pp, &pos_cpy, pe)) == PP_PARSE_INCOMPLETE) {
                if (nread == 0)
                    pp->zero_rd_cnt++;
                else if (nread > 0)
                    pp->zero_rd_cnt = 0;
                return nread;
            }
            else if (res == PP_PARSE_SUCCESS) {
                nread = pp->pos.npos;
                break;
            }
            else if (res == PP_PARSE_NO_MATCH) {
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
