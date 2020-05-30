/* DroidCam & DroidCamX (C) 2010-
 * https://github.com/aramg
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <linux/limits.h>

#include "common.h"
#include "settings.h"
#include "connection.h"
#include "decoder.h"

int v_running = 1;
int a_running = 0;
int thread_cmd = 0;
struct settings g_settings = {0};

extern char snd_device[32];
extern char v4l2_device[32];
void * AudioThreadProc(void * args);
void * VideoThreadProc(void * args);

void sig_handler(int sig) {
    a_running = 0;
    v_running = 0;
    return;
}

void ShowError(const char * title, const char * msg) {
    errprint("%s: %s\n", title, msg);
}

static inline void usage(int argc, char *argv[]) {
    fprintf(stderr, "Usage: \n"
    " %s -l <port>\n"
    "   Listen on 'port' for connections\n"
    "\n"
    " %s <ip> <port> [-add-audio]\n"
    "   Connect to 'ip' on 'port'\n"
    ,
    argv[0], argv[0]);
}


int main(int argc, char *argv[]) {
    pthread_t athread, vthread;
    int athread_rc = -1, vthread_rc = -1;

    if (argc == 3 && argv[1][0] == '-' && argv[1][1] == 'l') {
        g_settings.port = atoi(argv[2]);
        g_settings.connection = CB_WIFI_SRVR;
    }
    else if (argc >= 3) {
        strncpy(g_settings.ip, argv[1], sizeof(g_settings.ip));
        g_settings.port = atoi(argv[2]);
        g_settings.connection = CB_RADIO_WIFI;
        if (argc == 4 && argv[3][0] == '-' && argv[3][1] == 'a')
            g_settings.audio = 1;
    }
    else {
        usage(argc, argv);
        return 1;
    }

    if (!decoder_init()) {
        return 2;
    }
    printf("Client v" APP_VER_STR "\n"
            "Video: %s\n"
            "Audio: %s\n",
            v4l2_device, snd_device);

    if (v_running) {
        SOCKET videoSocket = INVALID_SOCKET;
        if (g_settings.connection == CB_RADIO_WIFI) {
            videoSocket = connect_droidcam(g_settings.ip, g_settings.port);
            if (videoSocket == INVALID_SOCKET) {
                errprint("Video: Connect failed to %s:%d\n", g_settings.ip, g_settings.port);
                return 0;
            }
        }
        vthread_rc = pthread_create(&vthread, NULL, VideoThreadProc, (void *) (SOCKET_PTR) videoSocket);

        if (videoSocket != INVALID_SOCKET && g_settings.audio) {
            a_running = 1;
            athread_rc = pthread_create(&athread, NULL, AudioThreadProc, NULL);
        }
    }

    signal(SIGINT, sig_handler);
    signal(SIGHUP, sig_handler);
    while (v_running)
        usleep(2000);

    dbgprint("joining\n");
    sig_handler(SIGHUP);
    if (athread_rc == 0) pthread_join(athread, NULL);
    if (vthread_rc == 0) pthread_join(vthread, NULL);
    decoder_fini();
    dbgprint("exit\n");
    return 0;
}
