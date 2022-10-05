/* DroidCam & DroidCamX (C) 2010-2021
 * https://github.com/dev47apps
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>

#include "common.h"
#include "settings.h"
#include "connection.h"
#include "decoder.h"

typedef struct Thread {
    pthread_t t;
    int rc;
} Thread;

Thread athread = {0, -1}, vthread = {0, -1}, dthread = {0, -1};

char *v4l2_dev = 0;
unsigned v4l2_width = 0, v4l2_height = 0;
volatile bool a_active = false;
volatile bool v_active = false;
volatile bool v_running = false;
volatile bool a_running = false;
volatile char thread_cmd = 0;
int no_controls = 0;
struct settings g_settings = {0};

extern const char *thread_cmd_val_str;
extern char snd_device[32];
extern char v4l2_device[32];
void * AudioThreadProc(void * args);
void * VideoThreadProc(void * args);
void * DecodeThreadProc(void * args);

void sig_handler(__attribute__((__unused__)) int sig) {
    a_running = false;
    v_running = false;
    return;
}

void ShowError(const char * title, const char * msg) {
    errprint("%s: %s\n", title, msg);
}

void UpdateBatteryLabel(char *battery_value) {
    (void) battery_value;
}

static inline void usage(__attribute__((__unused__)) int argc, char *argv[]) {
    fprintf(stderr, "Usage: \n"
    " %s -l <port>\n"
    "   Listen on 'port' for connections (video only)\n"
    "\n"
    " %s [options] <ip> <port>\n"
    "   Connect via ip\n"
    "\n"
    " %s [options] adb <port>\n"
    "   Connect via adb to Android device\n"
    "\n"
    " env ANDROID_SERIAL=<serial> %s [options] adb <port>\n"
    "   Connect via adb to Android device with serial number <serial>\n"
    "   (use `adb devices` to find serial number)\n"
    "\n"
    " %s [options] ios <port>\n"
    "   Connect via usbmuxd to iDevice\n"
    "\n"
    "Options:\n"
    " -a          Enable Audio\n"
    " -v          Enable Video\n"
    "             (only -v by default)\n"
    "\n"
    " -vflip      Apply vertical flip\n"
    " -hflip      Apply horizontal flip\n"
    "\n"
    " -nocontrols Disable controls and avoid reading from stdin.\n"
    "             Otherwise, enter '?' for list of commands while streaming.\n"
    "\n"
    " -dev=PATH   Specify v4l2loopback device to use, instead of first available.\n"
    "             Ex: -dev=/dev/video5\n"
    "\n"
    " -size=WxH   Specify video size (when using the regular v4l2loopback module)\n"
    "             Ex: 640x480, 1280x720, 1920x1080\n"
    "\n"
    ,
    argv[0],
    argv[0],
    argv[0],
    argv[0],
    argv[0]);
}

static void parse_args(int argc, char *argv[]) {
    if (argc >= 3) {
        int i = 1;
        for (; i < argc; i++) {
            if (argv[i][0] == '-' && argv[i][1] == 'v' && argv[i][2] == 'f' && argv[i][3] == 'l' && argv[i][5] == 'p') {
                g_settings.vertical_flip = 1;
                continue;
            }
            if (argv[i][0] == '-' && argv[i][1] == 'h' && argv[i][2] == 'f' && argv[i][3] == 'l' && argv[i][5] == 'p') {
                g_settings.horizontal_flip = 1;
                continue;
            }

            if (argv[i][0] == '-' && argv[i][1] == 'a') {
                a_running = 1;
                continue;
            }
            if (argv[i][0] == '-' && argv[i][1] == 'v') {
                v_running = true;
                continue;
            }

            if (argv[i][0] == '-' && argv[i][1] == 'd' && argv[i][3] == 'v') {
                if (argv[i][4] != '=' || argv[i][5] == 0)
                    goto ERROR;

                v4l2_dev = &argv[i][5];
                continue;
            }
            if (argv[i][0] == '-' && argv[i][1] == 's' && argv[i][3] == 'z') {
                if (sscanf(argv[i], "-size=%dx%d", &v4l2_width, &v4l2_height) != 2)
                    goto ERROR;
                continue;
            }

            if (argv[i][0] == '-' && strstr(&argv[i][1], "nocontrols") != NULL) {
                no_controls = 1;
                continue;
            }
            break;
        }
        if (i > (argc - 2))
            goto ERROR;

        if (argv[i][0] == '-' && argv[i][1] == 'l') {
            g_settings.port = strtoul(argv[i+1], NULL, 10);
            g_settings.connection = CB_WIFI_SRVR;
            a_running = false;
            v_running = true;
            return;
        }

        strncpy(g_settings.ip, argv[i], sizeof(g_settings.ip) - 1);
        g_settings.ip[sizeof(g_settings.ip) - 1] = '\0';
        g_settings.port = strtoul(argv[i+1], NULL, 10);

        if (strcmp(g_settings.ip, "adb") == 0) {
            g_settings.connection = CB_RADIO_ADB;
            memset(g_settings.ip, 0, sizeof(g_settings.ip));
            strncpy(g_settings.ip, ADB_LOCALHOST_IP, sizeof(g_settings.ip));
        }
        else if (strcmp(g_settings.ip, "ios") == 0) {
            g_settings.connection = CB_RADIO_IOS;
        }
        else {
            g_settings.connection = CB_RADIO_WIFI;
        }

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
                printf("M: Horizontal Flip / Mirror\n");
                printf("V: Vertical Flip\n");
                printf("A: Auto-focus\n");
                printf("L: Toggle Flash\n");
                printf("+: Zoom In\n");
                printf("-: Zoom Out\n");
                break;
            case '=':
            case '+':
                thread_cmd = CB_CONTROL_ZOOM_IN;
                break;
            case '-':
                thread_cmd = CB_CONTROL_ZOOM_OUT;
                break;
            case 'a':
            case 'A':
                thread_cmd = CB_CONTROL_AF;
                break;
            case 'l':
            case 'L':
                thread_cmd = CB_CONTROL_LED;
                break;
            case 'm':
            case 'M':
                decoder_horizontal_flip();
                break;
            case 'v':
            case 'V':
                decoder_vertical_flip();
                break;
        }
    }
}

int main(int argc, char *argv[]) {
    parse_args(argc, argv);

    if (!v_running && !a_running)
        v_running = true;

    if (!decoder_init(v4l2_dev, v4l2_width, v4l2_height)) {
        return 2;
    }

    printf("Client v" APP_VER_STR "\n");
    if (v_running) {
        printf("Video: %s\n", v4l2_device);
        SOCKET videoSocket = INVALID_SOCKET;
        if (g_settings.connection == CB_RADIO_WIFI || g_settings.connection == CB_RADIO_ADB || g_settings.connection == CB_RADIO_IOS) {

            if (g_settings.connection == CB_RADIO_ADB) {
                int rc = CheckAdbDevices(g_settings.port);
                if (rc != NO_ERROR) {
                    AdbErrorPrint(rc);
                    return 1;
                }
            }

            if (g_settings.connection == CB_RADIO_IOS) {
                int rc = CheckiOSDevices(g_settings.port);
                if (rc <= 0) {
                    iOSErrorPrint(rc);
                    return 1;
                }
                videoSocket = rc;
            }
            else {
                const char *errmsg = NULL;
                videoSocket = Connect(g_settings.ip, g_settings.port, &errmsg);
                if (videoSocket == INVALID_SOCKET) {
                    errprint("Video: Connect failed to %s:%d\n", g_settings.ip, g_settings.port);
                    if (errmsg) errprint("%s", errmsg);
                    return 0;
                }
            }
        }
        vthread.rc = pthread_create(&vthread.t, NULL, VideoThreadProc, (void*) (SOCKET_PTR) videoSocket);
        dthread.rc = pthread_create(&dthread.t, NULL, DecodeThreadProc, NULL);
    }

    if (a_running){
        printf("Audio: %s\n", snd_device);
        if (!v_running) {
            if (g_settings.connection == CB_RADIO_ADB && CheckAdbDevices(g_settings.port) != 0)
                return 1;
        }

        athread.rc = pthread_create(&athread.t, NULL, AudioThreadProc, NULL);
    }

    signal(SIGINT, sig_handler);
    signal(SIGHUP, sig_handler);

    if (g_settings.vertical_flip)
        decoder_vertical_flip();

    if (g_settings.horizontal_flip)
        decoder_horizontal_flip();

    if (!no_controls)
        wait_command();

    while (v_running || a_running)
        usleep(2000);

    dbgprint("joining\n");
    sig_handler(SIGHUP);
    if (athread.rc == 0) pthread_join(athread.t, NULL);
    if (vthread.rc == 0) pthread_join(vthread.t, NULL);
    if (dthread.rc == 0) pthread_join(dthread.t, NULL);

    decoder_fini();
    dbgprint("exit\n");
    return 0;
}
