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

int v_running = 0;
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
    "   Listen on 'port' for connections (video only)\n"
    "\n"
    " %s [-a] [-v] <ip> <port>\n"
    "   Connect to ip:port with audio and/or video\n"
    "\n"
    " %s [-a] [-v] adb <port>\n"
    "   Connect via adb with audio and/or video\n"
    "\n"
    "Input '?' for list of commands while streaming.\n"
    ,
    argv[0], argv[0], argv[0]);
}

static void parse_args(int argc, char *argv[]) {
    if (argc == 3 && argv[1][0] == '-' && argv[1][1] == 'l') {
        g_settings.port = atoi(argv[2]);
        g_settings.connection = CB_WIFI_SRVR;
        v_running = 1;
        return;
    }

    if (argc >= 3) {
        int i = 1;
        for (; i < argc; i++) {
            if (argv[i][0] == '-' && argv[i][1] == 'a') {
                a_running = 1;
                continue;
            }
            if (argv[i][0] == '-' && argv[i][1] == 'v') {
                v_running = 1;
                continue;
            }
            break;
        }
        if (i > (argc - 2))
            goto ERROR;

        strncpy(g_settings.ip, argv[i], sizeof(g_settings.ip));
        g_settings.port = atoi(argv[i+1]);

        if (strcmp(g_settings.ip, "adb") == 0) {
            strcpy(g_settings.ip, ADB_LOCALHOST_IP);
            g_settings.connection = CB_RADIO_ADB;
        }
        else {
            g_settings.connection = CB_RADIO_WIFI;
        }

        if (!v_running && !a_running) v_running = 1;

        return;
    }

ERROR:
    usage(argc, argv);
    exit(1);
}

void wait_command() {
    char buf[1];
    int flags;
    ssize_t len;

    flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags < 0)
      return;

    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    while (v_running) {
        len = read(STDIN_FILENO, buf, 1);
        if (len == 0)
            return;

        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(2000);
                continue;
            }
            return;
        }

        switch(buf[0]) {
            case '?':
                printf("DroidCamX Commands:\n");
                printf("A: Auto-focus\n");
                printf("L: Toggle Flash\n");
                printf("+: Zoom In\n");
                printf("-: Zoom Out\n");
                break;
            case '=':
            case '+':
                thread_cmd = (CB_CONTROL_ZIN-10);
                break;
            case '-':
                thread_cmd = (CB_CONTROL_ZOUT-10);
                break;
            case 'a':
            case 'A':
                thread_cmd = (CB_CONTROL_AF-10);
                break;
            case 'l':
            case 'L':
                thread_cmd = (CB_CONTROL_LED-10);
                break;
        }
    }
}

int main(int argc, char *argv[]) {
    pthread_t athread, vthread;
    int athread_rc = -1, vthread_rc = -1;

    parse_args(argc, argv);

    if (!decoder_init()) {
        return 2;
    }

    printf("Client v" APP_VER_STR "\n");
    if (v_running) {
        printf("Video: %s\n", v4l2_device);
        SOCKET videoSocket = INVALID_SOCKET;
        if (g_settings.connection == CB_RADIO_WIFI || g_settings.connection == CB_RADIO_ADB) {

            if (g_settings.connection == CB_RADIO_ADB && CheckAdbDevices(g_settings.port) != 8)
                return 1;

            videoSocket = connect_droidcam(g_settings.ip, g_settings.port);
            if (videoSocket == INVALID_SOCKET) {
                errprint("Video: Connect failed to %s:%d\n", g_settings.ip, g_settings.port);
                return 0;
            }
        }
        vthread_rc = pthread_create(&vthread, NULL, VideoThreadProc, (void*) (SOCKET_PTR) videoSocket);
    }

    if (a_running){
        printf("Audio: %s\n", snd_device);
        if (!v_running && g_settings.connection == CB_RADIO_ADB && CheckAdbDevices(g_settings.port) != 8)
            return 1;

        athread_rc = pthread_create(&athread, NULL, AudioThreadProc, NULL);
    }

    signal(SIGINT, sig_handler);
    signal(SIGHUP, sig_handler);
    wait_command();
    while (v_running || a_running)
        usleep(2000);

    dbgprint("joining\n");
    sig_handler(SIGHUP);
    if (athread_rc == 0) pthread_join(athread, NULL);
    if (vthread_rc == 0) pthread_join(vthread, NULL);
    decoder_fini();
    dbgprint("exit\n");
    return 0;
}
