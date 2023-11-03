#ifndef APICLIENT_H
#define APICLIENT_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <curl/curl.h>

#include "utils.h"
#include "podcast.h"


#define API_CLIENT_MAX_SERVER 64
#define API_CLIENT_MAX_USER   64
#define API_CLIENT_MAX_KEY    64
#define API_CLIENT_MAX_RDATA  100 * 1024

#define API_CLIENT_URL_FMT    "%s/index.php/apps/gpoddersync/%s"
#define API_CLIENT_SUBSCRIPTIONS "subscriptions"
#define API_CLIENT_EPISODE_ACTION "episode_action"




enum APIClientReqResult {
    API_CLIENT_REQ_SERIALIZE_ERROR,
    API_CLIENT_REQ_ERROR,
    API_CLIENT_REQ_UNKNOWN_ERROR,
    API_CLIENT_REQ_NOTFOUND,
    API_CLIENT_REQ_SUCCESS = 0
};

struct APIClientRData {
    char data[API_CLIENT_MAX_RDATA];
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



enum APIClientReqResult ac_get_subscriptions(struct APIClient *client);
enum APIClientReqResult ac_get_episodes(struct APIClient *client, struct Podcast *pod, time_t since);





#endif
