/* DroidCam & DroidCamX (C) 2010-
 * https://github.com/aramg
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef __DECODR_H__
#define __DECODR_H__
#include <alsa/asoundlib.h>

typedef unsigned char BYTE;

struct jpg_frame_s {
 BYTE *data;
 unsigned length;
};

struct snd_transfer_s {
    int first;
    snd_pcm_uframes_t offset;
    snd_pcm_uframes_t frames;
    const snd_pcm_channel_area_t *my_areas;
};

int  decoder_init();
void decoder_fini();

snd_pcm_t * decoder_prepare_audio(void);
int decoder_get_audio_frame_size(void);
void decoder_speex_plc(struct snd_transfer_s* transfer);
int decode_speex_frame(char *stream_buf, short *decode_buf, int droidcam_spx_chunks);
int  decoder_prepare_video(char * header);
void decoder_cleanup();

struct jpg_frame_s* decoder_get_next_frame();
void decoder_set_video_delay(unsigned v);
int decoder_get_video_width();
int decoder_get_video_height();
void decoder_show_test_image();

/* 20ms 16hkz 16 bit */
#define DROIDCAM_CHUNK_MS_2           20
#define DROIDCAM_SPX_CHUNK_BYTES_2    70
#define DROIDCAM_PCM_CHUNK_BYTES_2    640
#define DROIDCAM_PCM_CHUNK_SAMPLES_2  320
#define DROIDCAM_SPEEX_BACKBUF_MAX_COUNT 2

#define STREAM_BUF_SIZE (DROIDCAM_SPX_CHUNK_BYTES_2*6)
#define DECODE_BUF_SIZE (DROIDCAM_PCM_CHUNK_SAMPLES_2*6)
#define CHUNKS_PER_PACKET 2
#define UDP_STREAM 2
#define TCP_STREAM 1

#define VIDEO_FMT_DROIDCAM 3
#define VIDEO_FMT_DROIDCAMX 18

int find_droidcam_v4l();
void query_droidcam_v4l(int droidcam_device_fd, int *WEBCAM_W, int *WEBCAM_H);

snd_pcm_t *find_snd_device(void);
int snd_transfer_check(snd_pcm_t *handle, struct snd_transfer_s *transfer);
int snd_transfer_commit(snd_pcm_t *handle, struct snd_transfer_s *transfer);

#endif
