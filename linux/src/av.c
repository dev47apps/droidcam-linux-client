#include "common.h"
#include "settings.h"
#include "connection.h"
#include "decoder.h"

extern int a_running;
extern int v_running;
extern int thread_cmd;
extern struct settings g_settings;

void * VideoThreadProc(void * args) {
    char buf[32];
    SOCKET videoSocket = (SOCKET) args;
    int keep_waiting = 0;
    dbgprint("Video Thread Started s=%d\n", videoSocket);
    v_running = 1;

server_wait:
    if (videoSocket == INVALID_SOCKET) {
        videoSocket = accept_connection(g_settings.port);
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
    if (SendRecv(0, buf, 9, videoSocket) <= 0) {
        MSG_ERROR("Connection reset!\nIs the app running?");
        goto early_out;
    }

    if (decoder_prepare_video(buf) == FALSE) {
        goto early_out;
    }

    while (v_running != 0){
        if (thread_cmd != 0) {
            int len = sprintf(buf, OTHER_REQ, thread_cmd);
            SendRecv(1, buf, len, videoSocket);
            thread_cmd = 0;
        }

        int frameLen;
        struct jpg_frame_s *f = decoder_get_next_frame();
        if (SendRecv(0, buf, 4, videoSocket) == FALSE) break;
        make_int4(frameLen, buf[0], buf[1], buf[2], buf[3]);
        f->length = frameLen;
        if (SendRecv(0, (char*)f->data, frameLen, videoSocket) == FALSE)
            break;

    }

early_out:
    dbgprint("disconnect\n");
    disconnect(videoSocket);
    decoder_cleanup();

    if (v_running && keep_waiting){
        videoSocket = INVALID_SOCKET;
        goto server_wait;
    }

    connection_cleanup();
    dbgprint("Video Thread End\n");
    return 0;
}
