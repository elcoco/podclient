#include "podcast.h"


struct Podcast podcast_init()
{
    struct Podcast pod;
    pod.podcast[0] = '\0';
    pod.episode[0] = '\0';
    pod.guid[0] = '\0';
    pod.timestamp[0] = '\0';

    pod.action = POD_ACTION_UNDEFINED;
    pod.started = -1;
    pod.position = -1;
    pod.total = -1;

    return pod;
}


int podcast_serialize(struct Podcast *pod, char *buf)
{
    const char *fmt = "{ \"podcast\" : \"%s\", \
\"episode\" : \"%s\", \
\"timestamp\" : \"%s\", \
\"guid\" : \"%s\", \
\"action\" : \"%s\", \
\"position\" : \"%d\", \
\"started\" : \"%d\", \
\"total\" : \"%d\" }";

    char action[16] = "";
    switch (pod->action) {
        case POD_ACTION_DOWNLOAD:
            strcpy(action, "download");
            break;
        case POD_ACTION_DELETE:
            strcpy(action, "delete");
            break;
        case POD_ACTION_PLAY:
            strcpy(action, "play");
            break;
        case POD_ACTION_NEW:
            strcpy(action, "new");
            break;
        case POD_ACTION_FLATTR:
            strcpy(action, "flattr");
            break;
        default:
            ERROR("Failed to serialize podcast, no valid action\n");
            return -1;
    }

    sprintf(buf, fmt, pod->podcast,
                      pod->episode,
                      pod->timestamp,
                      pod->guid,
                      action,
                      pod->position,
                      pod->started,
                      pod->total);
    return 0;
}
