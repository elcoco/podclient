#include "potato_json.h"
#include "potato_parser.h"

//#define DO_DEBUG 1
//#define DO_INFO  1
//#define DO_ERROR 1

#define DEBUG(M, ...) if(do_debug){fprintf(stdout, "[DEBUG] " M, ##__VA_ARGS__);}
#define INFO(M, ...) if(do_info){fprintf(stdout, M, ##__VA_ARGS__);}
#define ERROR(M, ...) if(do_error){fprintf(stderr, "[ERROR] (%s:%d) " M, __FILE__, __LINE__, ##__VA_ARGS__);}

#define ASSERTF(A, M, ...) if(!(A)) {fprintf(stderr, M, ##__VA_ARGS__); assert(A); }


static void pp_json_stack_debug(struct PPStack *stack)
{
    INFO("STACK CONTENTS\n");
    struct PPItem *xi = stack->stack;

    for (int i=0 ; i<PP_MAX_STACK ; i++, xi++) {
        char dtype[16] = "";
        switch (xi->dtype) {
            case PP_DTYPE_OBJECT_OPEN:
                strcpy(dtype, "OBJECT_OPEN  ");
                break;
            case PP_DTYPE_OBJECT_CLOSE:
                strcpy(dtype, "OBJECT_CLOSE ");
                break;
            case PP_DTYPE_ARRAY_OPEN:
                strcpy(dtype, "ARRAY_OPEN  ");
                break;
            case PP_DTYPE_ARRAY_CLOSE:
                strcpy(dtype, "ARRAY_CLOSE ");
                break;
            case PP_DTYPE_STRING:
                strcpy(dtype, "STRING    ");
                break;
            case PP_DTYPE_VALUE:
                strcpy(dtype, "VALUE        ");
                break;
            case PP_DTYPE_KEY:
                strcpy(dtype, "KEY        ");
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

enum PPParseResult pp_json_object_open_cb(struct PP *pp, struct PPParserEntry *pe, struct PPItem *item)
{
    //DEBUG("FOUND OBJECT_OPEN\n");
    assert(item->dtype == PP_DTYPE_OBJECT_OPEN);  // Test if item is right type
    pp_stack_put(&(pp->stack), *item);
    pp->handle_data_cb(pp, item->dtype, pp->user_data);
    return PP_PARSE_RESULT_SUCCESS;
}

enum PPParseResult pp_json_object_close_cb(struct PP *pp, struct PPParserEntry *pe, struct PPItem *item)
{
    //DEBUG("FOUND OBJECT_CLOSE\n");
    assert(item->dtype == PP_DTYPE_OBJECT_CLOSE);  // Test if item is right type

    struct PPItem *item_prev = pp_stack_get_from_end(pp, 0);
    if (item_prev == NULL) {
        ERROR("Unexpected end of object found, stack is empty\n");
        pp_json_stack_debug(&(pp->stack));
        return PP_PARSE_RESULT_ERROR;
    }
    if (item_prev->dtype != PP_DTYPE_OBJECT_OPEN) {
        ERROR("Unexpected end of object found, previous stack item is not PP_DTYPE_OBJECT_OPEN\n");
        pp_json_stack_debug(&(pp->stack));
        return PP_PARSE_RESULT_ERROR;
    }
    pp_stack_put(&(pp->stack), *item);
    pp->handle_data_cb(pp, item->dtype, pp->user_data);
    pp_stack_pop(&(pp->stack));
    pp_stack_pop(&(pp->stack));
    return PP_PARSE_RESULT_SUCCESS;
}

enum PPParseResult pp_json_array_open_cb(struct PP *pp, struct PPParserEntry *pe, struct PPItem *item)
{
    //DEBUG("FOUND ARRAY_OPEN\n");
    assert(item->dtype == PP_DTYPE_ARRAY_OPEN);  // Test if item is right type
    pp_stack_put(&(pp->stack), *item);
    pp->handle_data_cb(pp, item->dtype, pp->user_data);
    return PP_PARSE_RESULT_SUCCESS;
}

enum PPParseResult pp_json_array_close_cb(struct PP *pp, struct PPParserEntry *pe, struct PPItem *item)
{
    //DEBUG("FOUND ARRAY_CLOSE\n");
    assert(item->dtype == PP_DTYPE_ARRAY_CLOSE);  // Test if item is right type
                                                  //
    struct PPItem *item_prev = pp_stack_get_from_end(pp, 0);
    if (item_prev == NULL) {
        ERROR("Unexpected end of array found, stack is empty\n");
        pp_json_stack_debug(&(pp->stack));
        return PP_PARSE_RESULT_ERROR;
    }
    if (item_prev->dtype != PP_DTYPE_ARRAY_OPEN) {
        ERROR("Unexpected end of array found, previous stack item is not PP_DTYPE_ARRAY_OPEN\n");
        pp_json_stack_debug(&(pp->stack));
        return PP_PARSE_RESULT_ERROR;
    }

    pp_stack_put(&(pp->stack), *item);
    pp->handle_data_cb(pp, item->dtype, pp->user_data);
    pp_stack_pop(&(pp->stack));
    pp_stack_pop(&(pp->stack));
    return PP_PARSE_RESULT_SUCCESS;
}

enum PPParseResult pp_json_string_cb(struct PP *pp, struct PPParserEntry *pe, struct PPItem *item)
{
    assert(item->dtype == PP_DTYPE_STRING);  // Test if item is right type

    struct PPItem *item_prev = pp_stack_get_from_end(pp, 0);
    if (item_prev == NULL) {
        ERROR("Unexpected string found, previous stack item must be an object, array or key: %s\n", item->data);
        pp_json_stack_debug(&(pp->stack));
        return PP_PARSE_RESULT_ERROR;
    }

    if (item_prev->dtype == PP_DTYPE_OBJECT_OPEN || item_prev->dtype == PP_DTYPE_ARRAY_OPEN) {
        //DEBUG("FOUND KEY\n");
        item->dtype = PP_DTYPE_KEY;
        pp_stack_put(&(pp->stack), *item);
        pp->handle_data_cb(pp, item->dtype, pp->user_data);
    }
    else if (item_prev->dtype == PP_DTYPE_KEY) {
        item->dtype = PP_DTYPE_STRING;
        pp_stack_put(&(pp->stack), *item);
        pp->handle_data_cb(pp, item->dtype, pp->user_data);
        pp_stack_pop(&(pp->stack));
        pp_stack_pop(&(pp->stack));
    }
    else {
        //ERROR("FOUND RANDOM STUFF\n");
        return PP_PARSE_RESULT_ERROR;
    }

    // check here if it is a key or value and set dtype accordingly
    
    return PP_PARSE_RESULT_SUCCESS;
}

void pp_json_handle_data_cb(struct PP *pp, enum PPDtype dtype, void *user_data)
{
    /* Callback can be used, instead of custom callback, to display full xml data */
    const int spaces = 2;

    struct PPItem *item = pp_stack_get_from_end(pp, 0);
    struct PPItem *item_prev = pp_stack_get_from_end(pp, 1);
    ASSERTF(item != NULL, "Callback received empty stack!");

    switch (dtype) {
        case PP_DTYPE_OBJECT_OPEN:
            pp_print_spaces(spaces * pp->stack.pos);
            if (item_prev != NULL && item_prev->dtype == PP_DTYPE_KEY)
                INFO("%s: \n", item_prev->data);

            INFO("OBJECT_OPEN\n");
            break;

        case PP_DTYPE_OBJECT_CLOSE:
            pp_print_spaces(spaces * pp->stack.pos - spaces);
            INFO("OBJECT_CLOSE\n");
            break;

        case PP_DTYPE_ARRAY_OPEN:
            pp_print_spaces(spaces * pp->stack.pos);
            if (item_prev != NULL && item_prev->dtype == PP_DTYPE_KEY)
                INFO("%s: ", item_prev->data);

            INFO("OBJECT_OPEN\n");
            break;

        case PP_DTYPE_ARRAY_CLOSE:
            pp_print_spaces(spaces * pp->stack.pos - spaces);
            INFO("ARRAY_CLOSE");
            break;

        case PP_DTYPE_STRING:
            pp_print_spaces(spaces * pp->stack.pos - spaces);
            if (item_prev != NULL && item_prev->dtype == PP_DTYPE_KEY)
                INFO("%s:\t", item_prev->data);

            INFO("[STRING:%03ld]: %s\n", strlen(item->data), item->data);
            break;
        case PP_DTYPE_KEY:
            break;
    }
}


struct PP pp_json_init(handle_data_cb data_cb)
{
    DEBUG("init json\n");
    struct PP pp;
    pp_stack_init(&(pp.stack));

    pp.max_entries = 0;
    //pp.skip_str[0] = '\0';
    pp.skip_is_set = 0;
    pp.zero_rd_cnt = 0;
    pp.user_data = NULL;
    pp.handle_data_cb = data_cb;

    // add required chars so we can check if something is a value (has ':' before) or key has only spaces
    pp_add_parse_entry(&pp, PP_JSON_CHAR_STRING_START,   PP_JSON_CHAR_STRING_END,  "\r\n\t :,", PP_DTYPE_STRING,       pp_json_string_cb,       PP_METHOD_GREEDY);
    pp_add_parse_entry(&pp, PP_JSON_CHAR_STRING2_START,  PP_JSON_CHAR_STRING2_END, "\r\n\t :,", PP_DTYPE_STRING,       pp_json_string_cb,       PP_METHOD_GREEDY);
    pp_add_parse_entry(&pp, PP_JSON_CHAR_OBJECT_START,   "",                       "\r\n\t :,", PP_DTYPE_OBJECT_OPEN,  pp_json_object_open_cb,  PP_METHOD_GREEDY);
    pp_add_parse_entry(&pp, PP_JSON_CHAR_OBJECT_END,     "",                       "\r\n\t   ", PP_DTYPE_OBJECT_CLOSE, pp_json_object_close_cb, PP_METHOD_GREEDY);
    pp_add_parse_entry(&pp, PP_JSON_CHAR_ARRAY_START,    "",                       "\r\n\t :,", PP_DTYPE_ARRAY_OPEN,   pp_json_array_open_cb,   PP_METHOD_GREEDY);
    pp_add_parse_entry(&pp, PP_JSON_CHAR_ARRAY_END,      "",                       "\r\n\t   ", PP_DTYPE_ARRAY_CLOSE,  pp_json_array_close_cb,  PP_METHOD_GREEDY);
    //pp_add_parse_entry(&pp, PP_JSON_CHAR_VALUE,            "",  PP_DTYPE_VALUE,          pp_json_value_cb, PP_METHOD_GREEDY);

    return pp;
}

