#include "json.h"

#define clean_errno() (errno == 0 ? "None" : strerror(errno))
#define log_error(M, ...) fprintf(stderr, "[ERROR] (%s:%d: errno: %s) " M "\n", __FILE__, __LINE__, clean_errno(), ##__VA_ARGS__)
#define assertf(A, M, ...) if(!(A)) {log_error(M, ##__VA_ARGS__); assert(A); }

#define DEBUG(M, ...) if(JSON_DEBUG){fprintf(stdout, "[DEBUG] " M, ##__VA_ARGS__);}
#define INFO(M, ...) if(JSON_INFO){fprintf(stdout, M, ##__VA_ARGS__);}
#define ERROR(M, ...) if(JSON_ERROR){fprintf(stderr, "[ERROR] (%s:%d) " M, __FILE__, __LINE__, ##__VA_ARGS__);}


static enum JSONStatus json_parse(struct JSONObject* jo, struct Position* pos);
static enum JSONStatus json_parse_object(struct JSONObject* jo, struct Position* pos);
static enum JSONStatus json_parse_array(struct JSONObject* jo, struct Position* pos);
static enum JSONStatus json_parse_string(struct JSONObject* jo, struct Position* pos, char quote_chr);
static enum JSONStatus json_parse_key(struct JSONObject* jo, struct Position* pos);
static enum JSONStatus json_parse_bool(struct JSONObject* jo, struct Position* pos);
static enum JSONStatus json_parse_number(struct JSONObject* jo, struct Position* pos);
static char fforward_skip_escaped_grow(struct Position* pos, char* search_lst, char* expected_lst, char* unwanted_lst, char* ignore_lst, char** buf);
static int count_backslashes(struct Position *pos);
static char fforward_skip_escaped(struct Position* pos, char* search_lst, char* expected_lst, char* unwanted_lst, char* ignore_lst, char* buf);
static char* pos_next(struct Position *pos);
static void print_parse_error(struct Position *pos);
static void get_spaces(char *buf, uint8_t spaces);
static int is_last_item(struct JSONObject *jo);
static void json_object_to_string_rec(struct JSONObject *jo, char **buf, uint32_t level, int spaces);
static void write_to_string(char **buf, char *fmt, ...);



static int is_last_item(struct JSONObject *jo)
{
    /* Check if object is last in an array or object to see
     * if we need to use a comma in json render */

    // is last in array/object
    if (jo->parent != NULL && (jo->parent->is_array || jo->parent->is_object))
        return jo->next == NULL;

    // is rootnode
    if (jo->parent == NULL && jo->length >= 0)
        return 1;
    return 0;
}

static void write_to_string(char **buf, char *fmt, ...)
{
    /* Allocate and write to string, return new size of allocated space */
    int chunk_size = 256;
    char src_buf[512] = "";

    va_list ptr;
    va_start(ptr, fmt);
    vsprintf(src_buf, fmt, ptr);
    va_end(ptr);

    if (*buf == NULL) {
        *buf = malloc(chunk_size);
        strcpy(*buf, "");
    }

    size_t old_nchunks = (strlen(*buf) / chunk_size) + 1;
    size_t new_nchunks = ((strlen(*buf) + strlen(src_buf)) / chunk_size) + 1;

    if (old_nchunks < new_nchunks)
        *buf = realloc(*buf, new_nchunks * chunk_size);

    strncat(*buf, src_buf, strlen(src_buf));
}

static void json_object_to_string_rec(struct JSONObject *jo, char **buf, uint32_t level, int spaces)
{
    /* The function that does the recursive string stuff */
    char space[level+1];
    get_spaces(space, level);

    if (jo != NULL) {
        write_to_string(buf, "%s", space);

        if (jo->key)
            write_to_string(buf, "\"%s\": ", jo->key);

        switch (jo->dtype) {

            case JSON_NUMBER:
                write_to_string(buf, "%f", json_get_number(jo));
                break;

            case JSON_STRING:
                write_to_string(buf, "\"%s\"", json_get_string(jo));
                break;

            case JSON_BOOL:
                write_to_string(buf, "%s", json_get_bool(jo) ? "true" : "false");
                break;

            case JSON_ARRAY:
                write_to_string(buf, "[\n");
                json_object_to_string_rec(jo->value, buf, level+spaces, spaces);
                write_to_string(buf, "%s]", space);

                break;

            case JSON_OBJECT:
                write_to_string(buf, "{\n");
                json_object_to_string_rec(jo->value, buf, level+spaces, spaces);
                write_to_string(buf, "%s}", space);
                break;

            case JSON_UNKNOWN:
                //ERROR("%s[UNKNOWN]%s\n", buf, JCOL_UNKNOWN, JRESET);
                break;
        }
        if (!is_last_item(jo))
            write_to_string(buf, ",\n");
        else
            write_to_string(buf, "\n");

        if (jo->next != NULL)
            json_object_to_string_rec(jo->next, buf, level, spaces);
    }
}

static int json_atoi_err(char *str, int *buf)
{
    char *endptr;

    *buf = strtol(str, &endptr, 0);
    if (endptr == str)
        return -1;
    return 1;
}

int json_count_children(struct JSONObject *parent)
{
    /* Count children of object or array */
    int count = 0;
    struct JSONObject *jo = parent->value;
    while (jo != NULL) {
        count++;
        jo = jo->next;
    }
    return count;
}

/* create string of n amount of spaces */
static void get_spaces(char *buf, uint8_t spaces) {
    uint8_t i;
    for (i=0 ; i<spaces ; i++) {
        buf[i] = ' ';
    }
    buf[i] = '\0';
}

/* print context for error message */
static void print_parse_error(struct Position *pos) {
    char lctext[LINES_CONTEXT+1];       // buffer for string left from current char
    char rctext[LINES_CONTEXT+1];       // buffer for string right from current char

    char *lptr = lctext;
    char *rptr = rctext;

    // get context
    for (int i=0,j=LINES_CONTEXT ; i<LINES_CONTEXT ; i++, j--) {

        // check if we go out of left string bounds
        if ((pos->npos - j) >= 0) {
            *lptr = *(pos->c - j);                  // add char to string
            lptr++;
        }
        // check if we go out of right string bounds
        // BUG this is not bugfree
        if ((pos->npos + i +1) < strlen(pos->json)) {
            *rptr = *(pos->c + i +1);               // add char to string
            rptr++;
        }
    }
    rctext[LINES_CONTEXT] = '\0';
    lctext[LINES_CONTEXT] = '\0';

    ERROR("JSON syntax error: >%c< @ (%d,%d)\n", *(pos->c), pos->rows, pos->cols);
    ERROR("\n%s%s%c%s<--%s%s\n", lctext, JRED, *(pos->c), JBLUE, JRESET, rctext);
}

static char* pos_next(struct Position *pos)
{
    /* Increment struct position in json string */
    // keep track of rows/cols position
    if (*(pos->c) == '\n') {
        pos->rows += 1;
        pos->cols = 0;
    } else {
        (pos->cols)++;
    }
    (pos->npos)++;
    (pos->c)++;

    // NOTE: EOF should not be reached under normal conditions.
    //       This indicates corrupted JSON
    if (pos->npos >= pos->length) {
        // check if character is printable
        if (*(pos->c) >= 32 && *(pos->c) <= 126)
            ERROR("EOL @ c=%c, pos: %d, cxr: %dx%d\n", *(pos->c), pos->npos, pos->cols, pos->rows);
        //else
        //    ERROR("EOL @ (unprintable), pos: %d, cxr: %dx%d\n", pos->npos, pos->cols, pos->rows);
        return NULL;
    }
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

static int count_backslashes(struct Position *pos)
{
    int count = 0;

    while (*(pos->c - (count+1)) == '\\' && pos->npos >= count)
        count++;

    return count;
}

static char fforward_skip_escaped_grow(struct Position* pos, char* search_lst, char* expected_lst, char* unwanted_lst, char* ignore_lst, char** buf)
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
    int grow_amount = 256;
    int buf_size = 0;
    int buf_pos = 0;

    if (buf != NULL) {
        buf_size = grow_amount;
        *buf = malloc(buf_size);
    }

    // don't return these chars with buffer
    ignore_lst = (ignore_lst) ? ignore_lst : "";
    unwanted_lst = (unwanted_lst) ? unwanted_lst : "";

    while (1) {
        if (buf != NULL && buf_pos >= buf_size) {
            buf_size += grow_amount;
            *buf = realloc(*buf, buf_size+1);
        }

        if (strchr(search_lst, *(pos->c))) {
            // if a character is found that is on the search list we have to check
            // if the character is escaped. So we need to look back to check for
            // an uneven amount of backslashes
            if (count_backslashes(pos) % 2 == 0)
                break;
        }
        if (strchr(unwanted_lst, *(pos->c))) {
            goto cleanup_on_err;
        }

        if (expected_lst != NULL) {
            if (!strchr(expected_lst, *(pos->c)))
                goto cleanup_on_err;
        }
        if (buf != NULL && !strchr(ignore_lst, *(pos->c)))
            (*buf)[buf_pos] = *(pos->c);

        pos_next(pos);
        buf_pos++;
    }
    // terminate string
    if (buf != NULL)
        (*buf)[buf_pos] = '\0';

    char ret = *(pos->c);
    return ret;

cleanup_on_err:
    if (buf != NULL)
        free(buf);
    return -1;
}

struct JSONObject* json_get_child_by_index(struct JSONObject *parent, int index)
{
    /* Find child by index.
     * If index == -1, get last child
     * If index  > length, get NULL
     */
    int count = 0;
    struct JSONObject *jo = parent->value;
    while (jo != NULL) {
        if (count == index && index >= 0)
            return jo;
        jo = jo->next;
        count++;
    }
    return NULL;
}

void json_object_append_child(struct JSONObject *parent, struct JSONObject *child)
{
    /* we are not creating array here but in json_parse_object().
     * So we should either come up with a solution or drop the functionality
     */
    DEBUG("Appending child\n");
    assertf(parent != NULL, "parent == NULL");
    assertf(child != NULL, "child == NULL");

    parent->length++;
    child->parent = parent;

    // if no children in parent
    if (parent->value == NULL) {

        // child is first node in linked list
        parent->value = child;
        child->prev = NULL;
        child->next = NULL;
        //child->index = 0;
    }
    else {
        // add to end of linked list
        struct JSONObject *prev = parent->value;

        // get last item in ll
        while (prev->next != NULL)
            prev = prev->next;

        prev->next = child;
        child->prev = prev;
        child->next = NULL;
        //child->index = child->prev->index + 1;
    }
}

struct JSONObject* json_object_insert_child(struct JSONObject *parent, struct JSONObject *child, int index)
{
    assertf(parent != NULL, "parent == NULL");
    assertf(child != NULL, "child == NULL");

    child->parent = parent;

    // parent has no children
    if (parent->value == NULL) {
        DEBUG("first child\n");

        // child is first node in linked list
        parent->value = child;
        child->prev = NULL;
        child->next = NULL;
    }
    else {
        // child is new head node
        if (index == 0) {

            // get previous head;
            struct JSONObject *next = parent->value;

            child->prev = NULL;
            child->next = next;

            if (next)
                next->prev = child;

            parent->value = child;
        }
        else {
            struct JSONObject *prev = json_get_child_by_index(parent, index-1);
            if (prev == NULL)
                RET_NULL("No child at index %d\n", index);

            struct JSONObject *next = prev->next;
            prev->next = child;
            child->prev = prev;

            child->next = next;
            if (next != NULL)
                next->prev = child;

            //child->index = index;
        }
    }
    parent->length = json_count_children(parent);
    return child;
}

static struct JSONObject* json_object_replace_child_at_index(struct JSONObject *parent, struct JSONObject *child, int index)
{
    DEBUG("Replacing child at index: %d\n", index);
    assertf(parent != NULL, "parent == NULL");
    assertf(child != NULL, "child == NULL");

    struct JSONObject *old = json_get_child_by_index(parent, index);
    if (old == NULL)
        RET_NULL("Failed to find old child at index %d", index);

    json_object_destroy(old);

    if (json_object_insert_child(parent, child, index) == NULL)
        return NULL;


    return child;
}

static enum JSONStatus json_parse_number(struct JSONObject* jo, struct Position* pos)
{
    /* All numbers are floats */
    char tmp[MAX_BUF] = {'\0'};
    char c;

    jo->dtype = JSON_NUMBER;
    jo->is_number = true;

    if ((c = fforward_skip_escaped(pos, ", ]}\n", "0123456789-null.", NULL, "\n", tmp)) < 0) {
        ERROR("Unexpected character while parsing number: %c\n", *pos->c);
        print_parse_error(pos);
        return PARSE_ERROR;
    }
    //jo->value = strdup(tmp);
    double* value = malloc(sizeof(double));

    // need to set locale because atof needs to know what char is the decimal points/comma
    setlocale(LC_NUMERIC,"C");

    *value = atof(tmp);
    jo->value = value;
    return STATUS_SUCCESS;
}

static enum JSONStatus json_parse_bool(struct JSONObject* jo, struct Position* pos)
{
    char tmp[MAX_BUF] = {'\0'};
    char c;

    jo->dtype = JSON_BOOL;
    jo->is_bool = true;

    if ((c = fforward_skip_escaped(pos, ", ]}\n", "truefalse", NULL, "\n", tmp)) < 0) {
        ERROR("Unexpected character while parsing boolean: %c\n", *pos->c);
        print_parse_error(pos);
        return PARSE_ERROR;
    }
    bool* value = malloc(sizeof(bool));

    if (strcmp(tmp, "true") == 0)
        *value = true;
    else if (strcmp(tmp, "false") == 0)
        *value = false;
    else
        return PARSE_ERROR;

    jo->value = value;
    return STATUS_SUCCESS;
}

static enum JSONStatus json_parse_key(struct JSONObject* jo, struct Position* pos)
{
    /* Parse key part of an object */
    char c;

    // skip to start of key
    if ((c = fforward_skip_escaped(pos, "\"'}", ", \n\t", NULL, "\n", NULL)) < 0) {
        ERROR("Unexpected character while finding key: %c\n", *pos->c);
        print_parse_error(pos);
        return PARSE_ERROR;
    }
    if (c == '}') {
        pos_next(pos);
        return END_OF_OBJECT;
    }

    pos_next(pos);

    // read key
    if ((c = fforward_skip_escaped_grow(pos, "\"'", NULL, NULL, "\n", &(jo->key))) < 0) {
        ERROR("Unexpected character while parsing key: %c\n", *pos->c);
        print_parse_error(pos);
        return PARSE_ERROR;
    }
    //jo->key = key;
    if (c == '}') {
        pos_next(pos);
        return END_OF_OBJECT;
    }
    
    // skip over "
    pos_next(pos);

    // find colon
    if ((c = fforward_skip_escaped(pos, ":", " \n", NULL, "\n", NULL)) < 0) {
        ERROR("Unexpected character while finding colon in object: %c\n", *pos->c);
        print_parse_error(pos);
        return PARSE_ERROR;
    }

    // skip over colon
    pos_next(pos);

    //DEBUG("json read key: %s\n", key);
    return STATUS_SUCCESS;
}

static enum JSONStatus json_parse_string(struct JSONObject* jo, struct Position* pos, char quote_chr)
{
    char c;

    jo->dtype = JSON_STRING;
    jo->is_string = true;

    // look for closing quotes, quote_chr tells us if it is " or ' that we're looking for
    if (quote_chr == '\'') {
        if ((c = fforward_skip_escaped_grow(pos, "'\n", NULL, NULL, "\n", (char**)&(jo->value))) < 0) {
            ERROR("Error while parsing string, Failed to find closing single quote\n");
            print_parse_error(pos);
            return PARSE_ERROR;
        }
    }
    else if (quote_chr == '"') {
        if ((c = fforward_skip_escaped_grow(pos, "\"\n", NULL, NULL, "\n", (char**)&(jo->value))) < 0) {
            ERROR("Error while parsing string, Failed to find closing quotes\n");
            print_parse_error(pos);
            return PARSE_ERROR;
        }
    }
    else {
        return PARSE_ERROR;
    }

    //DEBUG("json read string: %s\n", tmp);

    //if ((c = fforward_skip_escaped(pos, "\"'\n", NULL, NULL, "\n", tmp)) < 0) {
    //    ERROR("Error while parsing string, Failed to find closing quotes\n");
    //    print_parse_error(pos, LINES_CONTEXT);
    //    return PARSE_ERROR;
    //}
    //jo->value = tmp;


    // step over " char
    pos_next(pos);

    return STATUS_SUCCESS;
}

static enum JSONStatus json_parse_array(struct JSONObject* jo, struct Position* pos)
{
    jo->dtype = JSON_ARRAY;
    //jo->length = 0;
    jo->is_array = true;

    while (1) {
        struct JSONObject* child = json_object_init(jo);

        enum JSONStatus ret = json_parse(child, pos);
        if (ret < 0) {
            json_object_destroy(child);
            return ret;
        }
        else if (ret == END_OF_ARRAY) {
            json_object_destroy(child);
            break;
        }

        // look for comma or array end
        if (fforward_skip_escaped(pos, ",]", "\n\t ", NULL, "\n", NULL) < 0) {
            ERROR("Unexpected character while parsing array: %c\n", *pos->c);
            print_parse_error(pos);
            json_object_destroy(child);
            return PARSE_ERROR;
        }
    }
    return STATUS_SUCCESS;
}

static enum JSONStatus json_parse_object(struct JSONObject* jo, struct Position* pos)
{
    jo->dtype = JSON_OBJECT;
    jo->is_object = true;

    while (1) {
        struct JSONObject* child = json_object_init(jo);
        enum JSONStatus ret_key = json_parse_key(child, pos);

        if (ret_key < 0) {
            json_object_destroy(child);
            return ret_key;
        }
        else if (ret_key == END_OF_OBJECT) {
            json_object_destroy(child);
            break;
        }

        // parse the value
        enum JSONStatus ret_value = json_parse(child, pos);
        if (ret_value != STATUS_SUCCESS) {
            json_object_destroy(child);
            return PARSE_ERROR;
        }

        // look for comma or object end
        if (fforward_skip_escaped(pos, ",}", "\n\t ", NULL, "\n", NULL) < 0) {
            ERROR("Unexpected character while parsing object: %c\n", *pos->c);
            print_parse_error(pos);
            json_object_destroy(child);
            return PARSE_ERROR;
        }
    }
    return STATUS_SUCCESS;
}

static enum JSONStatus json_parse(struct JSONObject* jo, struct Position* pos)
{
    char tmp[MAX_BUF] = {'\0'};
    char c;

    // detect type
    if ((c = fforward_skip_escaped(pos, "\"[{1234567890-n.tf}]", NULL, NULL, "\n", tmp)) < 0) {
        ERROR("Unexpected character while finding type: %c\n", *pos->c);
        print_parse_error(pos);
        return PARSE_ERROR;
    }

    if (c == '[') {
        pos_next(pos);
        return json_parse_array(jo, pos);
    }
    else if (c == ']') {
        pos_next(pos);
        return END_OF_ARRAY;
    }
    else if (c == '{') {
        pos_next(pos);
        return json_parse_object(jo, pos);
    }
    else if (c == '}') {
        pos_next(pos);
        return END_OF_OBJECT;
    }
    //else if ((c == '"' || c == '\'') &&  *(pos->c-1) != '\\') {
    else if (c == '"' || c == '\'') {
        pos_next(pos);
        return json_parse_string(jo, pos, c);
    }
    else if (strchr("0123456789-n.", c)) {
        return json_parse_number(jo, pos);
    }
    else if (strchr("tf", c)) {
        return json_parse_bool(jo, pos);
    }
    else {
        return STATUS_ERROR;
    }
}

struct JSONObject* json_load(char* buf)
{
    struct Position* pos = malloc(sizeof(struct Position));
    pos->json     = buf;
    pos->c        = buf;
    pos->npos     = 0;
    pos->cols     = 1;
    pos->rows     = 1;
    pos->length   = strlen(buf);

    struct JSONObject* root = json_object_init(NULL);
    enum JSONStatus ret = json_parse(root, pos);

    // cleanup
    //free(pos->json);
    free(pos);

    return (ret < 0) ? NULL : root;
}

/* read file from disk and parse JSON */
struct JSONObject* json_load_file(char *path)
{
    // read file in chunks and dynamically allocate memory for buffer
    uint32_t chunk_size = 1000;   // read file in chunks
    uint32_t offset     = 0;    // offset in buffer to write data to
    uint32_t n_read     = 0;    // store amount of chars read from file
    FILE *fp = fopen(path, "r");

    if (fp == NULL) {
        ERROR("JSON: File doesn't exist: %s\n", path);
        return NULL;
    }

    char *buf = calloc(chunk_size, 1);

    while ((n_read=fread(buf + offset, 1, chunk_size, fp)) > 0) {
        offset += n_read;
        buf = realloc(buf, offset+chunk_size);
    }

    buf[offset] = '\0';     // properly end string array
    fclose(fp);
    DEBUG("read: %ld bytes\n", strlen(buf));

    struct JSONObject* jo = json_load(buf);
    return jo;
}

char* json_object_to_string(struct JSONObject *jo, int spaces)
{
    char *buf = NULL;
    json_object_to_string_rec(jo, &buf, 0, spaces);
    return buf;
}

int json_object_to_file(struct JSONObject *jo, char *path, int spaces)
{
    char *buf = json_object_to_string(jo, spaces);
    FILE *fp = fopen(path, "wb");

    if (fp == NULL) {
        ERROR("Failed to open file for writing\n");
        return -1;
    }
    size_t n;
    if ((n = fwrite(buf, 1, strlen(buf), fp)) < strlen(buf)) {
        ERROR("Failed to write to file (%ld bytes written)\n", n);
        return -1;
    }
    fclose(fp);
    return 0;
}

struct JSONObject* json_object_init(struct JSONObject* parent)
{
    struct JSONObject* jo = malloc(sizeof(struct JSONObject));
    jo->parent = parent;
    jo->prev = NULL;
    jo->next = NULL;

    jo->length = 0;
    //jo->index = -1;

    jo->dtype = JSON_UNKNOWN;

    jo->key = NULL;
    jo->value = NULL;

    jo->is_string = false;
    jo->is_number  = false;
    jo->is_bool   = false;
    jo->is_array  = false;
    jo->is_object = false;

    if (parent != NULL && (parent->is_array || parent->is_object))
        json_object_append_child(parent, jo);

    return jo;
}

void json_object_destroy(struct JSONObject* jo)
{
    /* TODO when removing an array index the indexes are not continuous anymore so 
     * they should be rebuilt
     */

    if (jo->key != NULL)
        free(jo->key);

    if (jo->parent != NULL && (jo->parent->is_array || jo->parent->is_object)) {

        // remove child from linked list
        if (jo->prev && jo->next) {
            jo->prev->next = jo->next;
            jo->next->prev = jo->prev->next;
        }
        else if (jo->prev) {
            jo->prev->next = NULL;
        }
        else if (jo->next) {
            // next is new head
            jo->next->prev = NULL;
            jo->parent->value = jo->next;
        }
        else {
            // no more linked list :(
            jo->parent->value = NULL;
        }
        
        if (jo->parent->length > 0)
            jo->parent->length--;
    }

    if (jo->dtype == JSON_OBJECT || jo->dtype == JSON_ARRAY) {

        struct JSONObject* child = jo->value;
        while (child != NULL) {
            struct JSONObject* tmp = child->next;
            json_object_destroy(child);
            child = tmp;
        }
    }
    else {
        free(jo->value);
    }

    free(jo);
}

static int json_object_get_index(struct JSONObject *jo)
{
    int count = 0;
    while (jo->prev != NULL) {
        jo = jo->prev;
        count++;
    }
    return count;
}

void json_print(struct JSONObject* jo, uint32_t level)
{
    /* Recursively print out tree starting at jo */
    uint8_t incr = 3;
    char space[level+1];
    get_spaces(space, level);

    if (jo != NULL) {
        INFO("%s", space);
        if (jo->parent && jo->parent->dtype == JSON_ARRAY)
            INFO("%s%d:%s ", JCOL_ARR_INDEX,json_object_get_index(jo), JRESET);
        if (jo->key)
            INFO("%s%s:%s ", JCOL_KEY, jo->key, JRESET);

        switch (jo->dtype) {

            case JSON_NUMBER:
                INFO("%s%f%s\n", JCOL_NUM, json_get_number(jo), JRESET);
                break;

            case JSON_STRING:
                INFO("%s\"%s\"%s\n", JCOL_STR, json_get_string(jo), JRESET);
                break;

            case JSON_BOOL:
                INFO("%s%s%s\n", JCOL_BOOL, json_get_bool(jo) ? "true" : "false", JRESET);
                break;

            case JSON_ARRAY:
                INFO("%s[ARRAY]%s\n", JCOL_ARR, JRESET);
                json_print(jo->value, level+incr);
                break;

            case JSON_OBJECT:
                INFO("%s[OBJECT]%s\n", JCOL_OBJ, JRESET);
                json_print(jo->value, level+incr);
                break;

            case JSON_UNKNOWN:
                INFO("%s[UNKNOWN]%s\n", JCOL_UNKNOWN, JRESET);
                break;
        }

        if (jo->next != NULL)
            json_print(jo->next, level);
    }
}

double json_get_number(struct JSONObject* jo)
{
    return *((double*)jo->value);
}

char* json_get_string(struct JSONObject* jo)
{
    return (char*)jo->value;
}

bool json_get_bool(struct JSONObject* jo)
{
    return *((bool*)jo->value);
}

struct JSONObject *json_object_init_string(struct JSONObject *parent, const char *key, const char *value)
{
    struct JSONObject *jo = json_object_init(parent);
    jo->dtype = JSON_STRING;
    jo->is_string = 1;
    jo->value = strdup(value);
    if (key)
        jo->key = strdup(key);
    return jo;
}

struct JSONObject *json_object_init_number(struct JSONObject *parent, const char *key, double value)
{
    struct JSONObject *jo = json_object_init(parent);
    jo->dtype = JSON_NUMBER;
    jo->is_number = 1;
    double *alloc_value = malloc(sizeof(double));
    *alloc_value = value;
    jo->value = alloc_value;
    if (key)
        jo->key = strdup(key);
    return jo;
}

struct JSONObject *json_object_init_object(struct JSONObject *parent, const char *key)
{
    struct JSONObject *jo = json_object_init(parent);
    jo->dtype = JSON_OBJECT;
    jo->is_object = 1;
    if (key)
        jo->key = strdup(key);
    return jo;
}

struct JSONObject *json_object_init_array(struct JSONObject *parent, const char *key)
{
    struct JSONObject *jo = json_object_init(parent);
    jo->dtype = JSON_ARRAY;
    jo->is_array = 1;
    if (key)
        jo->key = strdup(key);
    return jo;
}

int is_array_index(char *string)
{
    /* Check if string contains an array index by following the format:
     *   [123] => is index,               return: 123
     *   [-1]  => last item,              return:  -1
     *   [?]   => append to end of array, return:  -2
     *   else  => failed to parse,        return:  -3
     *
     */
    int len = strlen(string);
    int ret = JSON_ARR_INDEX_ERROR;
    int index;

    if (len == 0)
        return JSON_ARR_INDEX_ERROR;

    if (string[0] != '[' && string[len-1] != ']')
        return JSON_ARR_INDEX_ERROR;

    char *buf = malloc(len +1);
    char *pbuf = buf;

    for (int i=1 ; i<len-1 ; i++, pbuf++)
        *pbuf = string[i];
    *pbuf = '\0';

    if (strncmp(buf, "?", len) == 0) {
        ret = JSON_ARR_INDEX_APPEND;
    }

    else if (json_atoi_err(buf, &index) >= 0) {
        if (*buf == -1)
            ret = JSON_ARR_INDEX_LAST;
        else
            ret = index;
    }

    free(buf);
    return ret;
}

enum JSONParseResult parse_segment(const char *string, int *indexbuf)
{
    /* Function parses path segments and detects datatype by using the following format:
     *   [123] => is array index
     *   [-1]  => last array index
     *   [?]   => append to end of array
     *   abc   => key
     */
    enum JSONParseResult ret = JSON_PR_ERROR;
    if (string == NULL)
        return JSON_PR_ERROR;

    int len = strlen(string);
    if (len == 0)
        return JSON_PR_ERROR;

    // is key
    if (strcmp(string, "{}") == 0)
        return JSON_PR_EMPTY_OBJ;

    if (string[0] != '[' && string[len-1] != ']')
        return JSON_PR_OBJ_KEY;

    char *buf = malloc(len +1);
    char *pbuf = buf;

    for (int i=1 ; i<len-1 ; i++, pbuf++)
        *pbuf = string[i];
    *pbuf = '\0';

    // is arr length + 1
    if (strncmp(buf, "?", len) == 0) {
        ret = JSON_PR_ARR_INDEX_APPEND;
    }

    else if (json_atoi_err(buf, indexbuf) >= 0) {
        if (*indexbuf == -1)
            ret = JSON_PR_ARR_INDEX_LAST;
        else
            ret = JSON_PR_ARR_INDEX_REPLACE;
    }

    free(buf);
    return ret;
}

struct TokenResult get_next_segment_parsed(const char *path, const char *delim)
{
    /* Get first path segment until delim
     * Parse the segment, find type and in case of array the index
     * If segment was found, set result.found to 1
     */
    char *path_copy = strdup(path);
    char *rest;
    char *token = strtok_r(path_copy, PATH_DELIM, &rest);

    struct TokenResult result;
    result.token[0] = '\0';
    result.rest[0] = '\0';
    result.parsed = JSON_PR_ERROR;
    result.index = JSON_TOKEN_RESULT_INVALID_INDEX;
    result.is_last = (strlen(rest) == 0);

    if (token) {
        strcpy(result.token, token);
        result.parsed = parse_segment(token, &result.index);
    }

    if (rest)
        strcpy(result.rest, rest);

    free(path_copy);
    return result;
}

struct JSONObject *json_object_get_object_by_key(struct JSONObject *jo, const char *key)
{
    if (jo->value == NULL)
        return NULL;

    struct JSONObject *child = jo->value;

    while (child != NULL) {
        if (child->key != NULL)
            if (strcmp(child->key, key) == 0)
                return child;

        child = child->next;
    }

    return NULL;
}

struct JSONObject* json_get_path(struct JSONObject *rn, char *buf)
{
    /* Find a path given as a string and return node if found */
    if (rn == NULL)
        return NULL;

    if (!(rn->is_object || rn->is_array))
        return NULL;

    char path[JSON_MAX_PATH_LEN] = "";
    strncpy(path, buf, JSON_MAX_PATH_LEN);

    struct JSONObject* seg = rn;
    char *lasts;
    char *token = strtok_r(path, PATH_DELIM, &lasts);


    while(token) {
        if (seg == NULL)
            break;

        seg = seg->value;

        int index;
        enum JSONParseResult res = parse_segment(token, &index);

        while (seg != NULL) {

            if (res == JSON_PR_OBJ_KEY) {
               // ASSERTF((seg->parent == NULL || !seg->parent->is_object), "(%s) parent == NULL or not an object", token);
               // ASSERTF((seg->key == NULL), "(%s) Parse Error, seg->key == NULL", token);
                if (seg->parent == NULL || !seg->parent->is_object)
                    RET_NULL("(%s) parent == NULL or not an object", token);
                if (seg->key == NULL)
                    RET_NULL("(%s) Parse Error, seg->key == NULL", token);
                if (strcmp(token, seg->key) == 0)
                    break;
            }
            else if (res == JSON_PR_ARR_INDEX_REPLACE) {
                //ASSERTF((seg->parent == NULL || !seg->parent->is_array), "(%s) parent == NULL or not an array", token);
                if (seg->parent == NULL || !seg->parent->is_array)
                    RET_NULL("(%s) parent == NULL or not an array", token);
                if (json_object_get_index(seg) == index)
                    break;
            }
            else if (res == JSON_PR_ARR_INDEX_LAST) {
                //ASSERTF((seg->parent == NULL || !seg->parent->is_array), "(%s) parent == NULL or not an array", token);
                if (seg->parent == NULL || !seg->parent->is_array)
                    RET_NULL("(%s) parent == NULL or not an array", token);
                if (json_object_get_index(seg) == seg->parent->length-1)
                    break;
            }

            seg = seg->next;
        }

        token = strtok_r(NULL, PATH_DELIM, &lasts);
    }
    return seg;
}

int json_object_replace_child_by_key(struct JSONObject *parent, struct JSONObject *child, char *key)
{
    if (!parent->is_object)
        return -1;

    struct JSONObject *tmp = json_object_get_object_by_key(parent, key);
    json_object_destroy(tmp);
    json_object_append_child(parent, child);
    return 0;
}

struct JSONObject *json_set_path(struct JSONObject *jo_cur, const char *buf_path, struct JSONObject *child)
{
    /*
     * path = test_arr/[3]/{}
     * when itering path we get:
     *   jo_cur = test_arr
     *   seg_cur = [3]
     *   seg_next = {}
     *   so we look at seg_cur to check what kind of object we need to make
     *   In case of jo_cur being an array, we even have to look at seg_next
     *   to see what we need to create in test_arr[3]
     *
     * USAGE:
     *   create nested arrays and objects on the fly
     *   using keys after an array implicitly creates an empty
     *   object at that index with a child with key1
     *      eg: array/[0]/key1
     *
     *   append to an array
     *      eg: array/[?]/bla
     *
     *   replace last item in array
     *      eg: array/[-1]/bla
     *
     *
     */

    // get and parse the path segments
    struct TokenResult seg      = get_next_segment_parsed(buf_path, PATH_DELIM);
    struct TokenResult seg_next = get_next_segment_parsed(seg.rest, PATH_DELIM);
    struct JSONObject *jo_found = json_get_path(jo_cur, seg.token);

    DEBUG("[%s]\tINDEX=%d\tTOKEN=%s\t=> %s\n", (jo_found) ? "FOUND" : "NOT_FOUND", seg.index, seg.token, seg_next.token);

    if (jo_found) {

        if (seg.is_last) {

            // Check if existing key in object needs to be replaced or appended
            if (jo_found->is_object) {
                struct JSONObject *existing_child = json_object_get_object_by_key(jo_found, child->key);

                if (existing_child != NULL)
                    json_object_destroy(existing_child);
                json_object_append_child(jo_found, child);
            }

            else if (IS_ARRAY(jo_found)) {
                assertf(child->key == NULL, "Failed to replace child at [%d] in array, child has key: %s", seg.index, child->key);

                if (seg.parsed == JSON_PR_ARR_INDEX_LAST)
                    json_object_replace_child_at_index(jo_found->parent, child, json_count_children(jo_found->parent)-1);
                else if (seg.parsed == JSON_PR_ARR_INDEX_REPLACE)
                    json_object_replace_child_at_index(jo_found->parent, child, seg.index);
            }
            return child;
        }

        // We found an array index, we should see if it needs replacing
        // So we need to look ahead to the next token
        else if (IS_ARRAY(jo_found->parent)) {

            if  (seg_next.parsed == JSON_PR_OBJ_KEY) {
                if (jo_found->is_object) {

                    if (seg_next.is_last) {
                        if (json_object_get_object_by_key(jo_found, seg_next.token)) {
                            DEBUG("replace key in object in array\n");
                            struct JSONObject *new_child = json_object_init_object(NULL, seg_next.token);
                            json_object_replace_child_by_key(jo_found, new_child, seg_next.token);
                            jo_cur = new_child;
                            seg = seg_next;
                        }
                        else {
                            DEBUG("append new key in object in array\n");
                            struct JSONObject *new_child = json_object_init_object(NULL, seg_next.token);
                            json_object_append_child(jo_found, new_child);
                            jo_cur = new_child;
                            seg = seg_next;
                        }
                    }
                    else {
                        jo_cur = jo_found;
                    }
                }

                // Check if a child at jo_found->parent[seg.index] exists
                else if (json_get_child_by_index(jo_found->parent, seg.index)) {
                    DEBUG("Replacing at index: %d\n", seg.index);
                    struct JSONObject *new_child = json_object_init_object(NULL, NULL);
                    json_object_replace_child_at_index(jo_found->parent, new_child, seg.index);
                    jo_cur = new_child;
                }
                else {
                    jo_cur = jo_found;
                }
                
            }
            else {
                jo_cur = jo_found;
            }
        }

        // replace a key in object when the found object is an endnode
        else if (IS_NOT_CONTAINER(jo_found)) {
            DEBUG("%s is endnode and must be replaced\n", jo_found->key);
            int index = json_object_get_index(jo_found);
            struct JSONObject *new_child = json_object_init_object(NULL, jo_found->key);
            json_object_replace_child_at_index(jo_found->parent, new_child, index);
            jo_cur = new_child;
        }
        else {
            jo_cur = jo_found;
        }
    }

    else if (!jo_found) {

        if (jo_cur->is_object) {

            // Look ahead to check if next token is an array index
            // If so, create an array instead of an object
            if (IS_ARR_OP(seg_next)) {
                DEBUG("creating array\n");
                struct JSONObject *new_child = json_object_init_array(NULL, seg.token);
                json_object_append_child(jo_cur, new_child);
                jo_cur = new_child;

            }
            else {
                DEBUG("[%s] create new object here\n", seg.token);
                struct JSONObject *new_child = json_object_init_object(jo_cur, seg.token);
                jo_cur = new_child;
            }
        }
        else if (jo_cur->is_array) {
                if (seg.is_last && child->key != NULL) {
                    DEBUG("child has key so creating empty object at index=%d\n", seg.index);
                    struct JSONObject *empty_obj = json_object_init_object(NULL, NULL);
                    json_object_append_child(jo_cur, empty_obj);
                    jo_cur = empty_obj;
                }

                else if  (seg_next.parsed == JSON_PR_OBJ_KEY) {
                    if (seg.index > json_count_children(jo_cur))
                        RET_NULL("Index '%d' doesn't exist in array\n", seg.index);

                    DEBUG("implicit creation of object in array at index=%d\n", seg.index);
                    struct JSONObject *empty_obj = json_object_init_object(NULL, NULL);
                    json_object_append_child(jo_cur, empty_obj);

                    struct JSONObject *new_child = json_object_init_object(NULL, seg_next.token);
                    json_object_append_child(empty_obj, new_child);
                    jo_cur = new_child;

                }
                else if (IS_ARR_OP(seg_next)) {
                    DEBUG("creating nested array\n");
                    struct JSONObject *new_child = json_object_init_array(NULL, NULL);
                    json_object_append_child(jo_cur, new_child);
                    jo_cur = new_child;
                }
                else if  (seg.parsed == JSON_PR_OBJ_KEY) {
                    DEBUG("implicit creation of object in array\n");
                    struct JSONObject *empty_obj = json_object_init_object(NULL, NULL);
                    json_object_append_child(jo_cur, empty_obj);

                    struct JSONObject *new_child = json_object_init_object(NULL, seg.token);
                    json_object_append_child(empty_obj, new_child);
                    jo_cur = new_child;
                }
                else {
                    ERROR("UNEXPECTED!\n");
                }

                // skip segment because an object in an array doesn't have a key
                seg = seg_next;
        }
        else {
            ERROR("[%s] Exception!!!! %s\n", seg.token, jo_cur->key);
        }

        // at end of token path
        if (seg.is_last) {
            if (jo_cur->is_array) {

                if (seg.parsed == JSON_PR_ARR_INDEX_APPEND) {
                    assertf(child->key == NULL, "Failed to append child to array, child has key: %s", child->key);
                    json_object_append_child(jo_cur, child);
                    DEBUG("append into array\n");
                }
                // !!!!!!!!1 TODO handle -1 and [0] for the edge case where we just created a new array in the previous step
                else {
                    ERROR("Unknown array operation: %d\n", seg.parsed);
                }
            }

            //else if (jo_cur->parent && jo_cur->parent->is_object) {
            else if (jo_cur->is_object) {
                DEBUG("[%s] Insert child here\n", seg.token);
                json_object_append_child(jo_cur, child);
            }
            else {
                ERROR("EXCEPTION\n");
            }
            return child;
        }
    }

    return json_set_path(jo_cur, seg.rest, child);
}



