#include "json.h"


struct JSON json_init(void(*handle_data_cb)(struct JSON *json, struct JSONItem *ji))
{
    struct JSON json;
    json.handle_data_cb = handle_data_cb;
    json.stack_pos = -1;
    memset(json.stack, 0, sizeof(json.stack));
    return json;
}

static char* pos_next(struct Position *pos)
{
    (pos->c)++;
    (pos->npos)++;
    return pos->c;
}

static char fforward_skip_escaped(struct Position* pos, char* search_lst, char* expected_lst, char* unwanted_lst, char* ignore_lst, char* buf)
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
    //DEBUG("looking for %s in %s\n", search_lst, pos->c);

    while (1) {
        if (strchr(search_lst, *(pos->c))) {
            // check if previous character whas a backslash which indicates escaped
            if (pos->npos > 0 && *(pos->c-1) == '\\') {
                //DEBUG("ignoring escaped: %c, %c\n", *(pos->c-1), *(pos->c));
            }
            else
                break;
            //DEBUG("found char: %c\n", *(pos->c));
        }
        if (strchr(unwanted_lst, *(pos->c)))
            return -1;

        if (expected_lst != NULL) {
            if (!strchr(expected_lst, *(pos->c)))
                return -1;
        }
        if (buf != NULL && !strchr(ignore_lst, *(pos->c)))
            *ptr++ = *(pos->c);

        pos_next(pos);
    }
    // terminate string
    if (ptr != NULL)
        *ptr = '\0';

    char ret = *(pos->c);

    //if (buf)
    //    DEBUG("!!!!!!!!!!: >%s<\n", buf);
    return ret;
}

static int stack_append(struct JSON *json, struct JSONItem ji)
{
    if (json->stack_pos > JSON_MAX_STACK -1) {
        printf("ERROR: Can not append, stack is full!\n");
        return -1;
    }

    //printf("Appending item to stack\n");
    (json->stack_pos)++;
    memcpy(&(json->stack[json->stack_pos]), &ji, sizeof(struct JSONItem));
    return 0;
}

static int stack_pop(struct JSON *json)
{
    // err is stack is empty
    if (json->stack_pos < 0) {
        printf("ERROR: Can not pop, stack is empty!\n");
        return -1;
    }

    //printf("POP!\n");
    memset(&(json->stack[json->stack_pos]), 0, sizeof(struct Position));
    (json->stack_pos)--;
    return 0;
}

static struct JSONItem json_item_init()
{
    struct JSONItem ji;
    ji.dtype = JSON_UNKNOWN;
    ji.data[0] = '\0';
    return ji;
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
            case JSON_UNKNOWN:
                return;
        }
        printf("%d: dtype: %s  =>  %s\n", i, dtype, ji->data);

    }
}

size_t json_parse_str(struct JSON *json, char *data)
{
    printf("Parsing: %s\n", data);
    char c;

    struct Position pos;
    pos.c = data;
    pos.npos = 0;

    char tmp[256] = "";

    while (pos.npos < strlen(data)) {
        if ((c = fforward_skip_escaped(&pos, "\"[{1234567890-n.tf}]", NULL, NULL, "\n", tmp)) < 0) {
            printf("ERROR, Unexpected character: %d\n", c);
            break;
        }

        if (c == '{') {
            printf("START of object\n");
            struct JSONItem ji = json_item_init();
            ji.dtype = JSON_OBJECT;
            stack_append(json, ji);
            pos_next(&pos);
        }
        else if (c == '}') {
            printf("END of object\n");
            stack_pop(json);
            pos_next(&pos);
        }
        else if (c == '[') {
            printf("START of array\n");
            struct JSONItem ji = json_item_init();
            ji.dtype = JSON_ARRAY;
            stack_append(json, ji);
            pos_next(&pos);
        }
        else if (c == ']') {
            printf("END of array\n");
            stack_pop(json);
            pos_next(&pos);
        }
        else if (c == '"') {
            pos_next(&pos);
            if ((c = fforward_skip_escaped(&pos, "\"", NULL, NULL, "\n", tmp)) < 0) {
                printf("Failed to find closing quotes\n");
                break;
            }

            if (json->stack_pos >= 0 && json->stack[json->stack_pos].dtype == JSON_OBJECT) {
                printf("FOUND KEY: %s\n", tmp);
                struct JSONItem ji = json_item_init();
                ji.dtype = JSON_KEY;
                strncpy(ji.data, tmp, JSON_MAX_DATA);
                stack_append(json, ji);
                stack_debug(json);
            }
            else if (json->stack_pos >= 0 && json->stack[json->stack_pos].dtype == JSON_KEY) {
                printf("FOUND VALUE: %s\n", tmp);
                struct JSONItem ji = json_item_init();
                ji.dtype = JSON_STRING;
                strncpy(ji.data, tmp, JSON_MAX_DATA);
                stack_append(json, ji);
                stack_debug(json);
                stack_pop(json);
                stack_pop(json);
            }
            pos_next(&pos);
        }
        else {
            printf("found: %c\n", *pos.c);
            pos_next(&pos);
        }




    }

    //printf("**********************8\n");
    //for (; nread<strlen(data) ; nread++, pos++) {
    //    printf("%c", *pos);
    //}
    //printf("**********************8\n");



    return pos.npos;

}
