#include "xml.h"

#define DO_DEBUG 1
#define DO_INFO  1
#define DO_ERROR 1

#define DEBUG(M, ...) if(DO_DEBUG){fprintf(stdout, "[DEBUG] " M, ##__VA_ARGS__);}
#define INFO(M, ...) if(DO_INFO){fprintf(stdout, M, ##__VA_ARGS__);}
#define ERROR(M, ...) if(DO_ERROR){fprintf(stderr, "[ERROR] (%s:%d) " M, __FILE__, __LINE__, ##__VA_ARGS__);}

static void stack_debug(struct XML *xml);
static struct XMLPosition pos_init(char **chunks, size_t nchunks);
static int pos_next(struct XMLPosition *pos);
static void print_parse_error(struct XML *xml, struct XMLPosition *pos, const char *msg);

static int stack_put(struct XML *xml, struct XMLItem ji);
static int stack_pop(struct XML *xml);
static void stack_debug(struct XML *xml);

static enum XMLParseResult fforward_skip_escaped(struct XMLPosition *pos, char *search_lst, char *expected_lst, char *unwanted_lst, char *ignore_lst, char *buf, size_t buf_size);

struct XML xml_init(void(*handle_data_cb)(struct XML *xml, enum XMLEvent ev, void *user_data))
{
    struct XML xml;
    xml.handle_data_cb = handle_data_cb;
    xml.stack_pos = -1;
    memset(xml.stack, 0, sizeof(xml.stack));
    xml.user_data = NULL;
    xml.skip_until[0] = '\0';
    xml.zero_rd_cnt = 0;
    return xml;
}

static struct XMLPosition pos_init(char **chunks, size_t nchunks)
{
    struct XMLPosition pos;
    pos.max_chunks = nchunks;
    pos.chunks = chunks;
    pos.c = pos.chunks[0];
    pos.npos = 0;
    pos.length = strlen(pos.c);
    pos.cur_chunk = 0;
    return pos;
}

static int pos_next(struct XMLPosition *pos)
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

static int pos_get_next(struct XMLPosition *pos, int offset)
{
    /* don't move internal pointer, just return char at offset from current cahr */
}

static struct XMLPosition pos_copy(struct XMLPosition *src)
{
    /* Copy struct to new struct */
    struct XMLPosition copy;
    memcpy(&copy, src, sizeof(struct XMLPosition));
    return copy;
}

static struct XMLItem xml_item_init(enum XMLDtype dtype, char *data)
{
    struct XMLItem ji;
    ji.dtype = dtype;
    strncpy(ji.data, data, XML_MAX_DATA);
    ji.param = NULL;
    return ji;
}

static void print_parse_error(struct XML *xml, struct XMLPosition *pos, const char *msg)
{
    if (msg != NULL)
        ERROR("%s", msg);

    char lctext[XML_ERR_CHARS_CONTEXT+1] = "";       // buffer for string left from current char
    char rctext[XML_ERR_CHARS_CONTEXT+1] = "";       // buffer for string right from current char

    char *lptr = lctext;
    char *rptr = rctext;

    int j = XML_ERR_CHARS_CONTEXT;

    // get context
    for (int i=0 ; i<XML_ERR_CHARS_CONTEXT ; i++, j--) {

        // check if we go out of left string bounds
        if ((pos->npos - j) >= 0) {
            *lptr = *(pos->c - j);                  // add char to string
            lptr++;
        }
        // TODO ltext and rtext doesn't look into chunks other than current chunk
        // check if we go out of right string bounds
        // BUG this is not bugfree
        if ((pos->npos + i +1) < pos->length) {
            *rptr = *(pos->c + i +1);               // add char to string
            rptr++;
        }
    }
    rctext[XML_ERR_CHARS_CONTEXT] = '\0';
    lctext[XML_ERR_CHARS_CONTEXT] = '\0';

    ERROR("XML syntax error: >%c< @ %d\n", *(pos->c), pos->npos);
    ERROR("\n%s%s%c%s<--%s%s\n", lctext, XRED, *(pos->c), XBLUE, XRESET, rctext);

    stack_debug(xml);
}


static int stack_put(struct XML *xml, struct XMLItem ji)
{
    ASSERTF(xml->stack_pos <= XML_MAX_STACK -1, "Can't PUT, stack is full!\n");

    (xml->stack_pos)++;
    memcpy(&(xml->stack[xml->stack_pos]), &ji, sizeof(struct XMLItem));
    return 0;
}

static int stack_pop(struct XML *xml)
{
    ASSERTF(xml->stack_pos >= 0, "Can't POP, stack is empty!\n");

    memset(&(xml->stack[xml->stack_pos]), 0, sizeof(struct XMLPosition));
    (xml->stack_pos)--;

    // if previous value was a key, then also remove this item
    //if (xml->stack_pos >= 0 && xml->stack[xml->stack_pos].dtype == XML_DTYPE_KEY)
    //    stack_pop(xml);

    return 0;
}

static void stack_debug(struct XML *xml)
{
    ERROR("STACK CONTENTS\n");
    struct XMLItem *xi = xml->stack;

    for (int i=0 ; i<XML_MAX_STACK ; i++, xi++) {
        char dtype[16] = "";
        switch (xi->dtype) {
            case XML_DTYPE_TAG:
                strcpy(dtype, "TAG   ");
                break;
            case XML_DTYPE_STRING:
                strcpy(dtype, "STRING");
                break;
            case XML_DTYPE_UNKNOWN:
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

struct XMLItem* xml_stack_get_from_end(struct XML *xml, int offset)
{
    if (offset > xml->stack_pos+1)
        return NULL;
    return &(xml->stack[xml->stack_pos - offset]);
}

static enum XMLParseResult fforward_skip_escaped(struct XMLPosition *pos, char *search_lst, char *expected_lst, char *unwanted_lst, char *ignore_lst, char *buf, size_t buf_size)
{
    /* fast forward until a char from search_lst is found
     * Save all chars in buf until a char from search_lst is found
     * Only save in buf when a char is found in expected_lst
     * Error is a char from unwanted_lst is found
     *
     * If buf == NULL,          don't save chars
     * If expected_lst == NULL, allow all characters
     * If unwanted_lst == NULL, allow all characters
     */
    // TODO char can not be -1

    // save skipped chars that are on expected_lst in buffer
    char* ptr = buf;
    //char* ptr = buf + strlen(buf);
    size_t n = 0;

    // don't return these chars with buffer
    ignore_lst = (ignore_lst) ? ignore_lst : "";
    unwanted_lst = (unwanted_lst) ? unwanted_lst : "";

    while (1) {
        // any char that is on ignore list will make func return
        if (search_lst == NULL && !strchr(ignore_lst, *(pos->c)))
            break;

        if (strchr(search_lst, *(pos->c))) {
            // check if previous character whas a backslash which indicates escaped
            if (pos->npos > 0 && *(pos->c-1) == '\\')
                ;
            else
                break;
        }
        if (strchr(unwanted_lst, *(pos->c)))
            return XML_PARSE_UNEXPECTED;

        if (expected_lst != NULL) {
            if (!strchr(expected_lst, *(pos->c)))
                return XML_PARSE_UNEXPECTED;
        }

        if (n >= buf_size) {
            ERROR("Buffer full: %ld\n", n);
            return XML_PARSE_BUFFER_OVERFLOW;
        }

        if (buf != NULL && !strchr(ignore_lst, *(pos->c))) {
            *ptr++ = *(pos->c);
            n++;
        }

        if (pos_next(pos) < 0)
            return XML_PARSE_END_OF_DATA;
    }
    // terminate string
    if (ptr != NULL)
        *ptr = '\0';

    return XML_PARSE_SUCCESS;
}

static enum XMLParseResult str_search(struct XMLPosition *pos, const char *search_str, char *buf, size_t buf_size)
{
    char *ptr = buf;
    size_t n = 0;

    while (1) {

        // for some reason, buf stops filling up after 138 chars
        if (n >= buf_size) {
            ERROR("String search failed, max bufsize reached: %ld\n", n);
            return XML_PARSE_BUFFER_OVERFLOW;
        }

        *ptr++ = *(pos->c);
        n++;
        if (strstr(buf, search_str) != NULL) {
            *ptr = '\0';
            return XML_PARSE_SUCCESS;
        }

        if (pos_next(pos) < 0)
            return XML_PARSE_END_OF_DATA;
    }
    return XML_PARSE_END_OF_DATA;
}

static void print_spaces(int n)
{
    for (int i=0 ; i<n ; i++)
        printf(" ");
}

void xml_handle_data_cb(struct XML *xml, enum XMLEvent ev, void *user_data)
{
    const int spaces = 2;

    struct XMLItem *xi = xml_stack_get_from_end(xml, 0);
    ASSERTF(xi != NULL, "Callback received empty stack!");

    switch (ev) {
        case XML_EV_TAG_START:
            print_spaces(spaces * xml->stack_pos);
            if (xi->param != NULL) {
                INFO("TAG_START: %s, param: %s\n", xi->data, xi->param);
            }
            else {
                INFO("TAG_START: %s\n", xi->data);
            }
            break;
        case XML_EV_TAG_END:
            print_spaces(spaces * xml->stack_pos - spaces);
            INFO("TAG_END:   %s\n", xi->data);
            break;
        case XML_EV_STRING:
            print_spaces(spaces * xml->stack_pos);
            INFO("STRING:   %s\n", xi->data);
            break;
        case XML_EV_HEADER:
            print_spaces(spaces * xml->stack_pos);
            INFO("HEADER:   %s\n", xi->data);
            break;
    }

    //INFO("EVENT: %d = >>%s<<\n", ev, xi->data);
}

static int str_starts_with(const char *str, const char *substr)
{
    return strncmp(substr, str, strlen(substr)) == 0;
}

static int stack_last_is_tag(struct XML *xml)
{
    /* Look in stack to see if this item is a key or not */
    return xml->stack_pos >= 0 && xml->stack[xml->stack_pos].dtype == XML_DTYPE_TAG;
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

static int str_split_at_char(char *str, char c, char **rstr)
{
    /* Replace first occurance of c in str with '\0' char.
     * *rstr is a pointer to the char after '\0'
     * If char is last char in str, *rstr == NULL
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

static enum XMLParseResult xml_parse_header(struct XML *xml, struct XMLPosition *pos, char *buf)
{
    struct XMLItem xi = xml_item_init(XML_DTYPE_STRING, buf);
    stack_put(xml, xi);
    xml->handle_data_cb(xml, XML_EV_HEADER, xml->user_data);
    stack_pop(xml);

    if (pos_next(pos) < 0)
        return XML_PARSE_SUCCESS_END_OF_DATA;

    return XML_PARSE_SUCCESS;
}

static enum XMLParseResult xml_parse_cdata(struct XML *xml, struct XMLPosition *pos, struct XMLPosition *bak, char *buf)
{
    buf[0] = '\0';

    // restore (rewind) postition cause we want to search from start of CDATA
    *pos = pos_copy(bak);
    enum XMLParseResult res;

    if ((res = str_search(pos, XML_CHAR_CDATA_END, buf, XML_MAX_PARSE_BUFFER)) == XML_PARSE_SUCCESS) {
        buf[strlen(buf)-strlen(XML_CHAR_CDATA_END)] = '\0';
        struct XMLItem xi = xml_item_init(XML_DTYPE_STRING, buf+strlen(XML_CHAR_CDATA_START));
        stack_put(xml, xi);
        xml->handle_data_cb(xml, XML_EV_STRING, xml->user_data);
        stack_pop(xml);

        if (pos_next(pos) < 0)
            return XML_PARSE_SUCCESS_END_OF_DATA;

        return XML_PARSE_SUCCESS;
    }
    //ERROR("[%d] Failed to find CDATA end, %s\n", res, buf);

    if (res == XML_PARSE_END_OF_DATA)
        strncpy(xml->skip_until, XML_CHAR_CDATA_END, XML_MAX_SKIP_STR);

    return res;
}

static enum XMLParseResult xml_parse_tag_end(struct XML *xml, struct XMLPosition *pos, char *buf)
{
    struct XMLItem *xi_prev = xml_stack_get_from_end(xml, 0);
    if (xi_prev != NULL && xi_prev->dtype == XML_DTYPE_TAG) {
        if (strstr(buf+1, xi_prev->data) != 0) {
            struct XMLItem xi = xml_item_init(XML_DTYPE_TAG, buf+1);
            stack_put(xml, xi);
            xml->handle_data_cb(xml, XML_EV_TAG_END, xml->user_data);
            stack_pop(xml);
            stack_pop(xml);

            if (pos_next(pos) < 0)
                return XML_PARSE_SUCCESS_END_OF_DATA;

            return XML_PARSE_SUCCESS;
        }
    }
    ERROR("Unexpected closing tag found, cur=%s prev=%s\n", buf+1, xi_prev->data);
    return XML_PARSE_UNEXPECTED;
}

static enum XMLParseResult xml_parse_tag_start(struct XML *xml, struct XMLPosition *pos, char *buf)
{
        char *param;
        struct XMLItem xi;

        if (buf[strlen(buf)-1] == '/') {
            str_split_at_char(buf, ' ', &param);
            xi = xml_item_init(XML_DTYPE_TAG, buf);
            xi.param = param;
            stack_put(xml, xi);
            xml->handle_data_cb(xml, XML_EV_TAG_START, xml->user_data);
            stack_pop(xml);
        }
        else {
            str_split_at_char(buf, ' ', &param);
            xi = xml_item_init(XML_DTYPE_TAG, buf);
            xi.param = param;
            stack_put(xml, xi);
            xml->handle_data_cb(xml, XML_EV_TAG_START, xml->user_data);
        }
        if (pos_next(pos) < 0)
            return XML_PARSE_SUCCESS_END_OF_DATA;

    return XML_PARSE_SUCCESS;
}

static enum XMLParseResult xml_parse_string(struct XML *xml, struct XMLPosition *pos)
{
        char buf[XML_MAX_PARSE_BUFFER] = "";

        enum XMLParseResult res = fforward_skip_escaped(pos, "<", NULL, NULL, "\r\n", buf, XML_MAX_PARSE_BUFFER);

        // This also errors if there is no more data at all. In this case it is not an error
        if ( res < XML_PARSE_SUCCESS) {

            if (res == XML_PARSE_END_OF_DATA)
                strncpy(xml->skip_until, "<", XML_MAX_SKIP_STR);

            return res;
        }

        char *ptr = remove_leading_chars(buf, " \n\t");

        if (strlen(ptr) > 0) {
            struct XMLItem xi = xml_item_init(XML_DTYPE_STRING, buf);
            stack_put(xml, xi);
            xml->handle_data_cb(xml, XML_EV_STRING, xml->user_data);
            stack_pop(xml);
        }
        return XML_PARSE_SUCCESS;
}

static enum XMLParseResult parse_tag(struct XML *xml, struct XMLPosition *pos)
{
    char buf[XML_MAX_PARSE_BUFFER] = "";

    if (pos_next(pos) < 0)
        return XML_PARSE_INCOMPLETE;

    // backup postition so we can rewind later
    struct XMLPosition bak = pos_copy(pos);

    enum XMLParseResult res;
    if ((res = fforward_skip_escaped(pos, ">", NULL, NULL, "\n", buf, XML_MAX_PARSE_BUFFER)) < XML_PARSE_SUCCESS) {
        //DEBUG("Failed to find closing character in tag\n");
        if (res == XML_PARSE_END_OF_DATA)
            strncpy(xml->skip_until, ">", XML_MAX_SKIP_STR);

        return XML_PARSE_INCOMPLETE;
    }

    if (str_starts_with(buf, XML_CHAR_HEADER_START))
        return xml_parse_header(xml, pos, buf);
    
    else if (str_starts_with(buf, XML_CHAR_CDATA_START))
        return xml_parse_cdata(xml, pos, &bak, buf);

    else if (strlen(buf) > 0 && *buf == '/')
        return xml_parse_tag_end(xml, pos, buf);

    else
        return xml_parse_tag_start(xml, pos, buf);
}

size_t xml_parse(struct XML *xml, char **chunks, size_t nchunks)
{
    int nread = 0;
    struct XMLPosition pos = pos_init(chunks, nchunks);

    if (xml->zero_rd_cnt >= 1) {
        ASSERTF(strlen(xml->skip_until) > 0, "zero_rd_cnt reached treshold but skip_until string is empty\n");
        DEBUG("[%d] Skip to: >%s<\n", xml->zero_rd_cnt, xml->skip_until);
        char buf[XML_MAX_PARSE_BUFFER] = "";

        enum XMLParseResult res = str_search(&pos, xml->skip_until, buf, XML_MAX_PARSE_BUFFER);
        if (res != XML_PARSE_SUCCESS) {
            ERROR("Skip until not found\n");
            xml->zero_rd_cnt++;
            return XML_MAX_PARSE_BUFFER;
        }
        else {
            INFO("Found end\n");
            xml->skip_until[0] = '\0';
            xml->zero_rd_cnt = 0;
        }
    }
    // if parse result is incomplete, we should just stop saving chars and just skip to the known end.
    // in case of CDATA that would be  "]]"
    // in case of STRING that would be '<' or End of data
    // in case of TAG that would be    '>'

    while (1) {

        if (*pos.c == '<') {
            enum XMLParseResult tag_res = parse_tag(xml, &pos);
            if (tag_res == XML_PARSE_INCOMPLETE) {
                break;
            }
            else if (tag_res == XML_PARSE_SUCCESS_END_OF_DATA) {
                nread = pos.npos;
                break;
            }
            else if (tag_res == XML_PARSE_SUCCESS) {
                nread = pos.npos;
                continue;
            }
            else if (tag_res == XML_PARSE_END_OF_DATA) {
                break;
            }
            else if (tag_res == XML_PARSE_BUFFER_OVERFLOW) {
                ERROR("Failed to parse TAG due to smoll parse buffer\n");
                //break;
                return -1;
            }
            else {
                print_parse_error(xml, &pos, "Failed to parse tag\n");
                return -1;
            }
        }
        else {
            enum XMLParseResult str_res = xml_parse_string(xml, &pos);
            //DEBUG("res = %d\n", str_res);
            if (str_res == XML_PARSE_SUCCESS)
                nread = pos.npos;
            else if (str_res == XML_PARSE_SUCCESS_END_OF_DATA) {
                nread = pos.npos;
                break;
            }
            else if (str_res == XML_PARSE_INCOMPLETE) {
                break;
            }
            else if (str_res == XML_PARSE_END_OF_DATA) {
                break;
            }
            else if (str_res == XML_PARSE_BUFFER_OVERFLOW) {
                ERROR("Failed to parse STRING due to smoll parse buffer\n");
                //break;
                return -1;
            }
            else {
                DEBUG("res = %d\n", str_res);
                print_parse_error(xml, &pos, "Failed to parse string\n");
                return -1;
            }

        }


        //if (pos_next(&pos) < 0)
        //    break;

    }
    if (nread == 0) {
        xml->zero_rd_cnt++;
    }
    else if (nread > 0) {
        xml->skip_until[0] = '\0';
        xml->zero_rd_cnt = 0;
    }
        
    return nread;
}
