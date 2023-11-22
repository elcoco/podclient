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
        strncat(buf, param->key, max_buf-1);
        strncat(buf, " = ", max_buf-1);
        strncat(buf, param->value, max_buf-1);
        strncat(buf, ", ", max_buf-1);

        //char param_buf[256] = "";
        //snprintf(param_buf, 256, "%s=%s, ", param->key, param->value);
        //strncat(buf, param_buf, max_buf-1);
    }
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

static int pp_xml_token_parse_parameters(struct PPToken *t, char *str, size_t max_amount)
{
    /* Parse parameters into structs.
     * Return amount of parameters parsed or -1 on error */
    struct PPXMLParam *ptr = t->param;
    char *rest = str;
    int i = 0;

    for (; i<max_amount ; i++, ptr++) {
        // End of data
        if (rest == NULL || strlen(rest) == 0)
            break;

        char *value;

        if (pp_str_split_at_char(rest, '=', &value) < 0) {
            ERROR("Failed to parse parameter key, = not found: %s\n", rest);
            break;
        }

        ptr->key = rest;

        if (*value != '"') {
            ERROR("Failed to parse parameter value, opening \" not found: %s\n", rest);
            break;
        }

        // delete opening quote
        value++;

        if (pp_str_split_at_char(value, '"', &rest) < 0) {
            ERROR("Failed to parse parameter value, closing \" not found: %s\n", rest);
            i = -1;
            break;
        }

        ptr->value = value;

        // remove space separator
        if (rest != NULL && *rest == ' ')
            rest++;

        //DEBUG("Found parameter: %s = %s\n", ptr->key, ptr->value);
    }
    return i;
}


void pp_xml_handle_data_cb(struct PP *pp, enum PPDtype dtype, void *user_data)
{
    /* Callback can be used, instead of custom callback, to display full xml data */
    const int spaces = 2;

    struct PPToken *t = pp_stack_get_from_end(pp, 0);
    ASSERTF(t != NULL, "Callback received empty stack!");

    switch (dtype) {
        case PP_DTYPE_TAG_OPEN:
            pp_print_spaces(spaces * pp->stack.pos);
            char param_buf[512] = "";
            pp_xml_param_to_string(t->param, PP_XML_MAX_PARAM, param_buf, 512);
            if (strlen(param_buf) != 0) {
                INFO("TAG_OPEN: %s, param: %s\n", t->data, param_buf);
            }
            else {
              INFO("TAG_OPEN: %s\n", t->data);
            }
            break;
        case PP_DTYPE_TAG_CLOSE:
            pp_print_spaces(spaces * pp->stack.pos - spaces);
            INFO("TAG_CLOSE: %s\n", t->data);
            break;
        case PP_DTYPE_STRING:
            pp_print_spaces(spaces * pp->stack.pos);
            INFO("STRING: %s\n", t->data);
            break;
        case PP_DTYPE_CDATA:
            pp_print_spaces(spaces * pp->stack.pos);
            INFO("CDATA: %s\n", t->data);
            break;
        case PP_DTYPE_HEADER:
            pp_print_spaces(spaces * pp->stack.pos);
            INFO("HEADER: %s\n", t->data);
            break;
        case PP_DTYPE_COMMENT:
            pp_print_spaces(spaces * pp->stack.pos);
            INFO("COMMENT: %s\n", t->data);
            break;
    }
}

enum PPParseResult pp_xml_string_cb(struct PP *pp, struct PPToken *t)
{
    assert(t->dtype == PP_DTYPE_STRING);  // Test if token is right type
                                             
    struct PPToken *t_prev = pp_stack_get_from_end(pp, 0);
    if (t_prev == NULL) {
        ERROR("Unexpected string found, previous stack token == NULL: '%s'\n", t->data);
        pp_xml_stack_debug(&(pp->stack));
        return PP_PARSE_RESULT_ERROR;
    }
    else if (t_prev->dtype != PP_DTYPE_TAG_OPEN) {
        ERROR("Unexpected string found, previous stack token is not an opening tag: '%s'\n", t->data);
        pp_xml_stack_debug(&(pp->stack));
        //return PP_PARSE_RESULT_ERROR;
    }
    if (strlen(t->data) == 0) {
        ERROR("String is empty!\n");
        pp_xml_stack_debug(&(pp->stack));
        return PP_PARSE_RESULT_ERROR;
    }

    pp_stack_put(&(pp->stack), *t);
    pp->handle_data_cb(pp, t->dtype, pp->user_data);
    pp_stack_pop(&(pp->stack));
    return PP_PARSE_RESULT_SUCCESS;

}

enum PPParseResult pp_xml_tag_close_cb(struct PP *pp, struct PPToken *t)
{
    assert(t->dtype == PP_DTYPE_TAG_CLOSE);  // Test if item is right type
                                                
    struct PPToken *t_prev = pp_stack_get_from_end(pp, 0);
    if (t_prev == NULL) {
        ERROR("Unexpected closing tag found, stack token is empty: '%s'\n", t->data);
        pp_xml_stack_debug(&(pp->stack));
        return PP_PARSE_RESULT_ERROR;
    }
    if (t_prev->dtype != PP_DTYPE_TAG_OPEN) {
        ERROR("Unexpected closing tag found, previous token is not an opening tag: '%s'\n", t->data);
        pp_xml_stack_debug(&(pp->stack));
        return PP_PARSE_RESULT_ERROR;
    }

    // Because of buffer overflow, previous tag can be added with a placeholder in it
    // Current closing tag can also be a buffer overflow. now we don't have a way to check if
    // the XML is consistent. large xml tags are probably caused by loads of parameters. So
    // another way to fix this is to just drop the parameters and only keep the tag.
    if (strncmp(t->data, PP_BUFFER_OVERFLOW_PLACEHOLDER, PP_MAX_TOKEN_DATA) != 0 && strncmp(t->data, t_prev->data, PP_MAX_TOKEN_DATA) != 0 && strncmp(t_prev->data, PP_BUFFER_OVERFLOW_PLACEHOLDER, PP_MAX_TOKEN_DATA) != 0) {
        ERROR("Unexpected closing tag found, previous open tag doesn't correspond: '%s'\n", t->data);
        pp_xml_stack_debug(&(pp->stack));
        return PP_PARSE_RESULT_ERROR;
    }

    pp_stack_put(&(pp->stack), *t);
    pp->handle_data_cb(pp, t->dtype, pp->user_data);
    pp_stack_pop(&(pp->stack));
    pp_stack_pop(&(pp->stack));

    return PP_PARSE_RESULT_SUCCESS;
}


enum PPParseResult pp_xml_tag_open_cb(struct PP *pp, struct PPToken *t)
{
    // TODO: check if tag ends with />, if so, remove from stack
    assert(t->dtype == PP_DTYPE_TAG_OPEN);  // Test if token is right type
                                               //
    int is_single_line = 0;

    // NOTE: Need to look at previous char to determine if this is a single line but there may
    //       be no previous characters. Could be in previous chunk?
    //       When doning skip, this information is potentially lost because we report all bytes
    //       as parsed to the caller. So these do not return on the next call
    if (strcmp(t->data, PP_BUFFER_OVERFLOW_PLACEHOLDER) == 0) {
        if (pp->pos.npos < 1) {
            DEBUG("Like to look behind to determine single line tag, but no chars");
        }
        else if (*(pp->pos.c-1) == '/') {
            DEBUG("open tag is a single line\n")
            is_single_line = 1;
        }
    }

    DEBUG("SKIP BUF FROM TAG_OPEN: '%s'\n", t->skip_data);
    // When tag has buffer overflow, the data is lost and replaced with a placeholder (PP_BUFFER_OVERFLOW_PLACEHOLDER)
    // The found data is copied to t->skip_data
    // So look at this data to find out if its a single line tag
    if (strlen(t->skip_data) > 0 && str_ends_with(t->skip_data, "/")) {
        DEBUG("TAG_OPEN found skip data: %s\n", t->skip_data);

        is_single_line = 1;
    }

    // remove trailing slash that closes the tag
    if (str_ends_with(t->data, "/")) {
        t->data[strlen(t->data)-1] = '\0';
        is_single_line = 1;
    }

    char *param_str;
    pp_str_split_at_char(t->data, ' ', &param_str);

    // TODO fix this
    //if (param_str != NULL)
    //    pp_xml_token_parse_parameters(t, param_str, PP_XML_MAX_PARAM);

    pp_stack_put(&(pp->stack), *t);
    pp->handle_data_cb(pp, t->dtype, pp->user_data);

    if (is_single_line)
        pp_stack_pop(&(pp->stack));

    return PP_PARSE_RESULT_SUCCESS;
}

struct PP pp_xml_init(handle_data_cb data_cb)
{
    struct PP pp;
    pp_stack_init(&(pp.stack));

    pp.max_tokens = 0;
    //pp.skip_str[0] = '\0';
    pp.t_skip_is_set = 0;
    pp.zero_rd_cnt = 0;
    pp.user_data = NULL;
    pp.handle_data_cb = data_cb;

    struct PPToken t_comment = pp_token_init();
    struct PPToken t_cdata = pp_token_init();
    struct PPToken t_header = pp_token_init();
    struct PPToken t_tag_close = pp_token_init();
    struct PPToken t_tag_open = pp_token_init();
    struct PPToken t_string = pp_token_init();

    //struct PPToken t_test = pp_token_init();
    //t_test.capt_start_str = "<";
    ////t_test.capt_end_str   = "</bever>";
    //t_test.delim_chars    = ">";
    //t_test.allow_chars    = " \r\n\t";
    //t_test.dtype          = PP_DTYPE_COMMENT;
    //t_test.match_type     = PP_MATCH_ANY;
    //t_test.greedy         = PP_METHOD_NON_GREEDY;
    //t_test.cb             = NULL;
    //t_test.step_over      = 1;
    //pp_add_parse_token(&pp, t_test);

    //t_comment.start          = PP_XML_CHAR_COMMENT_START;
    //t_comment.end            = PP_XML_CHAR_COMMENT_END;
    //t_comment.ignore_chars   = " \r\n\t";
    //t_comment.dtype          = PP_DTYPE_COMMENT;
    //t_comment.match_type     = PP_MATCH_START_END;
    //t_comment.greedy         = PP_METHOD_NON_GREEDY;
    //t_comment.cb             = NULL;
    //t_comment.step_over      = 1;

    t_comment.capt_start_str = PP_XML_CHAR_COMMENT_START;
    t_comment.capt_end_str   = PP_XML_CHAR_COMMENT_END;
    t_comment.allow_chars    = " \r\n\t";
    t_comment.dtype          = PP_DTYPE_COMMENT;
    t_comment.greedy         = PP_METHOD_NON_GREEDY;
    t_comment.cb             = NULL;
    t_comment.step_over      = 1;

    //t_cdata.start            = PP_XML_CHAR_CDATA_START;
    //t_cdata.end              = PP_XML_CHAR_CDATA_END;
    //t_cdata.ignore_chars     = " \r\n\t";
    //t_cdata.match_type       = PP_MATCH_START_END;
    //t_cdata.dtype            = PP_DTYPE_CDATA;
    //t_cdata.greedy           = PP_METHOD_NON_GREEDY;
    //t_cdata.cb               = NULL;
    //t_cdata.step_over        = 1;
    
    t_cdata.capt_start_str   = PP_XML_CHAR_CDATA_START;
    t_cdata.capt_end_str     = PP_XML_CHAR_CDATA_END;
    t_cdata.allow_chars      = " \r\n\t";
    t_cdata.dtype            = PP_DTYPE_CDATA;
    t_cdata.greedy           = PP_METHOD_GREEDY;
    t_cdata.cb               = NULL;
    t_cdata.step_over        = 1;

    //t_header.start           = PP_XML_CHAR_HEADER_START;
    //t_header.end             = PP_XML_CHAR_HEADER_END;
    //t_header.ignore_chars    = " \r\n\t";
    //t_header.match_type      = PP_MATCH_START_END;
    //t_header.dtype           = PP_DTYPE_HEADER;
    //t_header.greedy          = PP_METHOD_NON_GREEDY;
    //t_header.cb              = NULL;
    //t_header.step_over       = 1;

    t_header.capt_start_str  = PP_XML_CHAR_HEADER_START;
    t_header.capt_end_str    = PP_XML_CHAR_HEADER_END;
    t_header.allow_chars     = " \r\n\t";
    t_header.dtype           = PP_DTYPE_HEADER;
    t_header.greedy          = PP_METHOD_NON_GREEDY;
    t_header.cb              = NULL;
    t_header.step_over       = 1;

    //t_tag_close.start        = PP_XML_CHAR_TAG_CLOSE_START;
    //t_tag_close.end          = PP_XML_CHAR_TAG_CLOSE_END;
    //t_tag_close.ignore_chars = " \r\n\t";
    //t_tag_close.match_type   = PP_MATCH_START_END;
    //t_tag_close.dtype        = PP_DTYPE_TAG_CLOSE;
    //t_tag_close.greedy       = PP_METHOD_NON_GREEDY;
    //t_tag_close.cb           = pp_xml_tag_close_cb;
    //t_tag_close.step_over    = 1;

    t_tag_close.capt_start_str = PP_XML_CHAR_TAG_CLOSE_START;
    t_tag_close.capt_end_str   = PP_XML_CHAR_TAG_CLOSE_END;
    t_tag_close.allow_chars    = " \r\n\t";
    t_tag_close.dtype          = PP_DTYPE_TAG_CLOSE;
    t_tag_close.greedy         = PP_METHOD_NON_GREEDY;
    t_tag_close.cb             = pp_xml_tag_close_cb;
    t_tag_close.step_over      = 1;

    //t_tag_open.start         = PP_XML_CHAR_TAG_OPEN_START;
    //t_tag_open.end           = PP_XML_CHAR_TAG_OPEN_END;
    //t_tag_open.ignore_chars  = " \r\n\t";
    //t_tag_open.match_type    = PP_MATCH_START_END;
    //t_tag_open.dtype         = PP_DTYPE_TAG_OPEN;
    //t_tag_open.greedy        = PP_METHOD_NON_GREEDY;
    //t_tag_open.cb            = pp_xml_tag_open_cb;
    //t_tag_open.step_over     = 1;

    t_tag_open.capt_start_str = PP_XML_CHAR_TAG_OPEN_START;
    t_tag_open.capt_end_str   = PP_XML_CHAR_TAG_OPEN_END;
    t_tag_open.allow_chars    = " \r\n\t";
    t_tag_open.dtype          = PP_DTYPE_TAG_OPEN;
    t_tag_open.greedy         = PP_METHOD_NON_GREEDY;
    t_tag_open.cb             = pp_xml_tag_open_cb;
    t_tag_open.step_over      = 1;

    //t_string.start         = "";
    //t_string.end             = "<";
    //t_string.ignore_chars    = " \r\n\t";
    //t_string.match_type      = PP_MATCH_START_END;
    //t_string.dtype           = PP_DTYPE_STRING;
    //t_string.greedy          = PP_METHOD_NON_GREEDY;
    //t_string.cb              = pp_xml_string_cb;
    //t_string.step_over       = 0;

    //t_string.start         = "";
    //t_string.end             = "<";
    t_string.delim_chars     = "<";
    t_string.dtype           = PP_DTYPE_STRING;
    t_string.greedy          = PP_METHOD_NON_GREEDY;
    t_string.cb              = pp_xml_string_cb;
    t_string.step_over       = 0;

    pp_add_parse_token(&pp, t_comment);
    pp_add_parse_token(&pp, t_cdata);
    pp_add_parse_token(&pp, t_header);
    pp_add_parse_token(&pp, t_tag_close);
    pp_add_parse_token(&pp, t_tag_open);
    pp_add_parse_token(&pp, t_string);
    return pp;
}
