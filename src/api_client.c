#include "api_client.h"
#include "lib/potato_parser/potato_parser.h"

#define DEBUG(M, ...) if(do_debug){fprintf(stdout, "[DEBUG] " M, ##__VA_ARGS__);}
#define INFO(M, ...) if(do_info){fprintf(stdout, M, ##__VA_ARGS__);}
#define ERROR(M, ...) if(do_error){fprintf(stderr, "[ERROR] (%s:%d) " M, __FILE__, __LINE__, ##__VA_ARGS__);}

//struct JSON json;
int bytes_read = 0;

static void ac_unescape(char *str)
{
    /* Find backslashes and remove them from string */
    for (int i=0 ; i<strlen(str) ; i++) {
        if (str[i] == '\\') {
            // move everything to the left
            char *lptr = str + i;
            char *rptr = str + i + 1;
            while (*rptr != '\0')
                *lptr++ = *rptr++;
            *lptr = '\0';
        }
    }
}

static char* ac_str_sanitize(char *str)
{
    /* Remove, replace and lower a string */
    char *str_ptr = str;

    for (int i=0 ; i<strlen(str) ; i++, str_ptr++) {
        if (strchr(API_CLIENT_SANITIZE_REMOVE_CHARS, *str_ptr) != NULL) {
            char *lptr = str+i;
            char *rptr = str+i+1;
            while (*rptr != '\0')
                *lptr++ = *rptr++;
            *lptr = '\0';
        }
        else if (strchr(API_CLIENT_SANITIZE_REPLACE_CHARS, *str_ptr) != NULL) {
            *str_ptr = '_';
        }
        else if (*str_ptr >= 'A' && *str_ptr <= 'Z') {
            *str_ptr -= 'A'-'a';
        }
    }
    return str;
}

static int write_to_file(char *path, const char *mode, const char *fmt, ...)
{
    va_list ptr;
    va_start(ptr, fmt);

    int res = mkdir(dirname(path), 0755);
    if (res < 0 && errno != EEXIST) {
        ERROR("Failed to create dir\n");
        perror(NULL);
        return -1;
    }
    path[strlen(path)] = '/';

    FILE *fp = fopen(path, mode);
    if (fp == NULL) {
        ERROR("Failed to open file for writing, %s\n", path);
        return -1;
    }
    vfprintf(fp, fmt, ptr);
    fclose(fp);

    va_end(ptr);
    return 0;
}


static void episodes_handle_data_cb(struct PP *pp, enum PPDtype dtype, void *user_data)
{
    /* Callback is passed to json lib to handle incoming data.
     * Data is saved in podcast struct */
    struct PPToken *item = pp_stack_get_from_end(pp, 0);
    struct APIUserData *data = user_data;
    struct Episode *ep = data->data;

    if (dtype == PP_DTYPE_TAG_OPEN && strcmp(item->data, "enclosure") == 0) {
        for (int i=0 ; i<PP_XML_MAX_PARAM ; i++) {
            if (strcmp(item->param[i].key, "url") == 0) {
                strncpy(ep->url, item->param[i].value, PODCAST_MAX_URL);
                //DEBUG("Found url!\n");
                break;
            }
        }
    }
    else if (dtype == PP_DTYPE_TAG_CLOSE && strcmp(item->data, "item") == 0) {
        char path[256] = "";
        sprintf(path, "%s/%s/%s.json", API_CLIENT_BASE_DIR, API_CLIENT_POD_DIR, ac_str_sanitize(ep->podcast->title));
        write_to_file(path, "a", EPISODE_JSON_FMT, ep->title, ep->guid, ep->url);
        ep->url[0] = '\0';
        ep->guid[0] = '\0';
        ep->title[0] = '\0';
    }
    else if (dtype == PP_DTYPE_STRING || dtype == PP_DTYPE_CDATA) {
        struct PPToken *item_tag = pp_stack_get_from_end(pp, 1);
        struct PPToken *item_item = pp_stack_get_from_end(pp, 2);


        if (item_item != NULL && item_item->dtype == PP_DTYPE_TAG_OPEN && strcmp(item_item->data, "channel") == 0) {
            if (strcmp(item_tag->data, "title") == 0) {
                strcpy(ep->podcast->title, item->data);
                //DEBUG("PODCAST TITLE: %s\n", item->data);
                printf("   %s\n", item->data);


                char path[256] = "";
                sprintf(path, "%s/%s/%s.json", API_CLIENT_BASE_DIR, API_CLIENT_POD_DIR, ac_str_sanitize(ep->podcast->title));
                write_to_file(path, "w", "[\n");
            }
        }

        else if (item_item != NULL && item_item->dtype == PP_DTYPE_TAG_OPEN && strcmp(item_item->data, "item") == 0) {

            if (strcmp(item_tag->data, "title") == 0) {
                strncpy(ep->title, item->data, PODCAST_MAX_TITLE);
                printf("   - %s\n", item->data);
            }
            else if (strcmp(item_tag->data, "guid") == 0) {
                strncpy(ep->guid, item->data, PODCAST_MAX_GUID);
                //DEBUG("GUID:  %s\n", item->data);
            }
        }
    }
}

static size_t ac_req_json_read_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    struct APIUserData *data = userdata;
    struct JSON *json = data->parser;

    size_t chunksize = size * nmemb;

    // This should never happen
    if (chunksize > API_CLIENT_MAX_RDATA) {
        ERROR("Chunksize is too big! %ld > %d\n", chunksize, API_CLIENT_MAX_RDATA);
        return CURLE_WRITE_ERROR;
    }

    /* copy as much data as possible into the 'ptr' buffer, but no more than
     'size' * 'nmemb' bytes! */
    memcpy(data->chunk, ptr, chunksize);
    data->chunk[chunksize] = '\0';
    ac_unescape(data->chunk);

    // TODO if unread_data is empty, json_parse doesn't work
    //char *chunks[2] = {data->unread_data, data->data};

    // parse data and remove read bytes from string
    char *chunks[2];
    if (strlen(data->unread_chunk) > 0) {
        chunks[0] = data->unread_chunk;
        chunks[1] = data->chunk;
    }
    else {
        chunks[0] = data->chunk;
        chunks[1] = NULL;
    }

    int nread = json_parse(json, chunks, sizeof(chunks)/sizeof(*chunks));
    if (nread < 0)
        return CURLE_WRITE_ERROR;

    DEBUG("read: %s\n", data->chunk);
    bytes_read += nread;

    // if not all chars could be read as json, store them in data->unread_data
    // and pass as first chunk next time
    // NOTE if a string is larger than a chunk this will not work because in that
    // case the JSON lib needs more data to find string boundaries.
    if (nread < chunksize)
        strcpy(data->unread_chunk, ptr+nread);
    else
        data->unread_chunk[0] = '\0';

    DEBUG("Bytes read: %d, total: %d\n", nread, bytes_read);
    return chunksize;
}

static size_t ac_req_xml_read_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    struct APIUserData *data = userdata;
    struct PP *pp = data->parser;

    size_t chunksize = size * nmemb;

    // This should never happen
    if (chunksize > API_CLIENT_MAX_RDATA) {
        ERROR("Chunksize is too big! %ld > %d\n", chunksize, API_CLIENT_MAX_RDATA);
        return CURLE_WRITE_ERROR;
    }

    /* copy as much data as possible into the 'ptr' buffer, but no more than
     'size' * 'nmemb' bytes! */
    memcpy(data->chunk, ptr, chunksize);
    data->chunk[chunksize] = '\0';
    ac_unescape(data->chunk);

    // TODO if unread_data is empty, json_parse doesn't work
    //char *chunks[2] = {data->unread_data, data->data};

    // parse data and remove read bytes from string
    char *chunks[2];
    if (strlen(data->unread_chunk) > 0) {
        chunks[0] = data->unread_chunk;
        chunks[1] = data->chunk;
    }
    else {
        chunks[0] = data->chunk;
        chunks[1] = NULL;
    }

    int nread = pp_parse(pp, chunks, sizeof(chunks)/sizeof(*chunks));
    if (nread < 0)
        return CURLE_WRITE_ERROR;

    //DEBUG("read: %s\n", data->chunk);
    //bytes_read += nread;

    // if not all chars could be read as json, store them in data->unread_data
    // and pass as first chunk next time
    // NOTE if a string is larger than a chunk this will not work because in that
    // case the JSON lib needs more data to find string boundaries.
    if (nread < chunksize)
        strcpy(data->unread_chunk, ptr+nread);
    else
        data->unread_chunk[0] = '\0';

    //DEBUG("Bytes read/parsed %ld/%d Bytes\n", nmemb*size, nread);
    //DEBUG("Bytes left: %s\n", data->unread_chunk);
    return chunksize;
}

static enum APIClientReqResult ac_req_get(struct APIClient *client, const char* url, struct APIUserData *user_data,  curl_write_cb write_cb, long *status_code)
{
    CURL *curl = curl_easy_init();
    if (!curl)
        return API_CLIENT_REQ_CURL_ERROR;

    CURLcode res;
    if (strlen(client->user) > 0)
        curl_easy_setopt(curl, CURLOPT_USERNAME, client->user);

    if (strlen(client->key) > 0)
        curl_easy_setopt(curl, CURLOPT_PASSWORD, client->key);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, client->timeout);

    // when reading data 
    if (user_data != NULL) {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, user_data);
    }

    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status_code);
    curl_easy_cleanup(curl);

    if (res == CURLE_OPERATION_TIMEDOUT) {
        ERROR("Timeout occured\n");
        return API_CLIENT_REQ_CURL_ERROR;
    }
    // checks for readerror from ac_req_read_cb()
    else if (res == CURLE_WRITE_ERROR) {
        return API_CLIENT_REQ_PARSE_ERROR;
    }
    else if (res != CURLE_OK) {
        ERROR("CURL error: %d\n", res);
        return API_CLIENT_REQ_CURL_ERROR;
    }

    //DEBUG("CURL CODE: %d\n", res);

    return API_CLIENT_REQ_SUCCESS;
}

static void ac_subscripions_handle_data_cb(struct JSON *json, enum JSONEvent ev, void *user_data)
{
    /* Callback is passed to json lib to handle incoming data.
     * Data is saved in podcast struct */
    struct JSONItem *ji = stack_get_from_end(json, 0);
    struct APIUserData *data = user_data;
    struct Podcast *pods = data->data;

    if (data->npod >= data->data_length) {
        ERROR("Failed to save podcast data, data limit reached: %d\n", data->data_length);
        return;
    }

    if (ev == JSON_EV_STRING) {
        struct JSONItem *ji_prev = stack_get_from_end(json, 1);
        if (ji_prev != NULL && ji_prev->dtype == JSON_DTYPE_ARRAY) {
            struct Podcast *pod = &(pods[data->npod]);
            strcpy(pod->url, ji->data);
            //DEBUG("Found xml: %s\n", ji->data); 
            data->npod++;
        }
    }
}

enum APIClientReqResult ac_get_subscriptions(struct APIClient *client, struct Podcast *pods, size_t pods_length, size_t *pods_found)
{
    long status_code;
    char url[512] = "";
    sprintf(url, API_CLIENT_URL_FMT, client->server, API_CLIENT_SUBSCRIPTIONS);

    struct APIUserData user_data;
    struct JSON json = json_init(ac_subscripions_handle_data_cb);

    // set data that is passed to our handler callback
    json.user_data = &user_data;

    memset(pods, 0, sizeof(struct Podcast) * pods_length);

    user_data.data = pods;
    user_data.npod = 0;
    user_data.data_length = pods_length;

    user_data.parser = &json;
    user_data.chunk[0] = '\0';
    user_data.unread_chunk[0] = '\0';
    *pods_found = 0;

    if (ac_req_get(client, url, &user_data, ac_req_json_read_cb, &status_code) < 0) {
        ERROR("Failed to make request\n");
        return API_CLIENT_REQ_UNKNOWN_ERROR;
    }

    if (status_code == 401) {
        ERROR("Server returned 401, NOT FOUND!\n");
        return API_CLIENT_REQ_NOTFOUND;
    }

    if (status_code != 200) {
        ERROR("Server returned unhandled error, %ld!\n", status_code);
        return API_CLIENT_REQ_UNKNOWN_ERROR;
    }

    *pods_found = user_data.npod;

    DEBUG("status_code: %ld\n", status_code);
    return API_CLIENT_REQ_SUCCESS;
}

enum APIClientReqResult get_episodes(struct APIClient *client, struct Podcast *pod)
{
    struct APIUserData user_data;

    // callback will be called on new parsed xml data
    struct PP pp = pp_xml_init(pp_xml_handle_data_cb);
    //struct PP pp = pp_xml_init(episodes_handle_data_cb);

    pp.user_data = &user_data;
    struct Episode ep;
    memset(&ep, 0, sizeof(struct Episode));
    ep.podcast = pod;

    user_data.data = &ep;
    user_data.parser = &pp;
    user_data.chunk[0] = '\0';
    user_data.unread_chunk[0] = '\0';

    long status_code;

    enum APIClientReqResult res = ac_req_get(client, pod->url, &user_data, ac_req_xml_read_cb, &status_code);

    if (res == API_CLIENT_REQ_SUCCESS)
        assert(pp.stack.pos == -1);  // not all tags were parsed


    // callback will be called when curl read new data from stream
    //if (res < 0) {
    //    ERROR("Failed to make request\n");
    //    return API_CLIENT_REQ_UNKNOWN_ERROR;
    //}

    if (status_code == 401) {
        ERROR("Server returned 401, NOT FOUND!\n");
        return API_CLIENT_REQ_NOTFOUND;
    }

    if (status_code != 200) {
        ERROR("Server returned unhandled error, %ld!\n", status_code);
        return API_CLIENT_REQ_UNKNOWN_ERROR;
    }

    //DEBUG("status_code: %ld\n", status_code);
    //return API_CLIENT_REQ_SUCCESS;
    return res;

}

/*
enum APIClientReqResult ac_get_actions(struct APIClient *client, time_t since)
{
    long status_code;
    char url[512] = "";
    char param[128] = "";

    json = json_init(json_handle_data_cb);

    sprintf(url, API_CLIENT_URL_FMT, client->server, API_CLIENT_EPISODE_ACTION);

    if (since >= 0)
        sprintf(param, "?since=%ld", since);

    strncat(url, param, sizeof(url)-strlen(url)-1);

    DEBUG("url: %s\n", url);

    struct APIClientRData rdata;
    rdata.data[0] = '\0';
    rdata.unread_data[0] = '\0';
    rdata.size = 0;

    enum APIClientReqResult res;
    if ((res = ac_req_get(client, url, &rdata, NULL, &status_code)) < API_CLIENT_REQ_SUCCESS) {
        ERROR("Failed to make request\n");
        return res;
    }

    if (status_code == 401) {
        ERROR("Server returned 401, NOT FOUND!\n");
        return API_CLIENT_REQ_NOTFOUND;
    }

    if (status_code != 200) {
        ERROR("Server returned unhandled error, %ld!\n", status_code);
        return API_CLIENT_REQ_UNKNOWN_ERROR;
    }

    DEBUG("status_code: %ld\n", status_code);
    
    return API_CLIENT_REQ_SUCCESS;
}
*/

