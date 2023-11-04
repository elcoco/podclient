#include "json.h"

#define DO_DEBUG 0
#define DO_INFO  1
#define DO_ERROR 1

#define DEBUG(M, ...) if(DO_DEBUG){fprintf(stdout, "[DEBUG] " M, ##__VA_ARGS__);}
#define INFO(M, ...) if(DO_INFO){fprintf(stdout, M, ##__VA_ARGS__);}
#define ERROR(M, ...) if(DO_ERROR){fprintf(stderr, "[ERROR] (%s:%d) " M, __FILE__, __LINE__, ##__VA_ARGS__);}

#define ASSERTF(A, M, ...) if(!(A)) {ERROR(M, ##__VA_ARGS__); assert(A); }


struct JSON json_init(void(*handle_data_cb)(struct JSON *json, struct JSONItem *ji))
{
    struct JSON json;
    json.handle_data_cb = handle_data_cb;
    json.stack_pos = -1;
    memset(json.stack, 0, sizeof(json.stack));
    return json;
}

struct Position pos_init(char **chunks, size_t nchunks)
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
    //INFO("pos: %d, len= %d\n", pos->npos, pos->length);
    if (pos->npos >= pos->length-1) {
        if (pos->cur_chunk < pos->max_chunks-1 &&  pos->chunks[pos->cur_chunk+1] != NULL) {
            INFO("Move to next chunk, %ld!\n", pos->cur_chunk+1);
            pos->cur_chunk++;
            pos->npos = 0;
            pos->c = pos->chunks[pos->cur_chunk];
            pos->length = strlen(pos->c);
            return 0;
        }
        else {
            INFO("No more chunks!\n");
            return -1;
        }
    }
    (pos->c)++;
    (pos->npos)++;
    return 0;
}

static enum JSONParseResult fforward_skip_escaped(struct Position* pos, char* search_lst, char* expected_lst, char* unwanted_lst, char* ignore_lst, char* buf)
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
            if (pos->npos > 0 && *(pos->c-1) == '\\') {
            }
            else
                break;
        }
        if (strchr(unwanted_lst, *(pos->c)))
            return JSON_PARSE_ILLEGAL_CHAR;

        if (expected_lst != NULL) {
            if (!strchr(expected_lst, *(pos->c)))
                return JSON_PARSE_UNEXPECTED_CHAR;
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

static struct JSONItem json_item_init(enum JSONDtype dtype, char *data)
{
    struct JSONItem ji;
    ji.dtype = dtype;
    strncpy(ji.data, data, JSON_MAX_DATA);
    return ji;
}

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
    if (json->stack_pos >= 0 && json->stack[json->stack_pos].dtype == JSON_KEY)
        stack_pop(json);

    return 0;
}

void stack_debug(struct JSON *json)
{
    struct JSONItem *ji = json->stack;

    for (int i=0 ; i<JSON_MAX_STACK ; i++, ji++) {
        char dtype[16] = "";
        switch (ji->dtype) {
            case JSON_KEY:
                strcpy(dtype, "KEY   ");
                break;
            case JSON_OBJECT:
                strcpy(dtype, "OBJECT");
                break;
            case JSON_ARRAY:
                strcpy(dtype, "ARRAY ");
                break;
            case JSON_STRING:
                strcpy(dtype, "STRING");
                break;
            case JSON_NUMBER:
                strcpy(dtype, "NUMBER");
                break;
            case JSON_BOOL:
                strcpy(dtype, "BOOL  ");
                break;
            case JSON_UNKNOWN:
                return;
        }

        if (strlen(ji->data) > 0) {
            DEBUG("%d: dtype: %s  =>  %s\n", i, dtype, ji->data);
        }
        else {
            DEBUG("%d: dtype: %s\n", i, dtype);
        }

    }
}

static int stack_last_is_key(struct JSON *json)
{
    /* Look in stack to see if this item is a key or not */
    return json->stack_pos >= 0 && json->stack[json->stack_pos].dtype == JSON_KEY;
}

static int stack_last_is_object(struct JSON *json)
{
    /* Look in stack to see if this item is a key or not */
    return json->stack_pos >= 0 && json->stack[json->stack_pos].dtype == JSON_OBJECT;
}

static int stack_last_is_array(struct JSON *json)
{
    /* Look in stack to see if this item is a key or not */
    return json->stack_pos >= 0 && json->stack[json->stack_pos].dtype == JSON_ARRAY;
}
static int stack_last_is_string(struct JSON *json)
{
    /* Look in stack to see if this item is a key or not */
    return json->stack_pos >= 0 && json->stack[json->stack_pos].dtype == JSON_STRING;
}
static int stack_last_is_number(struct JSON *json)
{
    /* Look in stack to see if this item is a key or not */
    return json->stack_pos >= 0 && json->stack[json->stack_pos].dtype == JSON_NUMBER;
}
static int stack_last_is_bool(struct JSON *json)
{
    /* Look in stack to see if this item is a key or not */
    return json->stack_pos >= 0 && json->stack[json->stack_pos].dtype == JSON_BOOL;
}
static int stack_is_empty(struct JSON *json)
{
    /* Look in stack to see if this item is a key or not */
    return json->stack_pos < 0;
}

static void print_parse_error(struct Position *pos, const char *msg) {
    if (msg != NULL)
        ERROR("%s", msg);

    char lctext[JSON_ERR_CHARS_CONTEXT+1];       // buffer for string left from current char
    char rctext[JSON_ERR_CHARS_CONTEXT+1];       // buffer for string right from current char

    char *lptr = lctext;
    char *rptr = rctext;

    // get context
    for (int i=0,j=JSON_ERR_CHARS_CONTEXT ; i<JSON_ERR_CHARS_CONTEXT ; i++, j--) {

        // check if we go out of left string bounds
        if ((pos->npos - j) >= 0) {
            *lptr = *(pos->c - j);                  // add char to string
            lptr++;
        }
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
}

static int json_parse_string(struct JSON *json, struct Position *pos, char *buf)
{
    /* Parse a string that starts and ends with  " or '
     * Can result in a key or a string type
     */
    char quote[2] = "";
    quote[0] = *(pos->c);
    struct JSONItem ji;
    if (pos_next(pos) < 0)
        return -1;

    if (fforward_skip_escaped(pos, quote, NULL, NULL, "\n", buf) < JSON_PARSE_SUCCESS) {
        ERROR("Failed to find closing quotes\n");
        return -1;
    }
    DEBUG("Found STRING: %s\n", buf);

    if (stack_last_is_key(json) || stack_last_is_array(json))
        ji = json_item_init(JSON_STRING, buf);
    else if (stack_last_is_object(json))
        ji = json_item_init(JSON_KEY, buf);

    json->handle_data_cb(json, &ji);
    stack_put(json, ji);
    if (pos_next(pos) < 0)
        return -1;

    stack_debug(json);

    if (!stack_last_is_key(json))
        stack_pop(json);

    return 0;
}

static int json_parse_number(struct JSON *json, struct Position *pos, char *buf)
{
    if (fforward_skip_escaped(pos, ", ]}\n", "0123456789-null.", NULL, "\n", buf) < JSON_PARSE_SUCCESS) {
        print_parse_error(pos, "Failed to find end of number\n");
        return -1;
    }
    DEBUG("FOUND NUMBER: %s\n", buf);
    struct JSONItem ji = json_item_init(JSON_NUMBER, buf);
    json->handle_data_cb(json, &ji);
    stack_put(json, ji);
    stack_debug(json);
    stack_pop(json);
    return 0;
}

static int json_parse_bool(struct JSON *json, struct Position *pos, char *buf)
{
    if (fforward_skip_escaped(pos, ", ]}\n", "truefalse", NULL, "\n", buf) < JSON_PARSE_SUCCESS) {
        ERROR("Unexpected character while parsing boolean: %c\n", *pos->c);
        print_parse_error(pos, NULL);
        return -1;
    }
    struct JSONItem ji = json_item_init(JSON_BOOL, buf);
    json->handle_data_cb(json, &ji);
    stack_put(json, ji);
    stack_debug(json);
    stack_pop(json);
    return 0;
}


size_t json_parse(struct JSON *json, char **chunks, size_t nchunks)
{
    //DEBUG("Parsing: %s\n", data);

    int nread = 0;
    struct Position pos = pos_init(chunks, nchunks);
    printf("input: >%s<\n", pos.c);

    // TODO set to reasonable size and protected for buffer overflow
    // fforward_skip_escaped should return JSON_PARSE_BUFFER_OVERFLOW
    char tmp[2048] = "";

    while (1) {

        enum JSONParseResult res;
        if ((res = fforward_skip_escaped(&pos, "\"[{1234567890-n.tf}]", NULL, NULL, "\n", tmp)) < JSON_PARSE_SUCCESS) {
            if (res == JSON_PARSE_END_OF_DATA) {
                break;
            }
            else {
                print_parse_error(&pos, "Found unexpected character!\n");
                return -1;
            }
        }

        if (*pos.c == '{') {
            if (stack_last_is_object(json)) {
                print_parse_error(&pos, "Unexpected start of object\n");
                return -1;
            }
            DEBUG("START of object\n");
            struct JSONItem ji = json_item_init(JSON_OBJECT, "");
            stack_put(json, ji);
            if (pos_next(&pos) < 0)
                break;
        }
        else if (*pos.c == '}') {
            if (!stack_last_is_object(json)) {
                print_parse_error(&pos, "Unexpected end of object\n");
                return -1;
            }
            DEBUG("END of object\n");
            stack_pop(json);
            if (pos_next(&pos) < 0)
                break;
        }
        else if (*pos.c == '[') {
            if (stack_last_is_object(json)) {
                print_parse_error(&pos, "Unexpected start of array\n");
                return -1;
            }

            DEBUG("START of array\n");
            struct JSONItem ji = json_item_init(JSON_ARRAY, "");
            stack_put(json, ji);
            if (pos_next(&pos) < 0)
                break;
        }
        else if (*pos.c == ']') {
            DEBUG("END of array\n");
            if (!stack_last_is_array(json)) {
                print_parse_error(&pos, "Unexpected end of array\n");
                return -1;
            }
            stack_pop(json);
            if (pos_next(&pos) < 0)
                break;
        }
        else if (*pos.c == '"' || *pos.c == '\'') {
            if (json_parse_string(json, &pos, tmp) < 0)
                break;
        }

        else if (strchr("0123456789-n.", *pos.c)) {
            if (json_parse_number(json, &pos, tmp) < 0)
                break;
        }

        else if (strchr("tf", *pos.c)) {
            if (json_parse_bool(json, &pos, tmp) < 0)
                break;
        }
        else {
            ERROR("Unhandled: %c\n", *pos.c);
            print_parse_error(&pos, NULL);
            return -1;
        }
        nread = pos.npos;
    }

    return nread;
}
