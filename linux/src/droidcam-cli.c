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
    char stream_buf[VIDEO_INBUF_SZ + 16]; // padded so libavcodec detects the end
    SOCKET videoSocket = INVALID_SOCKET;
    int keep_waiting = 0;

    if (g_ip != NULL) {
        videoSocket = connectDroidCam(g_ip, g_port);
        if (videoSocket == INVALID_SOCKET) {
            return;
        }
    }
    v_running  =1;
_wait:
    // We are the server
    if (videoSocket == INVALID_SOCKET) {
        videoSocket = accept_inet_connection(g_port);
        if (videoSocket == INVALID_SOCKET) { goto _out; }
        keep_waiting = 1;
    }
    {
        int L = sprintf(stream_buf, VIDEO_REQ, g_webcam_w, g_webcam_h);
        if ( SendRecv(1, stream_buf, L, videoSocket) <= 0 ){
            MSG_ERROR("Connection lost!");
            goto _out;
        }
        dbgprint("Sent request, ");
    }
    memset(stream_buf, 0, sizeof(stream_buf));
    if ( SendRecv(0, stream_buf, 5, videoSocket) <= 0 ){
        MSG_ERROR("Connection reset!\nDroidCam is probably busy with another client");
        goto _out;
    }

    if (decoder_prepare_video(stream_buf) == FALSE) { goto _out; }
    while (1){
        if (SendRecv(0, stream_buf, VIDEO_INBUF_SZ, videoSocket) == FALSE || DecodeVideo(stream_buf, VIDEO_INBUF_SZ) == FALSE) { break; }
    }

_out:
    dbgprint("disconnect\n");
    disconnect(videoSocket);
    decoder_cleanup();

    if (keep_waiting){
        videoSocket = INVALID_SOCKET;
        goto _wait;
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
