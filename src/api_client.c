#include "api_client.h"
#include "podcast.h"

static size_t ac_req_post_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    DEBUG("!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    return 0;
}

static size_t ac_req_read_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    DEBUG("BLA\n");
    struct APIClientRData *data = userdata;
    size_t realsize = size * nmemb;

    if (data->size + realsize > API_CLIENT_MAX_RDATA) {
        ERROR("Out of memory!\n");
        return 0;
    }

    /* copy as much data as possible into the 'ptr' buffer, but no more than
     'size' * 'nmemb' bytes! */
    memcpy(data->data + data->size, ptr, realsize);

    data->size += realsize;
    data->data[data->size] = '\0';

    DEBUG("Read %ld bytes\n", realsize);
    return realsize;
}

static int8_t ac_req_get(struct APIClient *client, const char* url, struct APIClientRData *rdata, char *pdata, long* response_code)
{
    CURL *curl = curl_easy_init();
    if(curl) {
        CURLcode res;
        curl_easy_setopt(curl, CURLOPT_USERNAME, client->user);
        curl_easy_setopt(curl, CURLOPT_PASSWORD, client->key);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        //curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, client->timeout);

        // when reading data 
        if (rdata != NULL) {
            printf("Setting rdata\n");
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, ac_req_read_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, rdata);
        }
        // when posting data
        if (pdata != NULL) {
            printf("Setting pdata\n");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, pdata);
        }
        // CURL_MAX_WRITE_SIZE

        res = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, response_code);
        curl_easy_cleanup(curl);
        return res;
    }
    return -1;
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

enum APIClientReqResult ac_get_episodes(struct APIClient *client, struct Podcast *pod, time_t since)
{
    long status_code;
    char url[256] = "";
    char pod_json[PODCAST_MAX_SERIALIZED] = "";

    if (podcast_serialize(pod, pod_json) < 0) {
        ERROR("Failed to get podcast\n");
        return API_CLIENT_REQ_SERIALIZE_ERROR;
    }

    DEBUG("Serialized: %s\n", pod_json);

    sprintf(url, API_CLIENT_URL_FMT, client->server, API_CLIENT_EPISODE_ACTION);

    if (since >= 0)
        sprintf(url, "%s?since=%ld", url, since);

    DEBUG("url: %s\n", url);

    struct APIClientRData rdata;
    rdata.data[0] = '\0';
    rdata.size = 0;

    struct APIClientPData pdata;
    pdata.data[0] = '\0';
    pdata.size = 0;

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
