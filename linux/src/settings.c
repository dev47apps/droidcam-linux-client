#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/limits.h>

#include "common.h"
#include "settings.h"

static inline FILE *GetFile(const char* mode) {
    char buf[PATH_MAX];
    snprintf(buf, sizeof(buf), "%s/.config/droidcam", getenv("HOME"));
    return fopen(buf, mode);
}

void LoadSettings(struct settings* settings) {
    char buf[512];
    int version = 0;
    FILE * fp = GetFile("r");

    // set defaults
    settings->audio = 0;
    settings->video = 1;
    settings->connection = CB_RADIO_WIFI;
    settings->ip[0] = 0;
    settings->port = 4747;

    if (!fp) {
        return;
    }

    if(fgets(buf, sizeof(buf), fp)){
        sscanf(buf, "v%d", &version);
    }

    if (version == 1) {
        if (fgets(buf, sizeof(buf), fp)){
            buf[strlen(buf)-1] = '\0';
            strncpy(settings->ip, buf, sizeof(settings->ip));
        }

        if (fgets(buf, sizeof(buf), fp)) {
            buf[strlen(buf)-1] = '\0';
            settings->port = atoi(buf);
        }
    }
    else if (version == 2 || version == 3) {
        if (fgets(buf, sizeof(buf), fp)){
            buf[strlen(buf)-1] = '\0';
            strncpy(settings->ip, buf, sizeof(settings->ip));
        }
        if (fgets(buf, sizeof(buf), fp)) {
            sscanf(buf, "%d", &settings->port);
        }
        if (fgets(buf, sizeof(buf), fp)) {
            sscanf(buf, "%d", &settings->audio);
        }
        if (version == 3) {
            if (fgets(buf, sizeof(buf), fp)) {
                sscanf(buf, "%d", &settings->video);
            }
        }
        if (fgets(buf, sizeof(buf), fp)) {
            sscanf(buf, "%d", &settings->connection);
        }
    }

    fclose(fp);
    dbgprint(
        "settings: ip=%s\n"
        "settings: port=%d\n"
        "settings: audio=%d\n"
        "settings: video=%d\n"
        "settings: connection=%d\n"
        ,
        settings->ip,
        settings->port,
        settings->audio,
        settings->video,
        settings->connection);
}

void SaveSettings(struct settings* settings) {
    int version = 3;
    FILE * fp = GetFile("w");
    if (!fp) return;

    fprintf(fp,
        "v%d\n"
        "%s\n"
        "%d\n"
        "%d\n"
        "%d\n"
        "%d\n"
        ,
        version,
        settings->ip,
        settings->port,
        settings->audio,
        settings->video,
        settings->connection);
    fclose(fp);
}
