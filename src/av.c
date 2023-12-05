/* DroidCam & DroidCamX (C) 2010-2021
 * https://github.com/dev47apps
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "common.h"
#include "settings.h"
#include "connection.h"
#include "decoder.h"
#include <stdint.h>

extern int a_active;
extern int v_active;
extern int a_running;
extern int v_running;
extern int thread_cmd;
extern struct settings g_settings;

const char *thread_cmd_val_str;

SOCKET GetConnection(void) {
    char *err;
    SOCKET socket = INVALID_SOCKET;

    if (g_settings.connection == CB_RADIO_IOS) {
        socket = CheckiOSDevices(g_settings.port);
        if (socket <= 0) socket = INVALID_SOCKET;
    } else {
        socket = Connect(g_settings.ip, g_settings.port, &err);
    }

    return socket;
}

// Battry Check thread
void *BatteryThreadProc(__attribute__((__unused__)) void *args) {
    SOCKET socket = INVALID_SOCKET;
    char buf[128] = {0};
    char battery_value[32] = {0};
    int i, j;

    dbgprint("Battery Thread Start\n");

    while (v_running || a_running) {
	if (v_active == 0 && a_active == 0) {
            usleep(50000);
            continue;
        }

        socket = GetConnection();
        if (socket == INVALID_SOCKET) {
            goto LOOP;
        }

        if (Send(BATTERY_REQ, CSTR_LEN(BATTERY_REQ), socket) <= 0) {
            errprint("error sending battery status request: (%d) '%s'\n",
                                        errno, strerror(errno));
            goto LOOP;
        }

        memset(buf, 0, sizeof(buf));
        if (RecvAll(buf, sizeof(buf), socket) <= 0) {
            goto LOOP;
        }

        for (i = 0; i < (sizeof(buf)-4); i++) {
            if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n') {
                i += 4;
                break;
            }
        }

        j = 0;
        while (i < sizeof(buf) && j < (sizeof(battery_value)-2) && buf[i] >= '0' && buf[i] <= '9')
            battery_value[j++] = buf[i++];

        if (j == 0)
            battery_value[j++] = '-';

        battery_value[j++] = '%';
        battery_value[j++] = 0;
        dbgprint("battery_value: %s\n", battery_value);
        UpdateBatteryLabel(battery_value);

    LOOP:
        disconnect(socket);
        for (j = 0; j < 30000 && (v_running || a_running); j++)
            usleep(1000);
    }

    dbgprint("Battery Thread End\n");
    return 0;
}

void *DecodeThreadProc(__attribute__((__unused__)) void *args) {
    dbgprint("Decode Thread Start\n");
    while (v_running != 0) {
        JPGFrame *f = pull_ready_jpg_frame();
        if (!f) {
            usleep(2000);
            continue;
        }
        process_frame(f);
        push_jpg_frame(f, true);
    }
    dbgprint("Decode Thread End\n");
    return 0;
}

void *VideoThreadProc(void *args) {
    char buf[32];
    SOCKET videoSocket = (SOCKET_PTR) args;
    int len;
    int keep_waiting = 0;
    dbgprint("Video Thread Started s=%d\n", videoSocket);

server_wait:
    if (videoSocket == INVALID_SOCKET) {
        videoSocket = accept_connection(g_settings.port);
        if (videoSocket == INVALID_SOCKET) { goto early_out; }
        keep_waiting = 1;
    }

    len = snprintf(buf, sizeof(buf), VIDEO_REQ, codec_names[g_settings.encoder],
                            decoder_get_video_width(), decoder_get_video_height());

    if (Send(buf, len, videoSocket) <= 0){
        errprint("send error (%d) '%s'\n", errno, strerror(errno));
        MSG_ERROR("Error sending request, DroidCam might be busy with another client.");
        goto early_out;
    }

    memset(buf, 0, sizeof(buf));
    if (RecvAll(buf, 9, videoSocket) <= 0) {
        errprint("recv error (%d) '%s'\n", errno, strerror(errno));
        MSG_ERROR("Connection reset!\nIs the app running?");
        goto early_out;
    }

    if (decoder_prepare_video(buf) == 0) {
        goto early_out;
    }

    v_active = 1;
    while (v_running != 0){
        if (thread_cmd != 0) {
            len = 0;
            if (thread_cmd == CB_CONTROL_WB) {
                len = snprintf(buf, sizeof(buf), OTHER_REQ_STR, thread_cmd, thread_cmd_val_str);
            }
            else {
                len = snprintf(buf, sizeof(buf), OTHER_REQ, thread_cmd);
            }
            if (len) {
                Send(buf, len, videoSocket);
            }
            thread_cmd = 0;
        }

        JPGFrame *f = pull_empty_jpg_frame();
        if (RecvAll(buf, 4, videoSocket) <= 0)
            break;

        f->length = le32toh(*(uint32_t*) buf);
        if (RecvAll((const char*)f->data, f->length, videoSocket) <= 0)
            break;

        push_jpg_frame(f, false);
    }

early_out:
    v_active = 0;
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

    // wait for video
    while (v_running) {
        usleep(200000);
        if (v_active) break;
        if (!a_running) return 0;
    }

    if (g_settings.connection == CB_RADIO_IOS)
        goto TCP_ONLY;
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
    socket = GetConnection();

    if (socket == INVALID_SOCKET) {
        errprint("Audio Connection failed\n");
        return 0;
    }

    if (Send(AUDIO_REQ, CSTR_LEN(AUDIO_REQ), socket) <= 0) {
        errprint("send error (audio) (%d) '%s'\n", errno, strerror(errno));
        MSG_ERROR("Error sending audio request");
        goto early_out;
    }

    memset(stream_buf, 0, 6);
    if (RecvAll(stream_buf, 6, socket) <= 0) {
        errprint("recv error (audio) (%d) '%s'\n", errno, strerror(errno));
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
    a_active = 1;
    while (a_running) {
        int len = (mode == UDP_STREAM)
            ? RecvNonBlockUDP(stream_buf, STREAM_BUF_SIZE, socket)
            : RecvNonBlock   (stream_buf, STREAM_BUF_SIZE, socket);
        if (len < 0) {
            errprint("recv error (audio) (%d) '%s'\n", errno, strerror(errno));
            goto early_out;
        }

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
    a_active = 0;
    if (mode == UDP_STREAM)
        SendUDPMessage(socket, STOP_REQ, CSTR_LEN(STOP_REQ), g_settings.ip, g_settings.port + 1);

    if (socket > 0)
        disconnect(socket);

    dbgprint("Audio Thread End\n");
    return 0;
}
