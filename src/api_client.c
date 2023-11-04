#include "api_client.h"
#include "podcast.h"

struct JSON json;
int bytes_read = 0;

static int str_rm_from_start(char *str, size_t n)
{
    if (strlen(str) < n)
        return -1;

    char *lptr = str;
    char *rptr = str+n;
    while (*rptr != '\0')
        *lptr++ = *rptr++;

    *(lptr + 1) = '\0';
    return 0;
}


static size_t ac_req_read_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    struct APIClientRData *data = userdata;
    size_t chunksize = size * nmemb;

    DEBUG("\n\n\n***** STARTING PARSING ******************************\n")
    // NOTE: oldsize are the chars that were not succesfully read when parsing JSON
    size_t oldsize = strlen(data->data);


    // This should never happen
    if (chunksize > API_CLIENT_MAX_RDATA) {
        ERROR("Chunksize is too big! %ld > %d\n", chunksize, API_CLIENT_MAX_RDATA);
        return CURLE_WRITE_ERROR;
    }


    /* copy as much data as possible into the 'ptr' buffer, but no more than
     'size' * 'nmemb' bytes! */
    //memcpy(data->data + oldsize, ptr, sizeof(data->data));
    //data->data[chunksize] = '\0';
    memcpy(data->data, ptr, chunksize);
    data->data[chunksize] = '\0';

    // TODO if unread_data is empty, json_parse doesn't work
    //char *chunks[2] = {data->unread_data, data->data};

    // parse data and remove read bytes from string
    char *chunks[2];
    if (strlen(data->unread_data) > 0) {
        chunks[0] = data->unread_data;
        chunks[1] = data->data;
    }
    else {
        chunks[0] = data->data;
        chunks[1] = NULL;
    }


    int nread = json_parse(&json, chunks, sizeof(chunks)/sizeof(*chunks));
    if (nread < 0) {
        ERROR("ABORT!!!!!!\n");
        return CURLE_WRITE_ERROR;
    }
    // nread is the index in a strcat string so index doesn't correspond with data->data

    //data->data[nread] = '\0';
    //
    //DEBUG("NEW DATA: %s\n\n", data->data);

    bytes_read += nread;
    //DEBUG("UNREAD: %s\n", data_unread);
    

    if (nread < chunksize)
        strcpy(data->unread_data, ptr+nread);
    else
        data->unread_data[0] = '\0';
    //memcpy(data->unread_data, ptr+nread, chunksize-nread);
    //data->data[chunksize-nread] = '\0';


    DEBUG("Bytes read: %d, total: %d\n", nread, bytes_read);
    //DEBUG("DATA: %s\n", data->data);
    return chunksize;
}

enum APIClientReqResult ac_req_get(struct APIClient *client, const char* url, struct APIClientRData *rdata, char *pdata, long* response_code)
{
    CURL *curl = curl_easy_init();
    if (!curl)
        return API_CLIENT_REQ_CURL_ERROR;

    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_USERNAME, client->user);
    curl_easy_setopt(curl, CURLOPT_PASSWORD, client->key);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    //curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, client->timeout);
    //curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 1L);

    // when reading data 
    if (rdata != NULL) {
        printf("Setting rdata\n");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ac_req_read_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, rdata);
    }
    // when posting data
    if (pdata != NULL)
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, pdata);

    // CURL_MAX_WRITE_SIZE

    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, response_code);
    curl_easy_cleanup(curl);

    // checks for readerror from ac_req_read_cb()
    if (res == CURLE_WRITE_ERROR)
        return API_CLIENT_REQ_OUT_OF_MEMORY;

    return API_CLIENT_REQ_SUCCESS;
}

enum APIClientReqResult ac_get_subscriptions(struct APIClient *client)
{
    long status_code;
    char url[256] = "";
    sprintf(url, API_CLIENT_URL_FMT, client->server, API_CLIENT_SUBSCRIPTIONS);

    struct APIClientRData rdata;
    rdata.data[0] = '\0';
    rdata.size = 0;


    if (ac_req_get(client, url, &rdata, NULL, &status_code) < 0) {
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


    DEBUG("status_code: %ld\n", status_code);
    DEBUG("data: %s\n", rdata.data);
    return API_CLIENT_REQ_SUCCESS;
}

void handle_data_cb(struct JSON *json, struct JSONItem *ji)
{
    DEBUG("Handling data: %s\n", ji->data);
}

enum APIClientReqResult ac_get_episodes(struct APIClient *client, struct Podcast *pod, time_t since)
{
    long status_code;
    char url[512] = "";
    char param[128] = "";

    char pod_json[PODCAST_MAX_SERIALIZED] = "";
    json = json_init(handle_data_cb);

    if (podcast_serialize(pod, pod_json) < 0) {
        ERROR("Failed to get podcast\n");
        return API_CLIENT_REQ_SERIALIZE_ERROR;
    }

    sprintf(url, API_CLIENT_URL_FMT, client->server, API_CLIENT_EPISODE_ACTION);

    if (since >= 0)
        sprintf(param, "?since=%ld", since);

    strncat(url, param, sizeof(url)-strlen(url)-1);

    DEBUG("Serialized: %s\n", pod_json);
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
    //DEBUG("data: %s\n", rdata.data);
    char buf[] = "{ \
                  \"actions\": [ \
                    { \
                      \"podcast\": \"https://media.rss.com/steakandeggscast/feed.xml\", \
                      \"episode\": \"https://media.rss.com/steakandeggscast/2023_09_22_09_18_08_1463937f-c833-4c4a-b27e-b66848bf013a.mp3\", \
                      \"timestamp\": \"2023-11-04T07:13:17\", \
                      \"guid\": \"d78bd6a9-e843-4dde-a6d7-b1ade25affbd\", \
                      \"position\": [], \
                      \"started\": 6233, \
                      \"total\": 6811, \
                      \"action\": \"PLAY\" \
                    }, \
                    { \
                      \"podcast\": \"https://anchor.fm/s/23c4a914/podcast/rss\", \
                      \"episode\": \"https://anchor.fm/s/23c4a914/podcast/play/78070380/https%3A%2F%2Fd3ctxlq1ktw2nl.cloudfront.net%2Fstaging%2F2023-10-2%2F353801394-44100-2-14630cd552cd6.mp3\", \
                      \"timestamp\": \"2023-11-04T09:31:34\", \
                      \"guid\": \"7793b3d3-6494-4a8f-bce8-6740beff9ed6\", \
                      \"position\": 2741, \
                      \"started\": 2099, \
                      \"total\": 6350, \
                      \"bever\" : true, \
                      \"disko\" : false, \
                      \"action\": \"PLAY\" \
                    } \
                  ], \
                  \"timestamp\": 1699090473 \
                } \
                ";
    

    return API_CLIENT_REQ_SUCCESS;
}
