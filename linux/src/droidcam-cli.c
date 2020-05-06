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
#include "connection.h"
#include "decoder.h"

char *g_ip;
int g_port;
int v_running = 1;
int a_running = 0;

void sig_handler(int sig) {
    a_running = 0;
    v_running = 0;
    return;
}

void ShowError(const char * title, const char * msg) {
    errprint("%s: %s\n", title, msg);
}

void *stream_video(void *unused) {
    char buf[32];
    int keep_waiting = 0;
    SOCKET videoSocket = INVALID_SOCKET;

    if (g_ip != NULL) {
        videoSocket = connect_droidcam(g_ip, g_port);
        if (videoSocket == INVALID_SOCKET) {
            errprint("Video: Connect failed to %s:%d\n", g_ip, g_port);
            return 0;
        }
    }

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
    if (SendRecv(0, buf, 9, videoSocket) <= 0 ){
        MSG_ERROR("Connection reset by app!\nDroidCam is probably busy with another client");
        goto early_out;
    }

    if (decoder_prepare_video(buf) == FALSE) {
        goto early_out;
    }

    while (v_running) {
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

    sig_handler(SIGHUP); // video stops audio too
    connection_cleanup();
    dbgprint("stream_video end\n");
    return 0;
}

void *stream_audio(void *unused) {
    char     stream_buf[STREAM_BUF_SIZE];
    short    decode_buf[DECODE_BUF_SIZE]={0};
    int      decode_buf_used = 0;
    int      chunks_per_packet;
    int      bytes_per_packet;
    int      keepAliveCounter = 0;
    int      mode = 0;
    SOCKET   socket = 0;

    struct snd_transfer_s transfer;
    snd_pcm_t *handle;

    if (!g_ip) {
        errprint("Audio: Missing droidcam ip\n");
        return 0;
    }

    transfer.first = 1;
    handle = decoder_prepare_audio();
    if (!handle) {
        MSG_ERROR("Audio Error: fopen failed");
        return 0;
    }

    if (strncmp(g_ip, ADB_LOCALHOST_IP, CSTR_LEN(ADB_LOCALHOST_IP)) == 0)
        goto TCP_ONLY;

    // Try to stream via UDP first
    socket = CreateUdpSocket();
    if (socket <= 0) goto TCP_ONLY;

    for (int tries = 0; tries < 3; tries++) {
        dbgprint("Audio Thread UDP try #%d\n", tries);
        SendUDPMessage(socket, AUDIO_REQ, CSTR_LEN(AUDIO_REQ), g_ip, g_port + 1);
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

    socket = connect_droidcam(g_ip, g_port);
    if (socket == INVALID_SOCKET) {
        errprint("Audio: Connect failed to %s:%d\n", g_ip, g_port);
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
    mode = TCP_STREAM;

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
            short *output_buffer = transfer.my_areas->addr;
            if (transfer.frames >= decoder_get_audio_frame_size()) {
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
            SendUDPMessage(socket, AUDIO_REQ, CSTR_LEN(AUDIO_REQ), g_ip, g_port + 1);
        }
        usleep(2000);
    }

early_out:
    disconnect(socket);
    dbgprint("stream_audio end\n");
    return 0;
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

    if (argc == 3 && argv[1][0] == '-' && argv[1][1] == 'l') {
        g_ip = NULL;
        g_port = atoi(argv[2]);
    }
    else if (argc >= 3) {
        g_ip = argv[1];
        g_port = atoi(argv[2]);
        if (argc == 4 && argv[3][0] == '-' && argv[3][1] == 'a')
            a_running = 1;
    }
    else {
        usage(argc, argv);
        return 1;
    }

    if (!decoder_init()) {
        return 2;
    }

    if (v_running) {
        if (0 != pthread_create(&vthread, NULL, stream_video, NULL))
            errprint("Error creating video thread\n");
    }

    if (a_running) {
        if (0 != pthread_create(&athread, NULL, stream_audio, NULL))
            errprint("Error creating audio thread\n");
    }

    signal(SIGINT, sig_handler);
    signal(SIGHUP, sig_handler);
    while (a_running || v_running)
        usleep(1000);

    pthread_join(athread, NULL);
    pthread_join(vthread, NULL);
    decoder_fini();
    return 0;
}
