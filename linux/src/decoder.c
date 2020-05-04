/* DroidCam & DroidCamX (C) 2010-
 * https://github.com/aramg
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Use at your own risk. See README file for more details.
 */

#ifdef HAVE_AV_CONFIG_H
#undef HAVE_AV_CONFIG_H
#endif

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/videodev2.h>
#include <linux/limits.h>

#include "turbojpeg.h"
#include "libswscale/swscale.h"
// #include "speex/speex.h"

#include "common.h"
#include "decoder.h"

struct spx_decoder_s {
 void *state;
#if 0
 SpeexBits bits;
#endif
 int audioBoostPerc;
 int frame_size;
};

struct jpg_dec_ctx_s {
 int subsamp;
 int m_width, m_height;
 int m_Yuv420Size, m_ySize, m_uvSize;
 int m_webcamYuvSize, m_webcam_ySize, m_webcam_uvSize;;
 int m_NextFrame, m_NextSlot, m_BufferLimit, m_BufferedFrames;

 BYTE *m_inBuf;         /* incoming stream */
 BYTE *m_decodeBuf;     /* decoded individual frames */
 BYTE *m_webcamBuf;     /* optional, scale incoming stream for the webcam */

 struct SwsContext *swc;
 tjhandle tj;
};

#define JPG_BACKBUF_MAX 10
struct jpg_frame_s    jpg_frames[JPG_BACKBUF_MAX];
struct jpg_dec_ctx_s  jpg_decoder;
struct spx_decoder_s  spx_decoder;

#define WEBCAM_Wf ((float)WEBCAM_W)
#define WEBCAM_Hf ((float)WEBCAM_H)
static int WEBCAM_W, WEBCAM_H;
static int droidcam_device_fd;

static void decoder_share_frame();

#define FREE_OBJECT(obj, free_func) if(obj){dbgprint(" " #obj " %p\n", obj); free_func(obj); obj=NULL;}

static int xioctl(int fd, int request, void *arg){
    int r;
    do r = ioctl (fd, request, arg);
    while (-1 == r && EINTR == errno);
    return r;
}

static int find_droidcam_v4l(){
    int crt_video_dev = 0;
    char device[16];
    struct stat st;
    struct v4l2_capability v4l2cap;

    for(crt_video_dev = 0; crt_video_dev < 99; crt_video_dev++) {
        droidcam_device_fd = -1;
        snprintf(device, sizeof(device), "/dev/video%d", crt_video_dev);
        if(-1 == stat(device, &st)){
            continue;
        }

        if(!S_ISCHR(st.st_mode)){
            continue;
        }

        droidcam_device_fd = open(device, O_RDWR | O_NONBLOCK, 0);

        if(-1 == droidcam_device_fd) {
            printf("Error opening '%s': %d '%s'\n", device, errno, strerror(errno));
            continue;
        }
        if(-1 == xioctl(droidcam_device_fd, VIDIOC_QUERYCAP, &v4l2cap)) {
            close(droidcam_device_fd);
            continue;
        }
        printf("Device: %s\n", v4l2cap.card);
        if(0 == strncmp((const char*) v4l2cap.card, "Droidcam", 8)) {
            printf("Found driver: %s (fd:%d)\n", device, droidcam_device_fd);
            return 1;
        }
        close(droidcam_device_fd); // not DroidCam .. keep going
        continue;
    }
    MSG_ERROR("Device not found (/dev/video[0-9]).\n"
            "Did it install correctly?\n"
            "If you had a kernel update, you may need to re-install.");
    return 0;
}

static void query_droidcam_v4l(void) {
    struct v4l2_format vid_format = {0};
    vid_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vid_format.fmt.pix.width = 0;
    vid_format.fmt.pix.height = 0;
    if (xioctl(droidcam_device_fd, VIDIOC_G_FMT, &vid_format) < 0) {
        fprintf(stderr, "Fatal: Unable to query droidcam video device. errno=%d\n", errno);
        return;
    }

    dbgprint("  vid_format->type                =%d\n", vid_format.type );
    dbgprint("  vid_format->fmt.pix.width       =%d\n", vid_format.fmt.pix.width );
    dbgprint("  vid_format->fmt.pix.height      =%d\n", vid_format.fmt.pix.height );
    dbgprint("  vid_format->fmt.pix.pixelformat =%d\n", vid_format.fmt.pix.pixelformat);
    dbgprint("  vid_format->fmt.pix.sizeimage   =%d\n", vid_format.fmt.pix.sizeimage );
    dbgprint("  vid_format->fmt.pix.field       =%d\n", vid_format.fmt.pix.field );
    dbgprint("  vid_format->fmt.pix.bytesperline=%d\n", vid_format.fmt.pix.bytesperline );
    dbgprint("  vid_format->fmt.pix.colorspace  =%d\n", vid_format.fmt.pix.colorspace );
    if (vid_format.fmt.pix.pixelformat != V4L2_PIX_FMT_YUV420) {
        fprintf(stderr, "Fatal: droidcam video device reported pixel format %d, expected %d\n",
            vid_format.fmt.pix.pixelformat, V4L2_PIX_FMT_YUV420);
        return;
    }
    if (vid_format.fmt.pix.width <= 0 ||  vid_format.fmt.pix.height <= 0) {
        fprintf(stderr, "Fatal: droidcam video device reported invalid resolution: %dx%d\n",
            vid_format.fmt.pix.width, vid_format.fmt.pix.height);
        return;
    }

    WEBCAM_W = vid_format.fmt.pix.width;
    WEBCAM_H = vid_format.fmt.pix.height;
}

void decoder_set_video_delay(unsigned v) {
    if (v > JPG_BACKBUF_MAX) v = JPG_BACKBUF_MAX;
    else if (v < 1) v = 1;
    jpg_decoder.m_BufferLimit = v;
    dbgprint("buffer %d frames\n", jpg_decoder.m_BufferLimit);
}

int decoder_init(void) {
    WEBCAM_W = 0;
    WEBCAM_H = 0;

    if (!find_droidcam_v4l())
        return 0;
    query_droidcam_v4l();
    dbgprint("WEBCAM_W=%d, WEBCAM_H=%d\n", WEBCAM_W, WEBCAM_H);
    if (WEBCAM_W < 2 || WEBCAM_H < 2 || WEBCAM_W > 9999 || WEBCAM_H > 9999){
        MSG_ERROR("Unable to query droidcam device for parameters");
        return 0;
    }

    memset(&jpg_decoder, 0, sizeof(struct jpg_dec_ctx_s));
    jpg_decoder.tj = NULL;
    jpg_decoder.m_webcamYuvSize  = WEBCAM_W * WEBCAM_H * 3 / 2;
    jpg_decoder.m_webcam_ySize   = WEBCAM_W * WEBCAM_H;
    jpg_decoder.m_webcam_uvSize  = jpg_decoder.m_webcam_ySize / 4;
    decoder_set_video_delay(0);

#if 0
    speex_bits_init(&spx_decoder.bits);
    spx_decoder.state = speex_decoder_init(speex_lib_get_mode(SPEEX_MODEID_WB));
    speex_decoder_ctl(spx_decoder.state, SPEEX_GET_FRAME_SIZE, &spx_decoder.frame_size);
    dbgprint("spx_decoder.state=%p\n", spx_decoder.state);
#endif

    return 1;
}

void decoder_fini() {
    if (droidcam_device_fd) close(droidcam_device_fd);
    decoder_cleanup();

    dbgprint("spx_decoder.state=%p\n", spx_decoder.state);
    if (spx_decoder.state != NULL) {
#if 0
        speex_bits_destroy(&spx_decoder.bits);
        speex_decoder_destroy(spx_decoder.state);
#endif
        spx_decoder.state = NULL;
    }
}

int decoder_prepare_video(char * header) {
    int i;
    make_int(jpg_decoder.m_width,  header[0], header[1]);
    make_int(jpg_decoder.m_height, header[2], header[3]);

    if (jpg_decoder.m_width <= 0 || jpg_decoder.m_height <= 0) {
        MSG_ERROR("Invalid data stream!");
        return FALSE;
    }

    jpg_decoder.tj = tjInitDecompress();
    if (!jpg_decoder.tj) {
        MSG_ERROR("Error creating decoder!");
        return FALSE;
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
                jpg_decoder.m_width, jpg_decoder.m_height, AV_PIX_FMT_YUV420P, /* src */
                WEBCAM_W, WEBCAM_H , AV_PIX_FMT_YUV420P, /* dst */
                SWS_FAST_BILINEAR /* flags */, NULL, NULL, NULL);
    }

    dbgprint("jpg: webcambuf: %p\n", jpg_decoder.m_webcamBuf);
    dbgprint("jpg: decodebuf: %p\n", jpg_decoder.m_decodeBuf);
    dbgprint("jpg: inbuf    : %p\n", jpg_decoder.m_inBuf);

    for (i = 0; i < JPG_BACKBUF_MAX; i++) {
        jpg_frames[i].data = &jpg_decoder.m_inBuf[i*jpg_decoder.m_Yuv420Size];
        jpg_frames[i].length = 0;
        dbgprint("jpg: jpg_frames[%d]: %p\n", i, jpg_frames[i].data);
    }

    jpg_decoder.m_BufferedFrames  = jpg_decoder.m_NextFrame = jpg_decoder.m_NextSlot = 0;
    return TRUE;
}

void decoder_cleanup() {
    dbgprint("Cleanup\n");
    FREE_OBJECT(jpg_decoder.m_inBuf, free);
    FREE_OBJECT(jpg_decoder.m_decodeBuf, free);
    FREE_OBJECT(jpg_decoder.m_webcamBuf, free);
    FREE_OBJECT(jpg_decoder.swc, sws_freeContext);
    FREE_OBJECT(jpg_decoder.tj, tjDestroy);
}

static void decode_next_frame() {
    BYTE *p = jpg_frames[jpg_decoder.m_NextFrame].data;
    unsigned long len = (unsigned long)jpg_frames[jpg_decoder.m_NextFrame].length;

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

    BYTE* dstSlice[4];
    int dstStride[4] = {
        jpg_decoder.m_width,
        jpg_decoder.m_width>>1,
        jpg_decoder.m_width>>1,
    0};

    dstSlice[0] = jpg_decoder.m_decodeBuf;
    dstSlice[1] = dstSlice[0] + jpg_decoder.m_ySize;
    dstSlice[2] = dstSlice[1] + jpg_decoder.m_uvSize;
    dstSlice[3] = NULL;

    if (tjDecompressToYUVPlanes(jpg_decoder.tj, p, len, dstSlice, jpg_decoder.m_width, dstStride, jpg_decoder.m_height, TJFLAG_FASTDCT | TJFLAG_FASTUPSAMPLE)) {
        errprint("tjDecompressToYUV2 failure: %d\n", tjGetErrorCode(jpg_decoder.tj));
        return;
    }

    decoder_share_frame();
    return;
}

static void decoder_share_frame() {
    BYTE *p = jpg_decoder.m_decodeBuf;
    if (jpg_decoder.swc != NULL) {
        uint8_t* srcSlice[4];
        uint8_t* dstSlice[4];

        int srcStride[4] = {
            jpg_decoder.m_width,
            jpg_decoder.m_width>>1,
            jpg_decoder.m_width>>1,
        0};
        int dstStride[4] = {
            WEBCAM_W,
            WEBCAM_W>>1,
            WEBCAM_W>>1,
        0};

        srcSlice[0] = &jpg_decoder.m_decodeBuf[0];
        srcSlice[1] = srcSlice[0] + jpg_decoder.m_ySize;
        srcSlice[2] = srcSlice[1] + jpg_decoder.m_uvSize;
        srcSlice[3] = NULL;
        dstSlice[0] = &jpg_decoder.m_webcamBuf[0];
        dstSlice[1] = dstSlice[0] + jpg_decoder.m_webcam_ySize;
        dstSlice[2] = dstSlice[1] + jpg_decoder.m_webcam_uvSize;
        dstSlice[3] = NULL;

        sws_scale(jpg_decoder.swc, (const uint8_t * const*)srcSlice, srcStride, 0, jpg_decoder.m_height, dstSlice, dstStride);
        p = jpg_decoder.m_webcamBuf;
    }

    write(droidcam_device_fd, p, jpg_decoder.m_webcamYuvSize);
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

struct jpg_frame_s* decoder_get_next_frame() {
    while (jpg_decoder.m_BufferedFrames > jpg_decoder.m_BufferLimit) {
        jpg_decoder.m_BufferedFrames--;
        jpg_decoder.m_NextFrame = (jpg_decoder.m_NextFrame < (JPG_BACKBUF_MAX-1)) ? (jpg_decoder.m_NextFrame + 1) : 0;
    }
    if (jpg_decoder.m_BufferedFrames == jpg_decoder.m_BufferLimit) {
        // dbgprint("decoding #%2d (have buffered: %d)\n", jpg_decoder.m_NextFrame, jpg_decoder.m_BufferedFrames);
        decode_next_frame();
        jpg_decoder.m_BufferedFrames--;
        jpg_decoder.m_NextFrame = (jpg_decoder.m_NextFrame < (JPG_BACKBUF_MAX-1)) ? (jpg_decoder.m_NextFrame + 1) : 0;
    }

    // a call to this function assumes we are about to get a full frame (or exit on failure).
    // so increment the # of buffered frames. do this after the while() loop above to
    // take care of the initial case:
    jpg_decoder.m_BufferedFrames ++;

    int nextSlotSaved = jpg_decoder.m_NextSlot;
    jpg_decoder.m_NextSlot = (jpg_decoder.m_NextSlot < (JPG_BACKBUF_MAX-1)) ? (jpg_decoder.m_NextSlot + 1) : 0;
    // dbgprint("next image going to #%2d (have buffered: %d)\n", nextSlotSaved, (jpg_decoder.m_BufferedFrames-1));
    return &jpg_frames[nextSlotSaved];
}

int decoder_get_video_width() {
    return WEBCAM_W;
}

int decoder_get_video_height(){
    return WEBCAM_H;
}

int decoder_get_audio_frame_size(void) {
    return spx_decoder.frame_size; //20ms for wb speex
}
