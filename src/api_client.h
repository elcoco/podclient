#ifndef APICLIENT_H
#define APICLIENT_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <libgen.h>    // basename, dirname
#include <sys/stat.h>  // mkdir
#include <errno.h>

#include <curl/curl.h>

//#include "utils.h"
#include "podcast.h"
#include "lib/json/json.h"

#include "lib/potato_parser/potato_xml.h"

#include "podcast.h"

#define API_CLIENT_BASE_DIR "test"
#define API_CLIENT_POD_DIR  "podcasts"

#define API_CLIENT_MAX_SERVER 64
#define API_CLIENT_MAX_USER   64
#define API_CLIENT_MAX_KEY    64
#define API_CLIENT_MAX_RDATA  17 * 1024
#define API_CLIENT_MAX_SUBSCRIPTIONS 64
#define API_CLIENT_MAX_PODCAST 256

#define API_CLIENT_URL_FMT    "%s/index.php/apps/gpoddersync/%s"
#define API_CLIENT_SUBSCRIPTIONS "subscriptions"
#define API_CLIENT_EPISODE_ACTION "episode_action"


#define API_CLIENT_SANITIZE_REMOVE_CHARS "\t\r\n'\"/\\<>"
#define API_CLIENT_SANITIZE_REPLACE_CHARS "- "


#define JSON_READ_CHUNK_SIZE CURL_MAX_WRITE_SIZE

extern int do_debug;
extern int do_info;
extern int do_error;

typedef size_t(*curl_write_cb)(char*, size_t, size_t, void*);

enum APIClientReqResult {
    API_CLIENT_REQ_OUT_OF_MEMORY,
    API_CLIENT_REQ_PARSE_ERROR,
    API_CLIENT_REQ_SERIALIZE_ERROR,
    API_CLIENT_REQ_ERROR,
    API_CLIENT_REQ_CURL_ERROR,
    API_CLIENT_REQ_UNKNOWN_ERROR,
    API_CLIENT_REQ_NOTFOUND,
    API_CLIENT_REQ_SUCCESS
};

struct APIClientRData {
    // string can hold at most twice the chunk size used by CURL
    char data[API_CLIENT_MAX_RDATA+1];
    char unread_data[API_CLIENT_MAX_RDATA+1];
    size_t size;
};

struct APIClientPData {
    char data[API_CLIENT_MAX_RDATA];
    size_t size;
    size_t limit;
};

struct APIClient {
    char server[API_CLIENT_MAX_SERVER];
    char user[API_CLIENT_MAX_USER];
    char key[API_CLIENT_MAX_KEY];
    int  port;

    long  timeout;
};

// Is passed to curl callback as user data.
struct APIUserData {

    // pointer to array of structs where data should be stored, eg: Podcast or Episode
    void *data;
    int data_length;

    // index in pods to current pod
    int npod;

    // parser object, eg: json or xml
    void *parser;

    // holds current chunk and unread data from previous chunk
    char chunk[API_CLIENT_MAX_RDATA+1];
    char unread_chunk[API_CLIENT_MAX_RDATA+1];
};



enum APIClientReqResult ac_get_subscriptions(struct APIClient *client, struct Podcast *pods, size_t pods_length, size_t *pods_found);
enum APIClientReqResult ac_get_actions(struct APIClient *client, time_t since);
enum APIClientReqResult get_episodes(struct APIClient *client, struct Podcast *pod);


#endif
