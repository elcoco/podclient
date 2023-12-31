#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

//#include "utils.h"
#include "api_client.h"
#include "podcast.h"
#include "lib/json/json.h"
#include "lib/potato_parser/potato_xml.h"
#include "lib/potato_parser/potato_json.h"
#include "lib/regex/potato_regex.h"

#define SUCCESS 0

int do_debug = 0;
int do_info = 1;
int do_error = 1;

#define DEBUG(M, ...) if(do_debug){fprintf(stdout, "[DEBUG] " M, ##__VA_ARGS__);}
#define INFO(M, ...) if(do_info){fprintf(stdout, M, ##__VA_ARGS__);}
#define ERROR(M, ...) if(do_error){fprintf(stderr, "[ERROR] (%s:%d) " M, __FILE__, __LINE__, ##__VA_ARGS__);}

struct State {
    char server[API_CLIENT_MAX_SERVER];
    char user[API_CLIENT_MAX_USER];
    char key[API_CLIENT_MAX_KEY];
    char podcast[API_CLIENT_MAX_PODCAST];
    int  port;
    int  do_sync;
    int  do_download;
};

static struct State state_init()
{
    struct State s;
    s.server[0] = '\0';
    s.user[0] = '\0';
    s.key[0] = '\0';
    s.podcast[0] = '\0';
    s.port = 80;
    s.do_sync = 0;
    s.do_download = 0;
    return s;
}

static void show_help(struct State *s)
{
    printf("PODCLIENT - Gotta catch 'm all\n");
    printf("  -s    server\n");
    printf("  -p    port,   default=%d\n", s->port);
    printf("  -u    user\n");
    printf("  -k    key\n");
    printf("  -d    download episodes\n");
    printf("  -S    sync\n");
    printf("  -P    podcast url\n");
    printf("  -D    debugging\n");
}

static int atoi_err(char *str, int *buf)
{
    char *endptr;

    *buf = strtol(str, &endptr, 0);
    if (endptr == str)
        return -1;
    return 1;
}

static int parse_args(struct State *s, int argc, char **argv)
{
    int option;
    DEBUG("Parsing args\n");

    while((option = getopt(argc, argv, "s:p:P:u:k:hDSd")) != -1) {
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
            case 'P':
                strncpy(s->podcast, optarg, sizeof(s->podcast));
                break;
            case 'u':
                strncpy(s->user, optarg, sizeof(s->user));
                break;
            case 'k':
                strncpy(s->key, optarg, sizeof(s->key));
                break;
            case 'd':
                s->do_download = 1;
                break;
            case 'S':
                s->do_sync = 1;
                break;
            case 'D':
                do_debug = 1;
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

static void test_pp_xml()
{
    // FIXME sometimes nread (returned from json_parse())is less than what is actually parsed
    const char *path = "data/bpradio.rss";
    //const char *path = "data/test.xml";
    //const char *path = "data/test2.xml";

    // BUG: if TAG_OPEN overflows or falls inbetween passes, and it is single line, it will not close
    const int nchunks = 256;

    FILE *fp;
    size_t n;
    struct PP pp;
    char chunk[nchunks+1];
    char chunk_unread[nchunks+1];
    chunk[0] = '\0';
    chunk_unread[0] = '\0';
    char *chunks[2];
    

    pp = pp_xml_init(pp_xml_handle_data_cb);

    fp = fopen(path, "r");
    if (fp == NULL) {
        DEBUG("no such file, %s\n", path);
        return;
    }

    while (!feof(fp)) {

        n = fread(chunk, 1, nchunks, fp);

        chunk[n] = '\0';

        if (strlen(chunk_unread) > 0) {
            chunks[0] = chunk_unread;
            chunks[1] = chunk;
        }
        else {
            chunks[0] = chunk;
            chunks[1] = NULL;
        }

        int nread = pp_parse(&pp, chunks, sizeof(chunks)/sizeof(*chunks));
        if (nread < 0) {
            DEBUG("PPXML returns 0 read chars\n");
            break;
        }

        if (nread < nchunks && nread != 0)
            strcpy(chunk_unread, chunk+nread);
        else
            chunk_unread[0] = '\0';
    
    }
    INFO("CUR SIZE: xml:%ld \n", sizeof(pp));

    fclose(fp);
}


static int do_download_episodes(struct State *s)
{
    // FIXME sometimes nread (returned from json_parse())is less than what is actually parsed
    const int nchunks = 256;
    const char *path = "data/test.json";
    //const char *path = "test/podcasts/trash_taste_podcast.json";
    FILE *fp;
    size_t n;
    char chunk[nchunks+1];
    char chunk_unread[nchunks+1];
    chunk[0] = '\0';
    chunk_unread[0] = '\0';
    char *chunks[2];
    int ret = 0;


    //struct JSON json = json_init(json_handle_data_cb);
    struct PP pp = pp_json_init(pp_json_handle_data_cb);

    fp = fopen(path, "r");
    if (fp == NULL) {
        DEBUG("no such file, %s\n", path);
        return -1;
    }

    while (!feof(fp)) {

        n = fread(chunk, 1, nchunks, fp);
        //DEBUG("READ: %s\n", chunk);

        if (strlen(chunk_unread) > 0) {
            chunks[0] = chunk_unread;
            chunks[1] = chunk;
        }
        else {
            chunks[0] = chunk;
            chunks[1] = NULL;
        }

        //int nread = json_parse(&json, chunks, sizeof(chunks)/sizeof(*chunks));
        int nread = pp_parse(&pp, chunks, sizeof(chunks)/sizeof(*chunks));
        if (nread < 0) {
            DEBUG("JSON returns 0 read chars\n");
            ret = -1;
            break;
        }

        if (nread < nchunks && nread != 0)
            strcpy(chunk_unread, chunk+nread);
        else
            chunk_unread[0] = '\0';
        //DEBUG("Read: %d of %d\n", nread, chunk_size);
    
    }
    DEBUG("END\n");
    fclose(fp);
    return ret;
}

int do_sync_episodes(struct State *s)
{
    struct APIClient client;
    strncpy(client.server, s->server, API_CLIENT_MAX_SERVER);
    strncpy(client.user, s->user, API_CLIENT_MAX_USER);
    strncpy(client.key, s->key, API_CLIENT_MAX_KEY);
    client.port = s->port;
    client.timeout = 100L;

    if (strlen(s->podcast) > 0) {
        struct Podcast pod;
        strcpy(pod.url, s->podcast);
        printf("\n** %s\n", pod.url);
        if (get_episodes(&client, &pod) < API_CLIENT_REQ_SUCCESS)
            return -1;
    }
    else {

        struct Podcast pods[API_CLIENT_MAX_SUBSCRIPTIONS];
        size_t pods_found = 0;

        //ac_get_actions(&client, -1);
        ac_get_subscriptions(&client, pods, API_CLIENT_MAX_SUBSCRIPTIONS, &pods_found);

        for (int i=0 ; i<pods_found ; i++) {
            printf("\n** %s\n", pods[i].url);
            if (get_episodes(&client, &pods[i]) == API_CLIENT_REQ_PARSE_ERROR) {
                ERROR("Fail on: %s\n", pods[i].url);
                return -1;
            }
        }
    }
    return 0;
}

static void handle_local_episode_data_cb(struct JSON *json, enum JSONEvent ev, void *user_data)
{
    DEBUG("EVENT: %d\n", ev);
}

int main(int argc, char **argv)
{

    
    struct State s = state_init();
    if (parse_args(&s, argc, argv) < 0) {
        show_help(&s);
        return 0;
    }

    re_test();
    return 0;
    //test_pp_xml();
    //return 0;

    if (s.do_sync) {
        if (do_sync_episodes(&s) < 0)
            return 1;
    }
    if (s.do_download) {
        if (do_download_episodes(&s) < 0)
            return 1;
    }



    return 0;
}
