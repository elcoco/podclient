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
    struct PPToken *xi = stack->stack;

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

enum PPParseResult pp_json_object_open_cb(struct PP *pp, struct PPToken *t)
{
    //DEBUG("FOUND OBJECT_OPEN\n");
    assert(t->dtype == PP_DTYPE_OBJECT_OPEN);  // Test if token is right type
    pp_stack_put(&(pp->stack), *t);
    pp->handle_data_cb(pp, t->dtype, pp->user_data);
    return PP_PARSE_RESULT_SUCCESS;
}

enum PPParseResult pp_json_object_close_cb(struct PP *pp, struct PPToken *t)
{
    //DEBUG("FOUND OBJECT_CLOSE\n");
    assert(t->dtype == PP_DTYPE_OBJECT_CLOSE);  // Test if token is right type

    struct PPToken *t_prev = pp_stack_get_from_end(pp, 0);
    if (t_prev == NULL) {
        ERROR("Unexpected end of object found, stack is empty\n");
        pp_json_stack_debug(&(pp->stack));
        return PP_PARSE_RESULT_ERROR;
    }
    if (t_prev->dtype != PP_DTYPE_OBJECT_OPEN) {
        ERROR("Unexpected end of object found, previous stack token is not PP_DTYPE_OBJECT_OPEN\n");
        pp_json_stack_debug(&(pp->stack));
        return PP_PARSE_RESULT_ERROR;
    }
    pp_stack_put(&(pp->stack), *t);
    pp->handle_data_cb(pp, t->dtype, pp->user_data);
    pp_stack_pop(&(pp->stack));
    pp_stack_pop(&(pp->stack));
    return PP_PARSE_RESULT_SUCCESS;
}

enum PPParseResult pp_json_array_open_cb(struct PP *pp, struct PPToken *t)
{
    //DEBUG("FOUND ARRAY_OPEN\n");
    assert(t->dtype == PP_DTYPE_ARRAY_OPEN);  // Test if token is right type
    pp_stack_put(&(pp->stack), *t);
    pp->handle_data_cb(pp, t->dtype, pp->user_data);
    return PP_PARSE_RESULT_SUCCESS;
}

enum PPParseResult pp_json_array_close_cb(struct PP *pp, struct PPToken *t)
{
    //DEBUG("FOUND ARRAY_CLOSE\n");
    assert(t->dtype == PP_DTYPE_ARRAY_CLOSE);  // Test if token is right type
                                                  //
    struct PPToken *t_prev = pp_stack_get_from_end(pp, 0);
    struct PPToken *t_prev_prev = pp_stack_get_from_end(pp, 1);
    if (t_prev == NULL) {
        ERROR("Unexpected end of array found, stack is empty\n");
        pp_json_stack_debug(&(pp->stack));
        return PP_PARSE_RESULT_ERROR;
    }
    if (t_prev->dtype != PP_DTYPE_ARRAY_OPEN) {
        ERROR("Unexpected end of array found, previous stack token is not PP_DTYPE_ARRAY_OPEN\n");
        pp_json_stack_debug(&(pp->stack));
        return PP_PARSE_RESULT_ERROR;
    }

    pp_stack_put(&(pp->stack), *t);
    pp->handle_data_cb(pp, t->dtype, pp->user_data);
    pp_stack_pop(&(pp->stack));
    pp_stack_pop(&(pp->stack));

    if (t_prev_prev != NULL && t_prev_prev->dtype == PP_DTYPE_KEY)
        pp_stack_pop(&(pp->stack));
    return PP_PARSE_RESULT_SUCCESS;
}

enum PPParseResult pp_json_bool_cb(struct PP *pp, struct PPToken *t)
{

    assert(t->dtype == PP_DTYPE_BOOL);  // Test if token is right type
    struct PPToken *t_prev = pp_stack_get_from_end(pp, 0);
    pp_stack_put(&(pp->stack), *t);
    pp->handle_data_cb(pp, t->dtype, pp->user_data);
    pp_stack_pop(&(pp->stack));

    if (t_prev != NULL && t_prev->dtype == PP_DTYPE_KEY)
        pp_stack_pop(&(pp->stack));

    return PP_PARSE_RESULT_SUCCESS;
}

enum PPParseResult pp_json_number_cb(struct PP *pp, struct PPToken *t)
{

    assert(t->dtype == PP_DTYPE_NUMBER);  // Test if token is right type
    struct PPToken *t_prev = pp_stack_get_from_end(pp, 0);
    pp_stack_put(&(pp->stack), *t);
    pp->handle_data_cb(pp, t->dtype, pp->user_data);
    pp_stack_pop(&(pp->stack));

    if (t_prev != NULL && t_prev->dtype == PP_DTYPE_KEY)
        pp_stack_pop(&(pp->stack));

    return PP_PARSE_RESULT_SUCCESS;
}

enum PPParseResult pp_json_string_cb(struct PP *pp, struct PPToken *t)
{
    assert(t->dtype == PP_DTYPE_STRING);  // Test if token is right type

    struct PPToken *t_prev = pp_stack_get_from_end(pp, 0);
    if (t_prev == NULL) {
        ERROR("Unexpected string found, previous stack token must be an object, array or key: %s\n", t->data);
        pp_json_stack_debug(&(pp->stack));
        return PP_PARSE_RESULT_ERROR;
    }

    if (t_prev->dtype == PP_DTYPE_OBJECT_OPEN) {
        //DEBUG("FOUND KEY\n");
        struct PPToken t_cpy = *t;
        t_cpy.dtype = PP_DTYPE_KEY;
        pp_stack_put(&(pp->stack), t_cpy);
        pp->handle_data_cb(pp, t->dtype, pp->user_data);
    }
    else if (t_prev->dtype == PP_DTYPE_ARRAY_OPEN) {
        t->dtype = PP_DTYPE_STRING;
        pp_stack_put(&(pp->stack), *t);
        pp->handle_data_cb(pp, t->dtype, pp->user_data);
        pp_stack_pop(&(pp->stack));
    }
    else if (t_prev->dtype == PP_DTYPE_KEY) {
        t->dtype = PP_DTYPE_STRING;
        pp_stack_put(&(pp->stack), *t);
        pp->handle_data_cb(pp, t->dtype, pp->user_data);
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

    struct PPToken *t = pp_stack_get_from_end(pp, 0);
    struct PPToken *t_prev = pp_stack_get_from_end(pp, 1);
    struct PPToken *t_prev_prev = pp_stack_get_from_end(pp, 2);
    ASSERTF(t != NULL, "Callback received empty stack!");

    switch (dtype) {
        case PP_DTYPE_OBJECT_OPEN:
            pp_print_spaces(spaces * pp->stack.pos);
            if (t_prev != NULL && t_prev->dtype == PP_DTYPE_KEY)
                INFO("%s: \n", t_prev->data);

            INFO("OBJECT_OPEN\n");
            break;

        case PP_DTYPE_OBJECT_CLOSE:
            pp_print_spaces(spaces * pp->stack.pos - spaces);
            INFO("OBJECT_CLOSE\n");
            break;

        case PP_DTYPE_ARRAY_OPEN:
            pp_print_spaces(spaces * pp->stack.pos - spaces);
            if (t_prev != NULL && t_prev->dtype == PP_DTYPE_KEY) {
                INFO("%s: ", t_prev->data);
            }

            INFO("ARRAY_OPEN\n");
            break;

        case PP_DTYPE_ARRAY_CLOSE:
            pp_print_spaces(spaces * pp->stack.pos - spaces);
            INFO("ARRAY_CLOSE\n");
            break;

        case PP_DTYPE_STRING:
            pp_print_spaces(spaces * pp->stack.pos - spaces);
            if (t_prev_prev != NULL && t_prev_prev->dtype == PP_DTYPE_KEY && t_prev != NULL && t_prev->dtype == PP_DTYPE_ARRAY_OPEN)
                pp_print_spaces(spaces);
            if (t_prev != NULL && t_prev->dtype == PP_DTYPE_KEY)
                INFO("%s: ", t_prev->data);

            INFO("[STRING:%03ld] %s\n", strlen(t->data), t->data);
            break;
        case PP_DTYPE_BOOL:
            pp_print_spaces(spaces * pp->stack.pos - spaces);
            if (t_prev != NULL && t_prev->dtype == PP_DTYPE_KEY)
                INFO("%s: ", t_prev->data);
            INFO("[BOOL] %s\n", t->data);
            break;

        case PP_DTYPE_NUMBER:
            pp_print_spaces(spaces * pp->stack.pos - spaces);
            if (t_prev != NULL && t_prev->dtype == PP_DTYPE_KEY)
                INFO("%s: ", t_prev->data);
            INFO("[NUMBER] %s\n", t->data);
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

    pp.max_tokens = 0;
    pp.t_skip_is_set = 0;
    pp.zero_rd_cnt = 0;
    pp.user_data = NULL;
    pp.handle_data_cb = data_cb;

    struct PPToken t_string = pp_token_init();
    struct PPToken t_object_open = pp_token_init();
    struct PPToken t_object_close = pp_token_init();
    struct PPToken t_array_open = pp_token_init();
    struct PPToken t_array_close = pp_token_init();
    struct PPToken t_true = pp_token_init();
    struct PPToken t_false = pp_token_init();
    struct PPToken t_number = pp_token_init();
    struct PPToken t_test = pp_token_init();


    //t_number.capture_start_str = "<aaa";
    //t_number.capture_end_str   = "bbb>";
    //
    //
    //

    t_true.capt_start_str = "true";
    t_true.capt_end_str = NULL;
    t_true.allow_chars    = ", \n";
    t_true.save_chars    = "a";
    //t_true.end            = "ue";
    t_true.any            = "";
    t_true.delim_chars    = ",";

    t_true.ignore_chars   = "\r\n\t :,";
    t_true.dtype          = PP_DTYPE_BOOL;
    t_true.match_type     = PP_MATCH_ANY;
    t_true.greedy         = PP_METHOD_GREEDY;
    t_true.cb             = pp_json_bool_cb;
    t_true.step_over      = 1;
    pp_add_parse_token(&pp, t_true);

    t_test.capt_start_str = "<";
    t_test.capt_end_str   = ">";
    //t_test.delim_chars    = "b";
    t_test.save_chars     = NULL;
    t_test.allow_chars    = ", :\n";
    t_test.illegal_chars  = NULL;

    t_test.any            = "";
    t_test.dtype          = PP_DTYPE_STRING;
    t_test.match_type     = PP_MATCH_ANY;
    t_test.greedy         = PP_METHOD_GREEDY;
    t_test.cb             = pp_json_string_cb;
    t_test.step_over      = 1;
    pp_add_parse_token(&pp, t_test);

    t_number.delim_chars    = ",]}\n";
    t_number.save_chars     = "0123456789-null.";
    t_number.allow_chars    = ":, 0123456789-null.";
    t_number.illegal_chars    = NULL;

    t_number.any            = ": 0123456789-null.";
    t_number.dtype          = PP_DTYPE_NUMBER;
    t_number.match_type     = PP_MATCH_ANY;
    t_number.greedy         = PP_METHOD_GREEDY;
    t_number.cb             = pp_json_number_cb;
    t_number.step_over      = 1;
    pp_add_parse_token(&pp, t_number);

    t_string.start          = PP_JSON_STRING_OPEN;
    t_string.end            = PP_JSON_STRING_CLOSE;
    t_string.ignore_chars   = "\r\n\t :,";
    t_string.dtype          = PP_DTYPE_STRING;
    t_string.greedy         = PP_METHOD_NON_GREEDY;
    t_string.match_type     = PP_MATCH_START_END;
    t_string.cb             = pp_json_string_cb;
    t_string.step_over      = 1;
    pp_add_parse_token(&pp, t_string);

    t_object_open.start          = PP_JSON_OBJECT_OPEN;
    t_object_open.ignore_chars   = "\r\n\t :,";
    t_object_open.dtype          = PP_DTYPE_OBJECT_OPEN;
    t_object_open.greedy         = PP_METHOD_NON_GREEDY;
    t_object_open.match_type     = PP_MATCH_START;
    t_object_open.cb             = pp_json_object_open_cb;
    t_object_open.step_over      = 1;
    pp_add_parse_token(&pp, t_object_open);

    t_object_close.start          = PP_JSON_OBJECT_CLOSE;
    t_object_close.ignore_chars   = "\r\n\t ,";
    t_object_close.dtype          = PP_DTYPE_OBJECT_CLOSE;
    t_object_close.greedy         = PP_METHOD_NON_GREEDY;
    t_object_close.match_type     = PP_MATCH_START;
    t_object_close.cb             = pp_json_object_close_cb;
    t_object_close.step_over      = 1;
    pp_add_parse_token(&pp, t_object_close);

    t_array_open.start          = PP_JSON_ARRAY_OPEN;
    t_array_open.ignore_chars   = "\r\n\t :,";
    t_array_open.dtype          = PP_DTYPE_ARRAY_OPEN;
    t_array_open.greedy         = PP_METHOD_NON_GREEDY;
    t_array_open.match_type     = PP_MATCH_START;
    t_array_open.cb             = pp_json_array_open_cb;
    t_array_open.step_over      = 1;
    pp_add_parse_token(&pp, t_array_open);

    t_array_close.start          = PP_JSON_ARRAY_CLOSE;
    t_array_close.ignore_chars   = "\r\n\t ,";
    t_array_close.dtype          = PP_DTYPE_ARRAY_CLOSE;
    t_array_close.match_type     = PP_MATCH_START;
    t_array_close.greedy         = PP_METHOD_NON_GREEDY;
    t_array_close.cb             = pp_json_array_close_cb;
    t_array_close.step_over      = 1;
    pp_add_parse_token(&pp, t_array_close);



    // this will capture numbers and bool
    //pp_add_parse_token(&pp, ":",                         ",",                      "\r\n\t ", PP_DTYPE_STRING,       pp_json_string_cb,  PP_METHOD_GREEDY);

    return pp;
}

