#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "utils.h"
#include "api_client.h"
#include "podcast.h"
#include "lib/json/json.h"

#define SUCCESS 0



struct State {
    char server[API_CLIENT_MAX_SERVER];
    char user[API_CLIENT_MAX_USER];
    char key[API_CLIENT_MAX_KEY];
    int  port;
};

struct State state_init()
{
    struct State s;
    s.server[0] = '\0';
    s.user[0] = '\0';
    s.key[0] = '\0';
    s.port = 80;
    return s;
}

void show_help(struct State *s)
{
    printf("PODCLIENT - Gotta catch 'm all\n");
    printf("  -s    server\n");
    printf("  -p    port,   default=%d\n", s->port);
    printf("  -u    user\n");
    printf("  -k    key\n");
}

static int atoi_err(char *str, int *buf)
{
    char *endptr;

    *buf = strtol(str, &endptr, 0);
    if (endptr == str)
        return -1;
    return 1;
}

int parse_args(struct State *s, int argc, char **argv)
{
    int option;
    DEBUG("Parsing args\n");

    while((option = getopt(argc, argv, "s:p:u:k:h")) != -1) {
        switch (option) {
            case 's':
                strncpy(s->server, optarg, sizeof(s->server));
                break;
            case 'p':
                if (atoi_err(optarg, &(s->port)) < 0) {
                    ERROR("Port is not a number: %s\n", optarg);
                    return -1;
                }
                break;
            case 'u':
                strncpy(s->user, optarg, sizeof(s->user));
                break;
            case 'k':
                strncpy(s->key, optarg, sizeof(s->key));
                break;
            case ':': 
                ERROR("Option needs a value\n"); 
                return -1;
            case 'h': 
                show_help(s);
                return -1;
            case '?': 
                show_help(s);
                return -1;
       }
    }
    if (strlen(s->user) <= 0)
        return -1;
    if (strlen(s->key) <= 0)
        return -1;
    if (strlen(s->server) <= 0)
        return -1;
    if (s->port < 0)
        return -1;

    return SUCCESS;
}

void read_json()
{
    // FIXME sometimes nread (returned from json_parse())is less than what is actually parsed
    const char *path = "data/sample01.json";
    const int chunk_size = 512;
    FILE *fp;
    size_t n;
    struct JSON json;
    char chunk[chunk_size+1];
    char chunk_unread[chunk_size+1];
    chunk[0] = '\0';
    chunk_unread[0] = '\0';
    char *chunks[2];


    json = json_init(json_handle_data_cb);

    fp = fopen(path, "r");
    if (fp == NULL) {
        DEBUG("no such file, %s\n", path);
        return;
    }

    while ((n = fread(chunk, 1, chunk_size, fp) > 0)) {
        //printf("\n");
        //DEBUG("CHUNK 0: >>%s<<\n", chunk_unread);
        //DEBUG("CHUNK 1: >>%s<<\n", chunk);


        if (strlen(chunk_unread) > 0) {
            chunks[0] = chunk_unread;
            chunks[1] = chunk;
        }
        else {
            chunks[0] = chunk;
            chunks[1] = NULL;
        }

        //DEBUG("read: %s\n", chunk);

        int nread = json_parse(&json, chunks, sizeof(chunks)/sizeof(*chunks));
        if (nread < 0) {
            DEBUG("JSON returns 0 read chars\n");
            break;
        }

        if (nread < chunk_size && nread != 0)
            strcpy(chunk_unread, chunk+nread);
        else
            chunk_unread[0] = '\0';
        //DEBUG("Read: %d of %d\n", nread, chunk_size);
    
    }
    INFO("CUR SIZE: json:%ld \n", sizeof(json));

    fclose(fp);

}

int main(int argc, char **argv)
{
    read_json();
    return 0;

    struct State s = state_init();
    if (parse_args(&s, argc, argv) < 0) {
        show_help(&s);
        return 1;
    }

    struct APIClient client;
    strncpy(client.server, s.server, API_CLIENT_MAX_SERVER);
    strncpy(client.user, s.user, API_CLIENT_MAX_USER);
    strncpy(client.key, s.key, API_CLIENT_MAX_KEY);
    client.port = s.port;
    client.timeout = 5L;

    //ac_get_subscriptions(&client);

    struct Podcast pod = podcast_init();
    pod.action = POD_ACTION_PLAY;
    strncpy(pod.guid, "2ff5675d-fba1-4bb4-b529-7db1b05fe6f6", PODCAST_MAX_GUID);
    ac_get_episodes(&client, &pod, -1);
    //ac_get_episodes(&client, &pod, 1699034956);


}
