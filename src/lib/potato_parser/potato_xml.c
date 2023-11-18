#include "potato_xml.h"
#include "potato_parser.h"

//#define DO_DEBUG 1
//#define DO_INFO  1
//#define DO_ERROR 1

#define DEBUG(M, ...) if(do_debug){fprintf(stdout, "[DEBUG] " M, ##__VA_ARGS__);}
#define INFO(M, ...) if(do_info){fprintf(stdout, M, ##__VA_ARGS__);}
#define ERROR(M, ...) if(do_error){fprintf(stderr, "[ERROR] (%s:%d) " M, __FILE__, __LINE__, ##__VA_ARGS__);}

#define ASSERTF(A, M, ...) if(!(A)) {fprintf(stderr, M, ##__VA_ARGS__); assert(A); }


void pp_xml_param_to_string(struct PPXMLParam *params, int max_amount, char *buf, int max_buf)
{
    struct PPXMLParam *param = params;

    for (int i=0 ; i<max_amount ; i++, param++) {
        if (param->key == NULL || param->value == NULL || strlen(param->key) == 0)
            break;
        char param_buf[256] = "";
        sprintf(param_buf, "%s=%s, ", param->key, param->value);
        strncat(buf, param_buf, max_buf);
    }
}

void pp_xml_handle_data_cb(struct PP *pp, enum PPDtype dtype, void *user_data)
{
    /* Callback can be used, instead of custom callback, to display full xml data */
    const int spaces = 2;

    struct PPItem *xi = pp_stack_get_from_end(pp, 0);
    ASSERTF(xi != NULL, "Callback received empty stack!");

    switch (dtype) {
        case PP_DTYPE_TAG_OPEN:
            pp_print_spaces(spaces * pp->stack.pos);
            char param_buf[512] = "";
            pp_xml_param_to_string(xi->param, PP_XML_MAX_PARAM, param_buf, 512);
            if (strlen(param_buf) != 0) {
                INFO("TAG_OPEN: %s, param: %s\n", xi->data, param_buf);
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
    // Current closing tag can also be a buffer overflow. now we don't have a way to check if
    // the XML is consistent. large xml tags are probably caused by loads of parameters. So
    // another way to fix this is to just drop the parameters and only keep the tag.
    if (strncmp(item->data, PP_BUFFER_OVERFLOW_PLACEHOLDER, PP_MAX_DATA) != 0 && strncmp(item->data, item_prev->data, PP_MAX_DATA) != 0 && strncmp(item_prev->data, PP_BUFFER_OVERFLOW_PLACEHOLDER, PP_MAX_DATA) != 0) {
        ERROR("Unexpected closing tag found, previous open tag doesn't correspond: >%s<\n", item->data);
        pp_xml_stack_debug(&(pp->stack));
        return PP_PARSE_RESULT_ERROR;
    }

    pp_stack_put(&(pp->stack), *item);
    pp->handle_data_cb(pp, item->dtype, pp->user_data);
    pp_stack_pop(&(pp->stack));
    pp_stack_pop(&(pp->stack));

    return PP_PARSE_RESULT_SUCCESS;
}

static void pp_xml_param_sanitize(struct PPXMLParam *param)
{
    /* Remove quotes around value, they're stupid */
    if (strlen(param->value) <= 0)
        return;

    if (param->value[0] == '"')
        param->value++;
    if (param->value[strlen(param->value)-1] == '"')
        *(param->value + strlen(param->value) -1) = '\0';
}

static int pp_xml_item_parse_parameters(struct PPItem *item, char *str, size_t max_amount)
{
    /* Parse parameters into structs.
     * Return amount of parameters parsed or -1 on error */
    struct PPXMLParam *ptr = item->param;
    char *rest;
    int i = 0;

    for (; i<max_amount ; i++, ptr++) {

        if (pp_str_split_at_char(str, ' ', &rest) < 0)
            break;

        if (pp_str_split_at_char(str, '=', &ptr->value) < 0) {
            ERROR("Failed to parse parameter: %s\n", str);
            i = -1;
            break;
        }
        ptr->key = str;
        pp_xml_param_sanitize(ptr);

        //DEBUG("Parameter: %s = %s\n", ptr->key, ptr->value);
        str = rest;
    }
    return i;
}


enum PPParseResult pp_xml_tag_open_cb(struct PP *pp, struct PPParserEntry *pe, struct PPItem *item)
{
    // TODO: check if tag ends with />, if so, remove from stack
    assert(item->dtype == PP_DTYPE_TAG_OPEN);  // Test if item is right type
                                               //
    int is_single_line = 0;

    // NOTE: Need to look at previous char to determine if this is a single line but there may
    //       be no previous characters. Could be in previous chunk?
    //       When doning skip, this information is potentially lost because we report all bytes
    //       as parsed to the caller. So these do not return on the next call
    if (strcmp(item->data, PP_BUFFER_OVERFLOW_PLACEHOLDER) == 0) {
        if (pp->pos.npos < 1) {
            DEBUG("Like to look behind to determine single line tag, but no chars");
        }
        else if (*(pp->pos.c-1) == '/') {
            DEBUG("open tag is a single line\n")
            is_single_line = 1;
        }
    }

    // remove trailing slash that closes the tag
    if (str_ends_with(item->data, "/")) {
        item->data[strlen(item->data)-1] = '\0';
        is_single_line = 1;
    }

    char *param_str;
    pp_str_split_at_char(item->data, ' ', &param_str);

    if (param_str != NULL)
        pp_xml_item_parse_parameters(item, param_str, PP_XML_MAX_PARAM);

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

