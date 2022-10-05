/* DroidCam & DroidCamX (C) 2010-2021
 * https://github.com/dev47apps
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifdef HAVE_AV_CONFIG_H
#undef HAVE_AV_CONFIG_H
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <linux/limits.h>

#include "common.h"
#include "decoder.h"
#include "queue.h"

#include "turbojpeg.h"
#include "speex/speex.h"
#ifndef __cplusplus
#include "libswscale/swscale.h"
#else
extern "C"
{
#include "libswscale/swscale.h"
}
#endif

struct spx_decoder_s {
 snd_pcm_t *snd_handle;
 void *state;
 SpeexBits bits;
 int audioBoostPerc;
 int frame_size;
};

struct jpg_dec_ctx_s {
 int invert;
 int subsamp;
 int m_width, m_height; // stream WxH
 int d_width, d_height; // decoded WxH (can be inverted)
 int m_Yuv420Size, m_ySize, m_uvSize;
 int m_webcamYuvSize, m_webcam_ySize, m_webcam_uvSize;;
 size_t m_BufferLimit;

 BYTE *m_inBuf;         /* incoming stream */
 BYTE *m_decodeBuf;     /* decoded individual frames */
 BYTE *m_webcamBuf;     /* optional, scale incoming stream for the webcam */

 struct SwsContext *swc;
 tjhandle tj;
 tjhandle tjXform;
 tjtransform transform;

 BYTE*  tjDstSlice[4];
 BYTE* swcSrcSlice[4];
 BYTE* swcDstSlice[4];

 int  tjDstStride[4];
 int swcSrcStride[4];
 int swcDstStride[4];
};

#define JPG_BACKBUF_MAX 3
JPGFrame jpg_frames[JPG_BACKBUF_MAX];
queue decode_queue;
queue receive_queue;

struct jpg_dec_ctx_s  jpg_decoder;
struct spx_decoder_s  spx_decoder;

#define WEBCAM_Wf ((float)WEBCAM_W)
#define WEBCAM_Hf ((float)WEBCAM_H)
static unsigned int WEBCAM_W, WEBCAM_H;

static int droidcam_device_fd;
static snd_output_t *output = NULL;

static void decoder_share_frame();

#define FREE_OBJECT(obj, free_func) if(obj){dbgprint(" " #obj " %p\n", obj); free_func(obj); obj=NULL;}

int decoder_init(const char* v4l2_device, unsigned v4l2_width, unsigned v4l2_height) {
    WEBCAM_W = v4l2_width;
    WEBCAM_H = v4l2_height;

    if (v4l2_device) {
        set_v4l2_device(v4l2_device);
        droidcam_device_fd = open_v4l2_device();
    } else {
        droidcam_device_fd = find_v4l2_device("platform:v4l2loopback_dc", &WEBCAM_W, &WEBCAM_H);
        if (droidcam_device_fd < 0) {
            // check for generic v4l2loopback device
            droidcam_device_fd = find_v4l2_device("platform:v4l2loopback", &WEBCAM_W, &WEBCAM_H);
        }
    }

    if (droidcam_device_fd < 0) {
        MSG_ERROR("Droidcam/v4l2loopback device not found (/dev/video[0-9]).\n"
                "Did it install correctly?\n"
                "If you had a kernel update, you may need to re-install.");

        WEBCAM_W = 320;
        WEBCAM_H = 240;
        droidcam_device_fd = 0;
    }

    memset(&jpg_decoder, 0, sizeof(struct jpg_dec_ctx_s));
    jpg_decoder.tj = NULL;
    jpg_decoder.tjXform = NULL;
    jpg_decoder.invert = (WEBCAM_W < WEBCAM_H);
    jpg_decoder.transform.op = 0;
    jpg_decoder.transform.options = TJXOPT_COPYNONE | TJXOPT_TRIM;
    jpg_decoder.m_BufferLimit = 0;
    jpg_decoder.m_webcamYuvSize  = WEBCAM_W * WEBCAM_H * 3 / 2;
    jpg_decoder.m_webcam_ySize   = WEBCAM_W * WEBCAM_H;
    jpg_decoder.m_webcam_uvSize  = jpg_decoder.m_webcam_ySize / 4;

    if (snd_output_stdio_attach(&output, stdout, 0) < 0) {
        errprint("snd_output_stdio_attach failed\n");
    }

    dbgprint("init audio\n");
    memset(&spx_decoder, 0, sizeof(struct spx_decoder_s));
    spx_decoder.snd_handle = find_snd_device();
    if (!spx_decoder.snd_handle) {
        errprint("Audio loopback device not found.\n"
                "Is snd_aloop loaded?\n");
    }

    spx_decoder.audioBoostPerc = 100;
    speex_bits_init(&spx_decoder.bits);
    spx_decoder.state = speex_decoder_init(speex_lib_get_mode(SPEEX_MODEID_WB));
    speex_decoder_ctl(spx_decoder.state, SPEEX_GET_FRAME_SIZE, &spx_decoder.frame_size);
    dbgprint("spx_decoder.state=%p, frame_size=%d\n", spx_decoder.state, spx_decoder.frame_size);

    queue_init(&decode_queue);
    queue_init(&receive_queue);
    dbgprint("decoder_init done\n");
    return 1;
}

void decoder_fini() {
    if (droidcam_device_fd) close(droidcam_device_fd);
    droidcam_device_fd = 0;
    decoder_cleanup();

    queue_destroy(&decode_queue);
    queue_destroy(&receive_queue);
    FREE_OBJECT(spx_decoder.snd_handle, snd_pcm_close);
    dbgprint("spx_decoder.state=%p\n", spx_decoder.state);
    if (spx_decoder.state != NULL) {
        speex_bits_destroy(&spx_decoder.bits);
        speex_decoder_destroy(spx_decoder.state);
        spx_decoder.state = NULL;
    }
}

int decoder_prepare_video(char * header) {
    jpg_decoder.m_width = be16toh(*(uint16_t*) &header[0]);
    jpg_decoder.m_height = be16toh(*(uint16_t*) &header[2]);

    if (droidcam_device_fd <= 0) {
        MSG_ERROR("Missing video device");
        return 0;
    }

    if (jpg_decoder.m_width <= 0 || jpg_decoder.m_height <= 0) {
        MSG_ERROR("Invalid data stream!");
        return 0;
    }

    jpg_decoder.tj = tjInitDecompress();
    if (!jpg_decoder.tj) {
        MSG_ERROR("Error creating decoder!");
        return 0;
    }

    jpg_decoder.tjXform = tjInitTransform();
    if (!jpg_decoder.tjXform) {
        MSG_ERROR("Error creating transform!");
        return 0;
    }

    if (jpg_decoder.invert) {
        jpg_decoder.d_width = jpg_decoder.m_height;
        jpg_decoder.d_height = jpg_decoder.m_width;
        jpg_decoder.transform.op = TJXOP_ROT90;

    } else {
        jpg_decoder.d_width = jpg_decoder.m_width;
        jpg_decoder.d_height = jpg_decoder.m_height;
    }

    dbgprint("Stream W=%d H=%d\n", jpg_decoder.m_width, jpg_decoder.m_height);
    jpg_decoder.subsamp       = 0;
    jpg_decoder.m_ySize       = jpg_decoder.m_width * jpg_decoder.m_height;
    jpg_decoder.m_uvSize      = jpg_decoder.m_ySize / 4;
    jpg_decoder.m_Yuv420Size  = jpg_decoder.m_ySize * 3 / 2;
    jpg_decoder.m_inBuf       = (BYTE*)malloc((jpg_decoder.m_Yuv420Size * JPG_BACKBUF_MAX + 4096) * sizeof(BYTE));
    jpg_decoder.m_decodeBuf   = (BYTE*)malloc(jpg_decoder.m_Yuv420Size * sizeof(BYTE));

    if (jpg_decoder.m_webcamYuvSize != jpg_decoder.m_Yuv420Size) {
        jpg_decoder.m_webcamBuf = (BYTE*)malloc(jpg_decoder.m_webcamYuvSize * sizeof(BYTE));
        jpg_decoder.swc = sws_getCachedContext(NULL,
                jpg_decoder.d_width, jpg_decoder.d_height, AV_PIX_FMT_YUV420P, /* src */
                WEBCAM_W, WEBCAM_H , AV_PIX_FMT_YUV420P, /* dst */
                SWS_FAST_BILINEAR /* flags */, NULL, NULL, NULL);

        int srcLen = jpg_decoder.d_width;
        int dstLen = WEBCAM_W;

        jpg_decoder.swcSrcStride[0] = srcLen;
        jpg_decoder.swcSrcStride[1] = srcLen>>1;
        jpg_decoder.swcSrcStride[2] = srcLen>>1;
        jpg_decoder.swcSrcStride[3] = 0;

        jpg_decoder.swcSrcSlice[0] = &jpg_decoder.m_decodeBuf[0];
        jpg_decoder.swcSrcSlice[1] = jpg_decoder.swcSrcSlice[0] + jpg_decoder.m_ySize;
        jpg_decoder.swcSrcSlice[2] = jpg_decoder.swcSrcSlice[1] + jpg_decoder.m_uvSize;
        jpg_decoder.swcSrcSlice[3] = NULL;

        jpg_decoder.swcDstStride[0] = dstLen;
        jpg_decoder.swcDstStride[1] = dstLen>>1;
        jpg_decoder.swcDstStride[2] = dstLen>>1;
        jpg_decoder.swcDstStride[3] = 0;

        jpg_decoder.swcDstSlice[0] = &jpg_decoder.m_webcamBuf[0];
        jpg_decoder.swcDstSlice[1] = jpg_decoder.swcDstSlice[0] + jpg_decoder.m_webcam_ySize;
        jpg_decoder.swcDstSlice[2] = jpg_decoder.swcDstSlice[1] + jpg_decoder.m_webcam_uvSize;
        jpg_decoder.swcDstSlice[3] = NULL;
    }

    dbgprint("jpg: webcambuf: %p\n", jpg_decoder.m_webcamBuf);
    dbgprint("jpg: decodebuf: %p\n", jpg_decoder.m_decodeBuf);
    dbgprint("jpg: inbuf    : %p\n", jpg_decoder.m_inBuf);

    for (int i = 0; i < JPG_BACKBUF_MAX; i++) {
        jpg_frames[i].data = &jpg_decoder.m_inBuf[i*jpg_decoder.m_Yuv420Size];
        jpg_frames[i].length = 0;
        dbgprint("jpg: jpg_frames[%d]: %p\n", i, jpg_frames[i].data);
        queue_add(&receive_queue, &jpg_frames[i]);
    }

    int stride = jpg_decoder.d_width;
    jpg_decoder.tjDstStride[0] = stride;
    jpg_decoder.tjDstStride[1] = stride>>1;
    jpg_decoder.tjDstStride[2] = stride>>1;
    jpg_decoder.tjDstStride[3] = 0;

    jpg_decoder.tjDstSlice[0] = jpg_decoder.m_decodeBuf;
    jpg_decoder.tjDstSlice[1] = jpg_decoder.tjDstSlice[0] + jpg_decoder.m_ySize;
    jpg_decoder.tjDstSlice[2] = jpg_decoder.tjDstSlice[1] + jpg_decoder.m_uvSize;
    jpg_decoder.tjDstSlice[3] = NULL;

    return 1;
}

snd_pcm_t * decoder_prepare_audio(void) {
    speex_bits_reset(&spx_decoder.bits);
    dbgprint("audio boost %d%%\n", spx_decoder.audioBoostPerc);
    return spx_decoder.snd_handle;
}

void decoder_cleanup() {
    dbgprint("Cleanup\n");
    FREE_OBJECT(jpg_decoder.m_inBuf, free);
    FREE_OBJECT(jpg_decoder.m_decodeBuf, free);
    FREE_OBJECT(jpg_decoder.m_webcamBuf, free);
    FREE_OBJECT(jpg_decoder.swc, sws_freeContext);
    FREE_OBJECT(jpg_decoder.tjXform, tjDestroy);
    FREE_OBJECT(jpg_decoder.tj, tjDestroy);
    queue_clear(&receive_queue);
    queue_clear(&decode_queue);
}

void process_frame(JPGFrame *frame) {
    unsigned long len = (unsigned long)frame->length;
    BYTE *p = frame->data;

    if (jpg_decoder.subsamp == 0) {
        int width, height, subsamp, colorspace;
        if (tjDecompressHeader3(jpg_decoder.tj, p, len, &width, &height, &subsamp, &colorspace) < 0) {
            errprint("tjDecompressHeader3() failure: %d\n", tjGetErrorCode(jpg_decoder.tj));
            errprint("%s\n", tjGetErrorStr2(jpg_decoder.tj));
            return;
        }

        dbgprint("stream is %dx%d subsamp %d colorspace %d\n", width, height, subsamp, colorspace);
        if (subsamp != TJSAMP_420) {
            errprint("error: unexpected video image stream subsampling: %d\n", subsamp);
            return;
        }

        if (width != jpg_decoder.m_width || height != jpg_decoder.m_height) {
            errprint("error: unexpected video image dimentions: %dx%d vs expected %dx%x\n",
                width, height, jpg_decoder.m_width, jpg_decoder.m_height);
            return;
        }

        jpg_decoder.subsamp = subsamp;
    }

    if (jpg_decoder.transform.op) {
        if (tjTransform(jpg_decoder.tjXform, p, len, 1, &p, &len, &jpg_decoder.transform, 0)) {
            errprint("tjTransform failure: %s\n", tjGetErrorStr());
            return;
        }
    }

    if (tjDecompressToYUVPlanes(jpg_decoder.tj, p, len,
            jpg_decoder.tjDstSlice, jpg_decoder.d_width,
            jpg_decoder.tjDstStride, jpg_decoder.d_height,
            TJFLAG_FASTDCT | TJFLAG_FASTUPSAMPLE))
    {
        errprint("tjDecompressToYUV2 failure: %d\n", tjGetErrorCode(jpg_decoder.tj));
        return;
    }

    decoder_share_frame();
    return;
}

static void decoder_share_frame() {
    BYTE *p = jpg_decoder.m_decodeBuf;
    if (jpg_decoder.swc != NULL) {
        sws_scale(jpg_decoder.swc,
            (const uint8_t * const*) jpg_decoder.swcSrcSlice,
            jpg_decoder.swcSrcStride,
            0,
            jpg_decoder.d_height,
            jpg_decoder.swcDstSlice,
            jpg_decoder.swcDstStride);

        p = jpg_decoder.m_webcamBuf;
    }

    if (write(droidcam_device_fd, p, jpg_decoder.m_webcamYuvSize) < 0) {
        errprint("error: write() failed for video device\n");
    }
}


void decoder_show_test_image() {
    int i,j;
    int m_height = WEBCAM_H * 2;
    int m_width  = WEBCAM_W * 2;
    char header[8];

    header[0] = ( m_width >> 8  ) & 0xFF;
    header[1] = ( m_width >> 0  ) & 0xFF;
    header[2] = ( m_height >> 8 ) & 0xFF;
    header[3] = ( m_height >> 0 ) & 0xFF;
    decoder_prepare_video(header);

    // [ jpg ] -> [ yuv420 ] -> [ yuv420 scaled ] -> [ yuv420 webcam transformed ]

    // fill in "decoded" data
    BYTE *p = jpg_decoder.m_decodeBuf;
    memset(p, 128, jpg_decoder.m_Yuv420Size);
    for (j = 0; j < m_height; j++) {
        BYTE *line_end = p + m_width;
        for (i = 0; i < (m_width / 4); i++) {
            *p++ = 0;
        }
        for (i = 0; i < (m_width / 4); i++) {
            *p++ = 64;
        }
        for (i = 0; i < (m_width / 4); i++) {
            *p++ = 128;
        }
        for (i = 0; i < (m_width / 4); i++) {
            *p++ = rand()%250;
        }
        while (p < line_end) p++;
    }

    decoder_share_frame();
}

void push_jpg_frame(JPGFrame* frame, bool empty) {
    if (empty || receive_queue.size == 0 || decode_queue.size > jpg_decoder.m_BufferLimit)
        queue_add(&receive_queue, frame);
    else
        queue_add(&decode_queue, frame);
}

JPGFrame* pull_empty_jpg_frame(void) {
    return (JPGFrame*) queue_next(&receive_queue, 0);
}

JPGFrame* pull_ready_jpg_frame(void) {
    return (JPGFrame*) queue_next(&decode_queue, jpg_decoder.m_BufferLimit);
}

int decoder_get_video_width() {
    return WEBCAM_W;
}

int decoder_get_video_height() {
    return WEBCAM_H;
}

int decoder_horizontal_flip() {
    int h_flip  = (jpg_decoder.transform.op == TJXOP_HFLIP);
    int v_flip  = (jpg_decoder.transform.op == TJXOP_VFLIP);
    int hv_flip = (jpg_decoder.transform.op == TJXOP_ROT180);

    if (h_flip || hv_flip) {
        jpg_decoder.transform.op = hv_flip ? TJXOP_VFLIP : TJXOP_NONE;
    } else {
        jpg_decoder.transform.op = v_flip ? TJXOP_ROT180 : TJXOP_HFLIP;
    }

    return jpg_decoder.transform.op == TJXOP_HFLIP || jpg_decoder.transform.op == TJXOP_ROT180;
}

int decoder_vertical_flip() {
    int h_flip  = (jpg_decoder.transform.op == TJXOP_HFLIP);
    int v_flip  = (jpg_decoder.transform.op == TJXOP_VFLIP);
    int hv_flip = (jpg_decoder.transform.op == TJXOP_ROT180);

    if (v_flip || hv_flip) {
        jpg_decoder.transform.op = hv_flip ? TJXOP_HFLIP : TJXOP_NONE;
    } else {
        jpg_decoder.transform.op = h_flip ? TJXOP_ROT180 : TJXOP_VFLIP;
    }

    return jpg_decoder.transform.op == TJXOP_VFLIP || jpg_decoder.transform.op == TJXOP_ROT180;
}

int decoder_get_audio_frame_size(void) {
    return spx_decoder.frame_size; //20ms for wb speex
}

void decoder_speex_plc(struct snd_transfer_s* transfer) {
    short *output_buffer = (short *)transfer->my_areas->addr;
    if ((int)transfer->frames >= spx_decoder.frame_size){
        speex_decode_int(spx_decoder.state, NULL, &output_buffer[transfer->offset]);
        transfer->frames = spx_decoder.frame_size;
    } else {
        memset(&output_buffer[transfer->offset], 0, transfer->frames * sizeof(short));
    }
    // dbgprint("guessed %ld frames\n", transfer->frames);
}

int decode_speex_frame(char *stream_buf, short *decode_buf, int droidcam_spx_chunks) {
    int output_used = 0;
    for (int i = 0; i < droidcam_spx_chunks; i++) {
        speex_bits_read_from(&spx_decoder.bits, &stream_buf[i * DROIDCAM_SPX_CHUNK_BYTES_2], DROIDCAM_SPX_CHUNK_BYTES_2);
        while (output_used < DECODE_BUF_SIZE) {
            int ret = speex_decode_int(spx_decoder.state, &spx_decoder.bits, &decode_buf[output_used]);
            if (ret != 0) break;
            output_used += spx_decoder.frame_size;
        }
    }
    if (output_used > 0 && spx_decoder.audioBoostPerc != 100 && spx_decoder.audioBoostPerc >= 50 && spx_decoder.audioBoostPerc < 200) {
        for (int i = 0; i < output_used; i++) {
            decode_buf[i] += (decode_buf[i] * spx_decoder.audioBoostPerc / 100);
        }
    }
    // dbgprint("decoded %d frames\n", output_used);
    return output_used;
}
