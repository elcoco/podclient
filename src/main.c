#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "utils.h"
#include "api_client.h"

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

int main(int argc, char **argv)
{
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

    ac_get_subscriptions(&client);


}
