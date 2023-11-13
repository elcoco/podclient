#ifndef PODCAST_H
#define PODCAST_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "lib/xml/xml.h"
#include "utils.h"

enum PodFields {
    POD_FIELD_PODCAST,
    POD_FIELD_EPISODE,
    POD_FIELD_GUID,
    POD_FIELD_ACTION,
    POD_FIELD_TIMESTAMP,
    POD_FIELD_POSITION,
    POD_FIELD_STARTED,
    POD_FIELD_TOTAL,
};

static const char *pod_fields[] = {
    "podcast",
    "episode",
    "guid",
    "action",
    "timestamp",
    "position",
    "started",
    "total"
};

enum PodActions {
    POD_ACTION_UNDEFINED = -1,
    POD_ACTION_DOWNLOAD,
    POD_ACTION_DELETE,
    POD_ACTION_PLAY,
    POD_ACTION_NEW,
    POD_ACTION_FLATTR
};

#define EPISODE_JSON_FMT "    {\"title\" : \"%s\", \"guid\" : \"%s\", \"url\" : \"%s\"},\n"

#define PODCAST_MAX_URL       256
#define PODCAST_MAX_EPISODES   32
#define PODCAST_MAX_GUID      256
#define PODCAST_MAX_TITLE     256
#define PODCAST_MAX_ACTION     32
#define PODCAST_MAX_TIMESTAMP 64

#define PODCAST_MAX_SERIALIZED 1024*2

struct Podcast {
    char url[PODCAST_MAX_URL];
    char title[PODCAST_MAX_TITLE];
    size_t ep_length;
    size_t ep_max;
};

struct Episode {
    struct Podcast *podcast;
    //char podcast_title[PODCAST_MAX_TITLE];
    char url[PODCAST_MAX_URL];
    char guid[PODCAST_MAX_GUID];
    char title[PODCAST_MAX_TITLE];
    int started;
    int position;
    int total;
};

struct EpisodeAction {
    struct Podcast pod;
    struct Episode ep;
    enum PodActions action;
    char timestamp[PODCAST_MAX_TIMESTAMP];
    int started;
    int position;
    int total;
};

struct Podcast podcast_init();
int podcast_add_episode(struct Podcast *pod, struct Episode ep);

int podcast_serialize(struct Podcast *pod, char *buf);


#endif
