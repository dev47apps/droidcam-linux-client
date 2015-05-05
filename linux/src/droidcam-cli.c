#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/limits.h>

#include <errno.h>
#include <string.h>

#include "common.h"
#include "connection.h"
#include "decoder.h"

char *g_ip;
int g_port;
int g_webcam_w = 320;
int g_webcam_h = 240;
int v_running;

void ShowError(const char * title, const char * msg) {
    errprint("%s: %s\n", title, msg);
}

static void load_settings(void) {
    char buf[PATH_MAX];
    FILE * fp;

    snprintf(buf, sizeof(buf), "%s/.droidcam/settings", getenv("HOME"));
    fp = fopen(buf, "r");

    if (!fp){
        MSG_LASTERROR("settings error");
        return;
    }

    if(fgets(buf, sizeof(buf), fp)){
        sscanf(buf, "%d-%d", &g_webcam_w, &g_webcam_h);
        dbgprint("got webcam_w=%d, webcam_h=%d\n", g_webcam_w, g_webcam_h);
    }

    fclose(fp);
}

void stream_video(void) {
    char buf[32];
    int keep_waiting = 0;
    SOCKET videoSocket = INVALID_SOCKET;

    if (g_ip != NULL) {
        videoSocket = connect_droidcam(g_ip, g_port);
        if (videoSocket == INVALID_SOCKET) {
            return;
        }
    }
    v_running  =1;

server_wait:
    if (videoSocket == INVALID_SOCKET) {
        videoSocket = accept_connection(g_port);
        if (videoSocket == INVALID_SOCKET) { goto early_out; }
        keep_waiting = 1;
    }

    {
        int len = snprintf(buf, sizeof(buf), VIDEO_REQ, decoder_get_video_width(), decoder_get_video_height());
        if (SendRecv(1, buf, len, videoSocket) <= 0){
            MSG_ERROR("Error sending request, DroidCam might be busy with another client.");
            goto early_out;
        }
    }

    memset(buf, 0, sizeof(buf));
    if (SendRecv(0, buf, 5, videoSocket) <= 0 ){
        MSG_ERROR("Connection reset by app!\nDroidCam is probably busy with another client");
        goto early_out;
    }

    if (decoder_prepare_video(buf) == FALSE) {
        goto early_out;
    }

    while (1){
        int frameLen;
        struct jpg_frame_s *f = decoder_get_next_frame();
        if (SendRecv(0, buf, 4, videoSocket) == FALSE) break;
        make_int4(frameLen, buf[0], buf[1], buf[2], buf[3]);
        f->length = frameLen;
        char *p = (char*)f->data;
        while (frameLen > 4096) {
            if (SendRecv(0, p, 4096, videoSocket) == FALSE) goto early_out;
            frameLen -= 4096;
            p += 4096;
        }
        if (SendRecv(0, p, frameLen, videoSocket) == FALSE) break;
    }

early_out:
    dbgprint("disconnect\n");
    disconnect(videoSocket);
    decoder_cleanup();

    if (keep_waiting){
        videoSocket = INVALID_SOCKET;
        goto server_wait;
    }

    v_running = 0;
    connection_cleanup();
}

inline void usage(int argc, char *argv[]) {
    fprintf(stderr, "Usage: \n"
    " %s -l <port>\n"
    "   Listen on 'port' for connections\n"
    "\n"
    " %s <ip> <port>\n"
    "   Connect to 'ip' on 'port'\n"
    ,
    argv[0], argv[0]);
}


int main(int argc, char *argv[]) {
    if (argc == 3 && argv[1][0] == '-' && argv[1][1] == 'l') {
        g_ip = NULL;
        g_port = atoi(argv[2]);
    }
    else if (argc == 3) {
        g_ip = argv[1];
        g_port = atoi(argv[2]);
    }
    else {
        usage(argc, argv);
        return 1;
    }

    load_settings();
    if (!decoder_init(g_webcam_w, g_webcam_h)) {
        return 2;
    }
    stream_video();
    decoder_fini();
    return 0;
}
