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
static struct PPPosition pp_pos_copy(struct PPPosition *src);

// HELPERS ////////////////////////////

static enum PPSearchResult pp_str_search(struct PPPosition *pos, const char *search_str, const char *ignore_chars, char *save_buf, int save_buf_size)
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
            return PP_SEARCH_RESULT_SUCCESS;

        //if (ignore_chars != NULL && !pp_str_contains_char(buf, ignore_buf))
        //    return PP_SEARCH_RESULT_RESULT_SYNTAX_ERROR;

        if (ignore_chars != NULL && !strchr(ignore_buf, *pos->c))
            return PP_SEARCH_RESULT_SYNTAX_ERROR;


        if (pp_pos_next(pos) < 0)
            return PP_SEARCH_RESULT_END_OF_DATA;

        save_buf_i++;
    }
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

/*
static int pp_str_contains_char(const char *str, const char *chars)
{
    for (int i=0 ; i<strlen(chars) ; i++) {
        DEBUG("%s        %s\n", str, chars);
        if (strchr(str, chars[i]) != NULL)
            return 1;
    }
    return 0;
}

static char* pp_str_rm_leading(char *str, const char *chars)
{
    // Remove, replace and lower a string 
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
*/

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

// STACK ////////////////////////////
void pp_stack_init(struct PPStack *stack)
{
    memset(stack, 0, sizeof(struct PPItem) * PP_MAX_STACK);
    stack->pos = -1;
}

int pp_stack_put(struct PPStack *stack, struct PPItem ji)
{
    ASSERTF(stack->pos <= PP_MAX_STACK -1, "Can't PUT, stack is full!\n");
    ASSERTF(ji.dtype != PP_DTYPE_UNKNOWN, "item has no datatype!\n");
    (stack->pos)++;
    memcpy(&(stack->stack[stack->pos]), &ji, sizeof(struct PPItem));
    return 0;
}

int pp_stack_pop(struct PPStack *stack)
{
    ASSERTF(stack->pos >= 0, "Can't POP, stack is empty!\n");
    //DEBUG("[%d] POP: %s\n", stack->pos, stack->stack[stack->pos].data);
    memset(&(stack->stack[stack->pos]), 0, sizeof(struct PPItem));
    (stack->pos)--;
    return 0;
}

struct PPItem* pp_stack_get_from_end(struct PP *pp, int offset)
{
    if (offset > pp->stack.pos+1)
        return NULL;
    return &(pp->stack.stack[pp->stack.pos - offset]);
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


// PARSER //////////////////////////////
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
    assert(dtype != PP_DTYPE_UNKNOWN); // PPItem should always have a datatype
    struct PPItem item;
    item.dtype = dtype;
    strncpy(item.data, data, PP_MAX_DATA);
    //item.param = NULL;
    memset(&(item.param), 0, PP_XML_MAX_PARAM * sizeof(struct PPXMLParam));
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

void pp_add_parse_entry(struct PP *pp, const char *start, const char *end, enum PPDtype dtype, entry_cb cb, enum PPParseMethod pm)
{
    /* Add a parse entry to the pp struct.
     * Parse entries define the start/end strings when parsing a file
     * eg: when searching for cdata in an XML file this means
     *       start: <![CDATA[
     *       end:   ]]>
     *       capture everything inbetween.
     */
    assert(pp->max_entries+1 <= PP_MAX_PARSER_ENTRIES); // entries max reached!
    pp->max_entries++;
    pp->entries[pp->max_entries-1] = (struct PPParserEntry){start, end, dtype, cb, pm};
}

enum PPParseResult pp_parse_entry(struct PP *pp, struct PPPosition *pos_cpy, struct PPParserEntry *pe)
{
    enum PPSearchResult res_start = pp_str_search(&(pp->pos), pe->start, " \r\n\t", NULL, -1);

    if (res_start != PP_SEARCH_RESULT_SUCCESS)
        return PP_PARSE_RESULT_NO_MATCH;

    // rewind to start of found item to capture the whole thing
    pp->pos = pp_pos_copy(pos_cpy);

    // Need to search with buffer here
    char parse_buf[PP_MAX_PARSE_BUFFER] = "";
    enum PPSearchResult res_end = pp_str_search(&(pp->pos), pe->end, NULL, parse_buf, PP_MAX_PARSE_BUFFER);

    if (res_end == PP_SEARCH_RESULT_END_OF_DATA) {
        pp->skip = *pe;
        pp->skip_is_set = 1;
        return PP_PARSE_RESULT_INCOMPLETE;
    }
    else if (res_end == PP_SEARCH_RESULT_SYNTAX_ERROR) {
        return PP_PARSE_RESULT_ERROR;
    }
    else {
        pp->skip_is_set = 0;
    }

    char *sanitised = remove_leading_chars(parse_buf, " \r\n\t");
    struct PPItem item = pp_item_init(pe->dtype, sanitised);
    pp_item_sanitize(&item, pe);

                                             
    if (pe->cb != NULL) {
        enum PPParseResult cb_res = pe->cb(pp, pe, &item);
        if  (cb_res < PP_PARSE_RESULT_SUCCESS)
            return cb_res;
    }
    else {
        assert(item.dtype != PP_DTYPE_UNKNOWN);  // trying to use uninitialised item
        pp_stack_put(&(pp->stack), item);
        pp->handle_data_cb(pp, item.dtype, pp->user_data);
        pp_stack_pop(&(pp->stack));

    }

    if (pe->greedy == PP_METHOD_GREEDY)
        pp_pos_next(&(pp->pos));

    return PP_PARSE_RESULT_SUCCESS;
}

void pp_xml_stack_debug(struct PPStack *stack)
{
    INFO("STACK CONTENTS\n");
    struct PPItem *xi = stack->stack;

    for (int i=0 ; i<PP_MAX_STACK ; i++, xi++) {
        char dtype[16] = "";
        switch (xi->dtype) {
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

        if (strlen(xi->data) > 0) {
            INFO("%d: dtype: %s  =>  %s\n", i, dtype, xi->data);
        }
        else {
            INFO("%d: dtype: %s\n", i, dtype);
        }
    }
}

size_t pp_parse(struct PP *pp, char **chunks, size_t nchunks)
{
    // DONE: when tag ends on same line, trailing slash is still there
    // DONE: In case of buffer overflow, item should have a placeholder
    // DONE: Seperate XML specific stuff into it's own header file (subclass)
    // TODO: Add some integrity checking stuff
    // TODO: Create JSON version
    // TODO: Sererate parameters in XML version
    pp->pos = pp_pos_init(chunks, nchunks);
    size_t nread = 0;

    int passc = 0;

    if (pp->zero_rd_cnt >= pp->pos.max_chunks-1) {
        assert(pp->skip_is_set == 1);                // trying to skip to skip char but it has no intentional value
                                                     
        DEBUG("[%d] Skip to: >%s<\n", pp->zero_rd_cnt, pp->skip.end);

        enum PPSearchResult res = pp_str_search(&(pp->pos), pp->skip.end, NULL, NULL, -1);
        if (res == PP_SEARCH_RESULT_SUCCESS) {

            // add a placeholder item that represents the item with missing data due to buffer overflow
            struct PPItem item = pp_item_init(pp->skip.dtype, PP_BUFFER_OVERFLOW_PLACEHOLDER);

            if (pp->skip.cb != NULL) {
                enum PPParseResult cb_res = pp->skip.cb(pp, &(pp->skip), &item);
                if  (cb_res < PP_PARSE_RESULT_SUCCESS)
                    return cb_res;
            }
            else {
                pp_stack_put(&(pp->stack), item);
                pp->handle_data_cb(pp, item.dtype, pp->user_data);
                pp_stack_pop(&(pp->stack));
            }

            if (pp->skip.greedy == PP_METHOD_GREEDY)
                pp_pos_next(&(pp->pos));

            pp->skip_is_set = 0;
            pp->zero_rd_cnt = 0;
            nread = pp->pos.npos;
        }
        else if (res == PP_PARSE_RESULT_ERROR) {
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
        struct PPParserEntry *pe = pp->entries;

        for (int i=0 ; i<pp->max_entries ; i++, pe++) {
            enum  PPParseResult res;
            if ((res = pp_parse_entry(pp, &pos_cpy, pe)) == PP_PARSE_RESULT_INCOMPLETE) {
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
