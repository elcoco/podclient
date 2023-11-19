#include "json.h"

#define DO_DEBUG 1
#define DO_INFO  1
#define DO_ERROR 1

#define DEBUG(M, ...) if(DO_DEBUG){fprintf(stdout, "[DEBUG] " M, ##__VA_ARGS__);}
#define INFO(M, ...) if(DO_INFO){fprintf(stdout, M, ##__VA_ARGS__);}
#define ERROR(M, ...) if(DO_ERROR){fprintf(stderr, "[ERROR] (%s:%d) " M, __FILE__, __LINE__, ##__VA_ARGS__);}



struct JSON json_init(void(*handle_data_cb)(struct JSON *json, enum JSONEvent ev, void *user_data))
{
    struct JSON json;
    json.handle_data_cb = handle_data_cb;
    json.stack_pos = -1;
    memset(json.stack, 0, sizeof(json.stack));
    json.user_data = NULL;
    return json;
}

static struct Position pos_init(char **chunks, size_t nchunks)
{
    struct Position pos;
    pos.max_chunks = nchunks;
    pos.chunks = chunks;
    pos.c = pos.chunks[0];
    pos.npos = 0;
    pos.length = strlen(pos.c);
    pos.cur_chunk = 0;
    return pos;
}

static int pos_next(struct Position *pos)
{
    //INFO("char: %c, pos: %d, len= %d\n", *pos->c, pos->npos, pos->length);
    if (pos->npos >= pos->length-1) {
        if (pos->cur_chunk < pos->max_chunks-1 &&  pos->chunks[pos->cur_chunk+1] != NULL) {
            //INFO("Move to next chunk, %ld!\n", pos->cur_chunk+1);
            pos->cur_chunk++;
            pos->npos = 0;
            pos->c = pos->chunks[pos->cur_chunk];
            pos->length = strlen(pos->c);
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

static enum JSONParseResult fforward_skip_escaped(struct Position *pos, char *search_lst, char *expected_lst, char *unwanted_lst, char *ignore_lst, char *buf)
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

    // don't return these chars with buffer
    ignore_lst = (ignore_lst) ? ignore_lst : "";
    unwanted_lst = (unwanted_lst) ? unwanted_lst : "";

    while (1) {
        if (strchr(search_lst, *(pos->c))) {
            // check if previous character whas a backslash which indicates escaped
            if (pos->npos > 0 && *(pos->c-1) == '\\')
                ;
            else
                break;
        }
        if (strchr(unwanted_lst, *(pos->c)))
            return JSON_PARSE_ILLEGAL_CHAR;

        if (expected_lst != NULL) {
            if (!strchr(expected_lst, *(pos->c)))
                return JSON_PARSE_ILLEGAL_CHAR;
        }
        if (buf != NULL && !strchr(ignore_lst, *(pos->c)))
            *ptr++ = *(pos->c);

        if (pos_next(pos) < 0)
            return JSON_PARSE_END_OF_DATA;
    }
    // terminate string
    if (ptr != NULL)
        *ptr = '\0';

    return JSON_PARSE_SUCCESS;
}


static enum JSONParseResult fforward_skip_escaped_destructive(struct Position *pos, char *search_lst, char *expected_lst, char *unwanted_lst, char *ignore_lst)
{
    /* fast forward until a char from search_lst is found
     * Function is destructive, found character is replaced with \0
     * Error is a char from unwanted_lst is found
     *
     * If buf == NULL,          don't save chars
     * If expected_lst == NULL, allow all characters
     * If unwanted_lst == NULL, allow all characters
     */

    // NOTE!!!!!! this can not work when using chunks because when looking for eg a closing quote:
    //
    //      chunk0: abcd\0
    //      chunk1: efghi"
    //
    //      becomes:
    //
    //              |   Pointer moves to 'a'
    //              v
    //      chunk0: abcd\0
    //      chunk1: efghi\0
    //
    //      This would result in the string "abcd" instead of "abcdefghi"
    //      because the string is not copied to a buffer
    
    
    // don't return these chars with buffer
    ignore_lst = (ignore_lst) ? ignore_lst : "";
    unwanted_lst = (unwanted_lst) ? unwanted_lst : "";

    while (1) {
        if (strchr(search_lst, *(pos->c))) {
            // check if previous character whas a backslash which indicates escaped
            if (pos->npos > 0 && *(pos->c-1) == '\\')
                ;
            else
                break;
        }
        if (strchr(unwanted_lst, *(pos->c)))
            return JSON_PARSE_ILLEGAL_CHAR;

        if (expected_lst != NULL) {
            if (!strchr(expected_lst, *(pos->c)))
                return JSON_PARSE_ILLEGAL_CHAR;
        }
        if (pos_next(pos) < 0)
            return JSON_PARSE_END_OF_DATA;
    }
    *pos->c = '\0';

    return JSON_PARSE_SUCCESS;
}

static struct JSONItem json_item_init(enum JSONDtype dtype, char *data)
{
    struct JSONItem ji;
    ji.dtype = dtype;
    strncpy(ji.data, data, JSON_MAX_DATA);
    return ji;
}


/* Stack operations */
static int stack_put(struct JSON *json, struct JSONItem ji)
{
    ASSERTF(json->stack_pos <= JSON_MAX_STACK -1, "Can't PUT, stack is full!\n");

    (json->stack_pos)++;
    memcpy(&(json->stack[json->stack_pos]), &ji, sizeof(struct JSONItem));
    return 0;
}

static int stack_pop(struct JSON *json)
{
    ASSERTF(json->stack_pos >= 0, "Can't POP, stack is empty!\n");

    memset(&(json->stack[json->stack_pos]), 0, sizeof(struct Position));
    (json->stack_pos)--;

    // if previous value was a key, then also remove this item
    if (json->stack_pos >= 0 && json->stack[json->stack_pos].dtype == JSON_DTYPE_KEY)
        stack_pop(json);

    return 0;
}

struct JSONItem* stack_get_from_end(struct JSON *json, int offset)
{
    if (offset > json->stack_pos+1)
        return NULL;
    return &(json->stack[json->stack_pos - offset]);
}

int stack_item_is_type(struct JSON *json, int offset, enum JSONDtype dtype)
{
    /* Look in stack to see if item is an object or not */
    struct JSONItem *ji = stack_get_from_end(json, offset);
    if (ji == NULL)
        return -1;

    return ji->dtype == dtype;
}

void stack_debug(struct JSON *json)
{
    ERROR("STACK CONTENTS\n");
    struct JSONItem *ji = json->stack;

    for (int i=0 ; i<JSON_MAX_STACK ; i++, ji++) {
        char dtype[16] = "";
        switch (ji->dtype) {
            case JSON_DTYPE_KEY:
                strcpy(dtype, "KEY   ");
                break;
            case JSON_DTYPE_OBJECT:
                strcpy(dtype, "OBJECT");
                break;
            case JSON_DTYPE_ARRAY:
                strcpy(dtype, "ARRAY ");
                break;
            case JSON_DTYPE_STRING:
                strcpy(dtype, "STRING");
                break;
            case JSON_DTYPE_NUMBER:
                strcpy(dtype, "NUMBER");
                break;
            case JSON_DTYPE_BOOL:
                strcpy(dtype, "BOOL  ");
                break;
            case JSON_DTYPE_UNKNOWN:
                return;
        }

        if (strlen(ji->data) > 0) {
            ERROR("%d: dtype: %s  =>  %s\n", i, dtype, ji->data);
        }
        else {
            ERROR("%d: dtype: %s\n", i, dtype);
        }
    }
}

static int stack_last_is_key(struct JSON *json)
{
    /* Look in stack to see if this item is a key or not */
    return json->stack_pos >= 0 && json->stack[json->stack_pos].dtype == JSON_DTYPE_KEY;
}

static int stack_last_is_object(struct JSON *json)
{
    /* Look in stack to see if this item is a key or not */
    return json->stack_pos >= 0 && json->stack[json->stack_pos].dtype == JSON_DTYPE_OBJECT;
}

static int stack_last_is_array(struct JSON *json)
{
    /* Look in stack to see if this item is a key or not */
    return json->stack_pos >= 0 && json->stack[json->stack_pos].dtype == JSON_DTYPE_ARRAY;
}
static int stack_last_is_string(struct JSON *json)
{
    /* Look in stack to see if this item is a key or not */
    return json->stack_pos >= 0 && json->stack[json->stack_pos].dtype == JSON_DTYPE_STRING;
}
static int stack_last_is_number(struct JSON *json)
{
    /* Look in stack to see if this item is a key or not */
    return json->stack_pos >= 0 && json->stack[json->stack_pos].dtype == JSON_DTYPE_NUMBER;
}
static int stack_last_is_bool(struct JSON *json)
{
    /* Look in stack to see if this item is a key or not */
    return json->stack_pos >= 0 && json->stack[json->stack_pos].dtype == JSON_DTYPE_BOOL;
}
static int stack_is_empty(struct JSON *json)
{
    /* Look in stack to see if this item is a key or not */
    return json->stack_pos < 0;
}

static void print_parse_error(struct JSON *json, struct Position *pos, const char *msg) {
    if (msg != NULL)
        ERROR("%s", msg);

    char lctext[JSON_ERR_CHARS_CONTEXT+1] = "";       // buffer for string left from current char
    char rctext[JSON_ERR_CHARS_CONTEXT+1] = "";       // buffer for string right from current char

    char *lptr = lctext;
    char *rptr = rctext;

    int j = JSON_ERR_CHARS_CONTEXT;

    // get context
    for (int i=0 ; i<JSON_ERR_CHARS_CONTEXT ; i++, j--) {

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
    rctext[JSON_ERR_CHARS_CONTEXT] = '\0';
    lctext[JSON_ERR_CHARS_CONTEXT] = '\0';

    ERROR("JSON syntax error: >%c< @ %d\n", *(pos->c), pos->npos);
    ERROR("\n%s%s%c%s<--%s%s\n", lctext, JRED, *(pos->c), JBLUE, JRESET, rctext);

    stack_debug(json);
}

enum JSONParseResult json_parse_string(struct JSON *json, struct Position *pos, char *buf)
{
    /* Parse a string that starts and ends with  " or '
     * Can result in a key or a string type
     */
    char quote[2] = "";
    quote[0] = *(pos->c);
    struct JSONItem ji;

    if (pos_next(pos) < 0)
        return JSON_PARSE_INCOMPLETE;

    if (fforward_skip_escaped(pos, quote, NULL, NULL, "\n", buf) < JSON_PARSE_SUCCESS) {
    //if (fforward_skip_escaped(pos, quote, NULL, NULL, "\n", buf) < JSON_PARSE_SUCCESS) {
        DEBUG("Failed to find closing quotes\n");
        return JSON_PARSE_INCOMPLETE;
    }
    if (stack_last_is_key(json) || stack_last_is_array(json))
        ji = json_item_init(JSON_DTYPE_STRING, buf);
    else if (stack_last_is_object(json))
        ji = json_item_init(JSON_DTYPE_KEY, buf);

    stack_put(json, ji);
    if (ji.dtype == JSON_DTYPE_KEY)
        json->handle_data_cb(json, JSON_EV_KEY, json->user_data);
    else
        json->handle_data_cb(json, JSON_EV_STRING, json->user_data);

    if (!stack_last_is_key(json))
        stack_pop(json);

    if (pos_next(pos) < 0)
        return JSON_PARSE_SUCCESS_END_OF_DATA;

    return JSON_PARSE_SUCCESS;
}

static int json_parse_number(struct JSON *json, struct Position *pos, char *buf)
{
    if (fforward_skip_escaped(pos, ", ]}\n", "0123456789-null.", NULL, "\n", buf) < JSON_PARSE_SUCCESS) {
        DEBUG("Failed to find end of number\n");
        return -1;
    }
    DEBUG("FOUND NUMBER: %s\n", buf);
    struct JSONItem ji = json_item_init(JSON_DTYPE_NUMBER, buf);
    stack_put(json, ji);
    json->handle_data_cb(json, JSON_EV_NUMBER, json->user_data);
    stack_pop(json);
    return 0;
}

static int json_parse_bool(struct JSON *json, struct Position *pos, char *buf)
{
    if (fforward_skip_escaped(pos, ", ]}\n", "truefalse", NULL, "\n", buf) < JSON_PARSE_SUCCESS) {
        DEBUG("Failed to find end of boolean: %c\n", *pos->c);
        return -1;
    }
    struct JSONItem ji = json_item_init(JSON_DTYPE_BOOL, buf);
    stack_put(json, ji);
    json->handle_data_cb(json, JSON_EV_BOOL, json->user_data);
    stack_pop(json);
    return 0;
}

void json_handle_data_cb(struct JSON *json, enum JSONEvent ev, void *user_data)
{
    /* Print out json in a sort of structured way */
    const char *space = "  ";

    if (ev == JSON_EV_KEY)
        return;

    struct JSONItem *ji = stack_get_from_end(json, 0);
    ASSERTF(ji != NULL, "Callback received empty stack!");

    for (int i=0 ; i<json->stack_pos ; i++)
        INFO("%s", space);

    if (ev == JSON_EV_OBJECT_START) {
        if (stack_item_is_type(json, 1, JSON_DTYPE_KEY))
            INFO("%s: ", stack_get_from_end(json, 1)->data);

        INFO("OBJECT START\n");
        return;
    }
    else if (ev == JSON_EV_OBJECT_END) {
        INFO("OBJECT END\n");
        return;
    }
    else if (ev == JSON_EV_ARRAY_START) {
        if (stack_item_is_type(json, 1, JSON_DTYPE_KEY)) {
            INFO("%s: ", stack_get_from_end(json, 1)->data);
        }
        INFO("ARRAY START\n");
        return;
    }
    else if (ev == JSON_EV_ARRAY_END) {
        INFO("ARRAY END\n");
        return;
    }

    struct JSONItem *ji_prev = stack_get_from_end(json, 1);
    if (ji_prev == NULL)
        return;

    //INFO("ji_prev (%d) is: %s\n", ji_prev->dtype, (ji_prev == NULL) ? "NULL" : ji_prev->data);
    if (ji_prev != NULL && ji_prev->dtype == JSON_DTYPE_KEY) {
        INFO("%s:\t[%s:%ld] %s\n", ji_prev->data, dtype_map[ji->dtype], strlen(ji->data), ji->data);
    }
    else {
        INFO("[%s] %s\n", dtype_map[ji->dtype], ji->data);
    }

    //DEBUG("Handling data: %s\n", ji->data);
}


size_t json_parse(struct JSON *json, char **chunks, size_t nchunks)
{
    int nread = 0;
    struct Position pos = pos_init(chunks, nchunks);
    //printf("input: >%s<\n", pos.c);

    // TODO set to reasonable size and protected for buffer overflow
    // fforward_skip_escaped should return JSON_PARSE_BUFFER_OVERFLOW
    char tmp[JSON_MAX_PARSE_BUFFER] = "";

    while (1) {
        // Instead of using a buffer, the char that we're searching for could be replaced by \0
        // A pointer to start of string and the found char could be returned
        // So this function would become destructive
        // This should only be done if tmp != NULL
        //
        //stack_debug(json);

        enum JSONParseResult res;
        if ((res = fforward_skip_escaped(&pos, "\"[{1234567890-n.tf}]", NULL, NULL, "\n", tmp)) < JSON_PARSE_SUCCESS) {
            if (res == JSON_PARSE_END_OF_DATA) {
                break;
            }
            else {
                print_parse_error(json, &pos, "Found unexpected character!\n");
                return -1;
            }
        }

        if (*pos.c == '{') {
            if (stack_last_is_object(json)) {
                print_parse_error(json, &pos, "Unexpected start of object\n");
                return -1;
            }
            struct JSONItem ji = json_item_init(JSON_DTYPE_OBJECT, "");
            stack_put(json, ji);
            json->handle_data_cb(json, JSON_EV_OBJECT_START, json->user_data);
            nread = pos.npos;
            if (pos_next(&pos) < 0) {
                nread = pos.npos;
                break;
            }
        }
        else if (*pos.c == '}') {
            if (!stack_last_is_object(json)) {
                print_parse_error(json, &pos, "Unexpected end of object\n");
                return -1;
            }
            json->handle_data_cb(json, JSON_EV_OBJECT_END, json->user_data);
            stack_pop(json);
            nread = pos.npos;
            if (pos_next(&pos) < 0) {
                nread = pos.npos;
                break;
            }
        }
        else if (*pos.c == '[') {
            if (stack_last_is_object(json)) {
                print_parse_error(json, &pos, "Unexpected start of array\n");
                return -1;
            }

            struct JSONItem ji = json_item_init(JSON_DTYPE_ARRAY, "");
            stack_put(json, ji);
            json->handle_data_cb(json, JSON_EV_ARRAY_START, json->user_data);
            nread = pos.npos;
            if (pos_next(&pos) < 0) {
                nread = pos.npos;
                break;
            }
        }
        else if (*pos.c == ']') {
            if (!stack_last_is_array(json)) {
                print_parse_error(json, &pos, "Unexpected end of array\n");
                return -1;
            }
            json->handle_data_cb(json, JSON_EV_ARRAY_END, json->user_data);
            stack_pop(json);
            nread = pos.npos;
            if (pos_next(&pos) < 0) {
                nread = pos.npos;
                break;
            }
        }
        else if (*pos.c == '"' || *pos.c == '\'') {
            enum JSONParseResult res = json_parse_string(json, &pos, tmp);
            if (res == JSON_PARSE_INCOMPLETE) {
                // do not save nread
                break;
            }
            else if ( res == JSON_PARSE_SUCCESS_END_OF_DATA) {
                nread = pos.npos;
                break;
            }
            else if (res == JSON_PARSE_SUCCESS) {
                nread = pos.npos;
            }
            else {
                print_parse_error(json, &pos, "Error while parsing string\n");
                return -1;
            }
        }

        else if (strchr("0123456789-n.", *pos.c)) {
            if (json_parse_number(json, &pos, tmp) < 0)
                break;
            nread = pos.npos;
        }

        else if (strchr("tf", *pos.c)) {
            if (json_parse_bool(json, &pos, tmp) < 0)
                break;
            nread = pos.npos;
        }
        else {
            ERROR("Unhandled: %c\n", *pos.c);
            print_parse_error(json, &pos, NULL);
            return -1;
        }
    }

    return nread+1;
}
