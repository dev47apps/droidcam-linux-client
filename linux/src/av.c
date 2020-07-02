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
    SOCKET videoSocket = (SOCKET_PTR) args;
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


void *AudioThreadProc(void *arg) {
    char     stream_buf[STREAM_BUF_SIZE];
    short    decode_buf[DECODE_BUF_SIZE]={0};
    int      decode_buf_used = 0;
    int      chunks_per_packet;
    int      bytes_per_packet;
    int      keepAliveCounter = 0;
    int      mode = 0;
    SOCKET   socket = 0;
    (void)   arg;

    struct snd_transfer_s transfer;
    snd_pcm_t *handle = decoder_prepare_audio();
    transfer.first = 1;
    if (!handle) {
        MSG_ERROR("Missing audio device");
        return 0;
    }

    if (strncmp(g_settings.ip, ADB_LOCALHOST_IP, CSTR_LEN(ADB_LOCALHOST_IP)) == 0)
        goto TCP_ONLY;

    // Try to stream via UDP first
    socket = CreateUdpSocket();
    if (socket <= 0) goto TCP_ONLY;

    for (int tries = 0; tries < 3; tries++) {
        dbgprint("Audio Thread UDP try #%d\n", tries);
        SendUDPMessage(socket, AUDIO_REQ, CSTR_LEN(AUDIO_REQ), g_settings.ip, g_settings.port + 1);
        for (int i = 0; i < 12; i++) {
            usleep(32000);
            int len = RecvNonBlockUDP(stream_buf, STREAM_BUF_SIZE, socket);
            if (len < 0) { goto TCP_ONLY; }
            if (len > 0) {
                bytes_per_packet = CHUNKS_PER_PACKET * DROIDCAM_SPX_CHUNK_BYTES_2;
                mode = UDP_STREAM;
                goto STREAM;
            }
        }
    }

TCP_ONLY:
    dbgprint("UDP didnt work, trying TCP\n");
    mode = TCP_STREAM;
    socket = Connect(g_settings.ip, g_settings.port);
    if (socket == INVALID_SOCKET) {
        errprint("Audio: Connect failed to %s:%d\n", g_settings.ip, g_settings.port);
        return 0;
    }

    if (SendRecv(1, AUDIO_REQ, CSTR_LEN(AUDIO_REQ), socket) <= 0) {
        MSG_ERROR("Error sending audio request");
        goto early_out;
    }

    memset(stream_buf, 0, 6);
    if (SendRecv(0, stream_buf, 6, socket) <= 0) {
        MSG_ERROR("Audio connection reset!");
        goto early_out;
    }

    if (stream_buf[0] != '-' || stream_buf[1] != '@'
        || stream_buf[2] != 'v'
        || stream_buf[3] != '0'
        || stream_buf[4] != '2'){
        MSG_ERROR("Invalid audio data stream!");
        goto early_out;
    }

    chunks_per_packet = stream_buf[5];
    if (CHUNKS_PER_PACKET != chunks_per_packet) {
        MSG_ERROR("Unsupported audio stream");
        goto early_out;
    }

    bytes_per_packet = CHUNKS_PER_PACKET * DROIDCAM_SPX_CHUNK_BYTES_2;

STREAM:
    while (a_running) {
        int len = (mode == UDP_STREAM)
            ? RecvNonBlockUDP(stream_buf, STREAM_BUF_SIZE, socket)
            : RecvNonBlock   (stream_buf, STREAM_BUF_SIZE, socket);
        if (len < 0) { goto early_out; }

        if (len > 0) {
            // dbgprint("recv %d frames\n", (len / DROIDCAM_SPX_CHUNK_BYTES_2));
            // if we get more than 1 frame, fast-fwd to latest one
            int idx = 0;
            if (len > bytes_per_packet) {
                // dbgprint("got excess data: %u bytes\n", len);
                idx = (len - bytes_per_packet);
                // not needed, but: len = bytes_per_packet;
            }
            decode_speex_frame(&stream_buf[idx], decode_buf, CHUNKS_PER_PACKET);
            // if (decode_buf_used) dbgprint("overwriting %d frames\n", decode_buf_used);
            decode_buf_used = CHUNKS_PER_PACKET * DROIDCAM_PCM_CHUNK_SAMPLES_2;
        }

        int err = snd_transfer_check(handle, &transfer);
        if (err < 0) {
            MSG_ERROR("Audio Error: snd_transfer_check failed");
            goto early_out;
        }
        if (err == 0) {
            usleep(1000);
            continue;
        }

        // dbgprint("can transfer %ld frames with offset=%ld\n", transfer.frames, transfer.offset);
        if (decode_buf_used == 0) {
            decoder_speex_plc(&transfer);
        } else {
            short *output_buffer = (short *)transfer.my_areas->addr;
            if ((int)transfer.frames >= decoder_get_audio_frame_size()) {
                transfer.frames = decoder_get_audio_frame_size();
            }
            memcpy(&output_buffer[transfer.offset], decode_buf, transfer.frames * sizeof(short));
            memmove(decode_buf, &decode_buf[transfer.frames], transfer.frames * sizeof(short));
            decode_buf_used -= transfer.frames;
            // dbgprint("copied %ld frames\n", transfer.frames);
        }

        err = snd_transfer_commit(handle, &transfer);
        if (err < 0) {
            MSG_ERROR("Audio Error: snd_transfer_commit failed");
            goto early_out;
        }

        if (mode == UDP_STREAM && ++keepAliveCounter > 1024) {
            keepAliveCounter = 0;
            dbgprint("audio keepalive\n");
            SendUDPMessage(socket, AUDIO_REQ, CSTR_LEN(AUDIO_REQ), g_settings.ip, g_settings.port + 1);
        }
        usleep(2000);
    }

early_out:
    if (mode == UDP_STREAM)
        SendUDPMessage(socket, STOP_REQ, CSTR_LEN(STOP_REQ), g_settings.ip, g_settings.port + 1);

    if (socket > 0)
        disconnect(socket);

    dbgprint("Audio Thread End\n");
    return 0;
}
