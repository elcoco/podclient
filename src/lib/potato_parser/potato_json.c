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
            case PP_DTYPE_KEY:
                strcpy(dtype, "KEY        ");
                break;
            case PP_DTYPE_NUMBER:
                strcpy(dtype, "NUMBER        ");
                break;
            case PP_DTYPE_BOOL:
                strcpy(dtype, "BOOL        ");
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
    struct PPItem *item_prev_prev = pp_stack_get_from_end(pp, 1);
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

    if (item_prev_prev != NULL && item_prev_prev->dtype == PP_DTYPE_KEY)
        pp_stack_pop(&(pp->stack));
    return PP_PARSE_RESULT_SUCCESS;
}

enum PPParseResult pp_json_bool_cb(struct PP *pp, struct PPParserEntry *pe, struct PPItem *item)
{

    assert(item->dtype == PP_DTYPE_BOOL);  // Test if item is right type
    struct PPItem *item_prev = pp_stack_get_from_end(pp, 0);
    pp_stack_put(&(pp->stack), *item);
    pp->handle_data_cb(pp, item->dtype, pp->user_data);
    pp_stack_pop(&(pp->stack));

    if (item_prev != NULL && item_prev->dtype == PP_DTYPE_KEY)
        pp_stack_pop(&(pp->stack));

    return PP_PARSE_RESULT_SUCCESS;
}

enum PPParseResult pp_json_number_cb(struct PP *pp, struct PPParserEntry *pe, struct PPItem *item)
{

    assert(item->dtype == PP_DTYPE_NUMBER);  // Test if item is right type
    struct PPItem *item_prev = pp_stack_get_from_end(pp, 0);
    pp_stack_put(&(pp->stack), *item);
    pp->handle_data_cb(pp, item->dtype, pp->user_data);
    pp_stack_pop(&(pp->stack));

    if (item_prev != NULL && item_prev->dtype == PP_DTYPE_KEY)
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

    if (item_prev->dtype == PP_DTYPE_OBJECT_OPEN) {
        //DEBUG("FOUND KEY\n");
        item->dtype = PP_DTYPE_KEY;
        pp_stack_put(&(pp->stack), *item);
        pp->handle_data_cb(pp, item->dtype, pp->user_data);
    }
    else if (item_prev->dtype == PP_DTYPE_ARRAY_OPEN) {
        item->dtype = PP_DTYPE_STRING;
        pp_stack_put(&(pp->stack), *item);
        pp->handle_data_cb(pp, item->dtype, pp->user_data);
        pp_stack_pop(&(pp->stack));
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
    struct PPItem *item_prev_prev = pp_stack_get_from_end(pp, 2);
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
            pp_print_spaces(spaces * pp->stack.pos - spaces);
            if (item_prev != NULL && item_prev->dtype == PP_DTYPE_KEY) {
                INFO("%s: ", item_prev->data);
            }

            INFO("ARRAY_OPEN\n");
            break;

        case PP_DTYPE_ARRAY_CLOSE:
            pp_print_spaces(spaces * pp->stack.pos - spaces);
            INFO("ARRAY_CLOSE\n");
            break;

        case PP_DTYPE_STRING:
            pp_print_spaces(spaces * pp->stack.pos - spaces);
            if (item_prev_prev != NULL && item_prev_prev->dtype == PP_DTYPE_KEY && item_prev != NULL && item_prev->dtype == PP_DTYPE_ARRAY_OPEN)
                pp_print_spaces(spaces);
            if (item_prev != NULL && item_prev->dtype == PP_DTYPE_KEY)
                INFO("%s: ", item_prev->data);

            INFO("[STRING:%03ld] %s\n", strlen(item->data), item->data);
            break;
        case PP_DTYPE_BOOL:
            pp_print_spaces(spaces * pp->stack.pos - spaces);
            if (item_prev != NULL && item_prev->dtype == PP_DTYPE_KEY)
                INFO("%s: ", item_prev->data);
            INFO("[BOOL] %s\n", item->data);
            break;

        case PP_DTYPE_NUMBER:
            pp_print_spaces(spaces * pp->stack.pos - spaces);
            if (item_prev != NULL && item_prev->dtype == PP_DTYPE_KEY)
                INFO("%s: ", item_prev->data);
            INFO("[NUMBER] %s\n", item->data);
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

    struct PPParserEntry pe_string = pp_entry_init();
    struct PPParserEntry pe_object_open = pp_entry_init();
    struct PPParserEntry pe_object_close = pp_entry_init();
    struct PPParserEntry pe_array_open = pp_entry_init();
    struct PPParserEntry pe_array_close = pp_entry_init();
    struct PPParserEntry pe_true = pp_entry_init();
    struct PPParserEntry pe_false = pp_entry_init();
    struct PPParserEntry pe_number = pp_entry_init();


    pe_string.start          = PP_JSON_STRING_OPEN;
    pe_string.end            = PP_JSON_STRING_CLOSE;
    pe_string.ignore_chars   = "\r\n\t :,";
    pe_string.dtype          = PP_DTYPE_STRING;
    pe_string.greedy         = PP_METHOD_NON_GREEDY;
    pe_string.match_type     = PP_MATCH_START_END;
    pe_string.cb             = pp_json_string_cb;
    pe_string.step_over      = 1;
    pp_add_parse_entry(&pp, pe_string);

    pe_object_open.start          = PP_JSON_OBJECT_OPEN;
    //pe_object_open.end            = "";
    pe_object_open.ignore_chars   = "\r\n\t :,";
    pe_object_open.dtype          = PP_DTYPE_OBJECT_OPEN;
    pe_object_open.greedy         = PP_METHOD_NON_GREEDY;
    pe_object_open.match_type     = PP_MATCH_START;
    pe_object_open.cb             = pp_json_object_open_cb;
    pe_object_open.step_over      = 1;
    pp_add_parse_entry(&pp, pe_object_open);

    pe_object_close.start          = PP_JSON_OBJECT_CLOSE;
    //pe_object_close.end            = "";
    pe_object_close.ignore_chars   = "\r\n\t ,";
    pe_object_close.dtype          = PP_DTYPE_OBJECT_CLOSE;
    pe_object_close.greedy         = PP_METHOD_NON_GREEDY;
    pe_object_close.match_type     = PP_MATCH_START;
    pe_object_close.cb             = pp_json_object_close_cb;
    pe_object_close.step_over      = 1;
    pp_add_parse_entry(&pp, pe_object_close);

    pe_array_open.start          = PP_JSON_ARRAY_OPEN;
    //pe_array_open.end            = "";
    pe_array_open.ignore_chars   = "\r\n\t :,";
    pe_array_open.dtype          = PP_DTYPE_ARRAY_OPEN;
    pe_array_open.greedy         = PP_METHOD_NON_GREEDY;
    pe_array_open.match_type     = PP_MATCH_START;
    pe_array_open.cb             = pp_json_array_open_cb;
    pe_array_open.step_over      = 1;
    pp_add_parse_entry(&pp, pe_array_open);

    pe_array_close.start          = PP_JSON_ARRAY_CLOSE;
    //pe_array_close.end            = "";
    pe_array_close.ignore_chars   = "\r\n\t ,";
    pe_array_close.dtype          = PP_DTYPE_ARRAY_CLOSE;
    pe_array_close.match_type     = PP_MATCH_START;
    pe_array_close.greedy         = PP_METHOD_NON_GREEDY;
    pe_array_close.cb             = pp_json_array_close_cb;
    pe_array_close.step_over      = 1;
    pp_add_parse_entry(&pp, pe_array_close);

    pe_true.start          = "tr";
    pe_true.end            = "ue";
    pe_true.ignore_chars   = "\r\n\t :,";
    pe_true.dtype          = PP_DTYPE_BOOL;
    pe_true.match_type     = PP_MATCH_START_END;
    pe_true.greedy         = PP_METHOD_GREEDY;
    pe_true.cb             = pp_json_bool_cb;
    pe_true.step_over      = 1;
    pp_add_parse_entry(&pp, pe_true);

    pe_false.start          = "fa";
    pe_false.end            = "lse";
    pe_false.ignore_chars   = "\r\n\t :,";
    pe_false.dtype          = PP_DTYPE_BOOL;
    pe_false.match_type     = PP_MATCH_START_END;
    pe_false.greedy         = PP_METHOD_GREEDY;
    pe_false.cb             = pp_json_bool_cb;
    pe_false.step_over      = 1;
    pp_add_parse_entry(&pp, pe_false);

    pe_number.any            = ": 0123456789-null.";
    pe_number.ignore_chars   = "\r\n\t :,";
    pe_number.dtype          = PP_DTYPE_NUMBER;
    pe_number.match_type     = PP_MATCH_ANY;
    pe_number.greedy         = PP_METHOD_GREEDY;
    pe_number.cb             = pp_json_number_cb;
    pe_number.step_over      = 1;
    pp_add_parse_entry(&pp, pe_number);
    // this will capture numbers and bool
    //pp_add_parse_entry(&pp, ":",                         ",",                      "\r\n\t ", PP_DTYPE_STRING,       pp_json_string_cb,  PP_METHOD_GREEDY);

    return pp;
}

