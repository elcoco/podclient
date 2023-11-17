#include "potato_xml.h"
#include "potato_parser.h"

#define DO_DEBUG 1
#define DO_INFO  1
#define DO_ERROR 1

#define DEBUG(M, ...) if(DO_DEBUG){fprintf(stdout, "[DEBUG] " M, ##__VA_ARGS__);}
#define INFO(M, ...) if(DO_INFO){fprintf(stdout, M, ##__VA_ARGS__);}
#define ERROR(M, ...) if(DO_ERROR){fprintf(stderr, "[ERROR] (%s:%d) " M, __FILE__, __LINE__, ##__VA_ARGS__);}

#define ASSERTF(A, M, ...) if(!(A)) {ERROR(M, ##__VA_ARGS__); assert(A); }



void pp_xml_handle_data_cb(struct PP *pp, enum PPDtype dtype, void *user_data)
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

enum PPParseResult pp_xml_string_cb(struct PP *pp, struct PPParserEntry *pe, struct PPItem *item)
{
    assert(item->dtype == PP_DTYPE_STRING);  // Test if item is right type
                                             
    struct PPItem *item_prev = pp_stack_get_from_end(pp, 0);
    if (item_prev == NULL) {
        ERROR("Unexpected string found, previous stack item == NULL: %s\n", item->data);
        pp_xml_stack_debug(&(pp->stack));
        return PP_PARSE_RESULT_ERROR;
    }
    else if (item_prev->dtype != PP_DTYPE_TAG_OPEN) {
        ERROR("Unexpected string found, previous stack item is not an opening tag: %s\n", item->data);
        pp_xml_stack_debug(&(pp->stack));
        //return PP_PARSE_RESULT_ERROR;
    }
    if (strlen(item->data) == 0) {
        ERROR("String is empty!\n");
        pp_xml_stack_debug(&(pp->stack));
        return PP_PARSE_RESULT_ERROR;
    }

    pp_stack_put(&(pp->stack), *item);
    pp->handle_data_cb(pp, item->dtype, pp->user_data);
    pp_stack_pop(&(pp->stack));
    return PP_PARSE_RESULT_SUCCESS;

}

enum PPParseResult pp_xml_tag_close_cb(struct PP *pp, struct PPParserEntry *pe, struct PPItem *item)
{
    assert(item->dtype == PP_DTYPE_TAG_CLOSE);  // Test if item is right type
                                                
    struct PPItem *item_prev = pp_stack_get_from_end(pp, 0);
    if (item_prev == NULL) {
        ERROR("Unexpected closing tag found, stack item is empty: %s\n", item->data);
        pp_xml_stack_debug(&(pp->stack));
        return PP_PARSE_RESULT_ERROR;
    }
    if (item_prev->dtype != PP_DTYPE_TAG_OPEN) {
        ERROR("Unexpected closing tag found, previous item is not an opening tag: %s\n", item->data);
        pp_xml_stack_debug(&(pp->stack));
        return PP_PARSE_RESULT_ERROR;
    }

    // Because of buffer overflow, previous tag can be added with a placeholder in it
    if (strcmp(item->data, item_prev->data) != 0 && strcmp(item_prev->data, PP_BUFFER_OVERFLOW_PLACEHOLDER)) {
        ERROR("Unexpected closing tag found, previous open tag doesn't correspond: %s\n", item->data);
        pp_xml_stack_debug(&(pp->stack));
        return PP_PARSE_RESULT_ERROR;
    }

    pp_stack_put(&(pp->stack), *item);
    pp->handle_data_cb(pp, item->dtype, pp->user_data);
    pp_stack_pop(&(pp->stack));
    pp_stack_pop(&(pp->stack));
    return PP_PARSE_RESULT_SUCCESS;
}

enum PPParseResult pp_xml_tag_open_cb(struct PP *pp, struct PPParserEntry *pe, struct PPItem *item)
{
    // TODO: check if tag ends with />, if so, remove from stack
    assert(item->dtype == PP_DTYPE_TAG_OPEN);  // Test if item is right type
                                               //
    /*
    struct PPItem *item_prev = pp_stack_get_from_end(pp, 0);
    if (item_prev != NULL && item_prev->dtype != PP_DTYPE_TAG_CLOSE) {
        ERROR("Unexpected opening tag found, previous item is not a closing tag: %s\n", item->data);
        pp_xml_stack_debug(&(pp->stack));
        return PP_PARSE_RESULT_ERROR;
    }
    */
    
    // FIXME why segfault when uncomment
    //DEBUG("BEFORE: %s\n", item->data);
    int is_single_line = 0;

    // remove trailing slash that closes the tag
    if (str_ends_with(item->data, "/")) {
        item->data[strlen(item->data)-1] = '\0';
        is_single_line = 1;
    }

    pp_str_split_at_char(item->data, ' ', &(item->param));
    pp_stack_put(&(pp->stack), *item);
    pp->handle_data_cb(pp, item->dtype, pp->user_data);

    if (is_single_line)
        pp_stack_pop(&(pp->stack));

    return PP_PARSE_RESULT_SUCCESS;
}

struct PP pp_xml_init(handle_data_cb data_cb)
{
    
    struct PP pp;
    pp_stack_init(&(pp.stack));

    pp.max_entries = 0;
    //pp.skip_str[0] = '\0';
    pp.skip_is_set = 0;
    pp.zero_rd_cnt = 0;
    pp.user_data = NULL;
    pp.handle_data_cb = data_cb;

    pp_add_parse_entry(&pp, PP_XML_CHAR_COMMENT_START,   PP_XML_CHAR_COMMENT_END,               PP_DTYPE_COMMENT,   NULL,             PP_METHOD_GREEDY);
    pp_add_parse_entry(&pp, PP_XML_CHAR_CDATA_START,     PP_XML_CHAR_CDATA_END,                 PP_DTYPE_CDATA,     NULL,             PP_METHOD_GREEDY);
    pp_add_parse_entry(&pp, PP_XML_CHAR_HEADER_START,    PP_XML_CHAR_HEADER_END,                PP_DTYPE_HEADER,    NULL,             PP_METHOD_GREEDY);
    pp_add_parse_entry(&pp, PP_XML_CHAR_TAG_CLOSE_START, PP_XML_CHAR_TAG_CLOSE_END,             PP_DTYPE_TAG_CLOSE, pp_xml_tag_close_cb, PP_METHOD_GREEDY);
    pp_add_parse_entry(&pp, PP_XML_CHAR_TAG_OPEN_START,  PP_XML_CHAR_TAG_OPEN_END,              PP_DTYPE_TAG_OPEN,  pp_xml_tag_open_cb,  PP_METHOD_GREEDY);
    pp_add_parse_entry(&pp, "",                          "<",                                   PP_DTYPE_STRING,    pp_xml_string_cb,  PP_METHOD_NON_GREEDY);

    return pp;
}

