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
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/videodev2.h>
#include <linux/limits.h>

#include "jpeglib.h"
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
 struct jpeg_decompress_struct dinfo;
 struct jpeg_error_mgr jerr;
 int init;
 int subsamp;
 int m_width, m_height;
 int m_Yuv420Size, m_ySize, m_uvSize;
 int m_webcamYuvSize, m_webcam_ySize, m_webcam_uvSize;;
 int m_NextFrame, m_NextSlot, m_BufferLimit, m_BufferedFrames;

 BYTE *m_inBuf;         /* incoming stream */
 BYTE *m_decodeBuf;     /* decoded individual frames */
 BYTE *m_webcamBuf;     /* optional, scale incoming stream for the webcam */
 BYTE *scratchBuf;

 // xxx: better way to do all the scaling/rotation/etc?
 struct SwsContext *swc;
 float scale_matrix[9];
 float angle_matrix[9];

 /* these should be alloced for each frame but sunce the stream
  * from the app will be consistent, we'll optimize by only allocing
  * once */
 int cw[MAX_COMPONENTS], ch[MAX_COMPONENTS], iw[MAX_COMPONENTS], th[MAX_COMPONENTS];
 JSAMPROW *outbuf[MAX_COMPONENTS];

 int transform;
};

#define JPG_BACKBUF_MAX 10
struct jpg_frame_s    jpg_frames[JPG_BACKBUF_MAX];
struct jpg_dec_ctx_s  jpg_decoder;
struct spx_decoder_s  spx_decoder;

#define WEBCAM_Wf ((float)WEBCAM_W)
#define WEBCAM_Hf ((float)WEBCAM_H)
static int WEBCAM_W, WEBCAM_H;
static int droidcam_device_fd;

#undef MAX_COMPONENTS
#define MAX_COMPONENTS  4
#define TJ_NUMSAMP 5
#define NUMSUBOPT TJ_NUMSAMP

/**
 * MCU block width (in pixels) for a given level of chrominance subsampling.
 * MCU block sizes:
 * - 8x8 for no subsampling or grayscale
 * - 16x8 for 4:2:2
 * - 8x16 for 4:4:0
 * - 16x16 for 4:2:0
 */
static const int tjMCUWidth[TJ_NUMSAMP]  = {8, 16, 16, 8, 8};

/**
 * MCU block height (in pixels) for a given level of chrominance subsampling.
 * MCU block sizes:
 * - 8x8 for no subsampling or grayscale
 * - 16x8 for 4:2:2
 * - 8x16 for 4:4:0
 * - 16x16 for 4:2:0
 */
static const int tjMCUHeight[TJ_NUMSAMP] = {8, 8, 16, 8, 16};

static const int pixelsize[TJ_NUMSAMP]={3, 3, 3, 1, 3};

#define PAD(v, p) ((v+(p)-1)&(~((p)-1)))

enum TJSAMP {
  TJSAMP_444=0,
  TJSAMP_422,
  TJSAMP_420,
  TJSAMP_GRAY,
  TJSAMP_440,
  TJSAMP_UNK,
  TJSAMP_NIL
};


static int fatal_error = 0;

void jpeg_mem_dest_tj(j_compress_ptr, unsigned char **, unsigned long *, boolean);
void jpeg_mem_src_tj(j_decompress_ptr, unsigned char *, unsigned long);

static void decoder_share_frame();
static void decoder_set_stransform(int value);

void joutput_message(j_common_ptr cinfo) {
    char buffer[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message) (cinfo, buffer);
    dbgprint("JERR: %s", buffer);
}

void jerror_exit(j_common_ptr cinfo) {
    dbgprint("jerror_exit(), fatal error");
    fatal_error = 1;
    (*cinfo->err->output_message) (cinfo);
}

static inline void fill_matrix(float sx, float sy, float angle, float scale, float *matrix) {
    matrix[0] = scale * cos(angle);
    matrix[1] = -sin(angle);
    matrix[2] = sx;
    matrix[3] = -matrix[1];
    matrix[4] = matrix[0];
    matrix[5] = sy;
    matrix[6] = 0;
    matrix[7] = 0;
    matrix[8] = 1;
}

#define FREE_OBJECT(obj, free_func) if(obj){dbgprint(" " #obj " %p\n", obj); free_func(obj); obj=NULL;}

static int xioctl(int fd, int request, void *arg){
    int r;
    do r = ioctl (fd, request, arg);
    while (-1 == r && EINTR == errno);
    return r;
}

static inline int clip(int v){ return ((v < 0) ? 0 : ((v >= 256) ? 255 : v)); }

static int find_droidcam_v4l(){
    int crt_video_dev = 0;
    char device[12];
    struct stat st;
    struct v4l2_capability v4l2cap;

    for(crt_video_dev = 0; crt_video_dev < 99; crt_video_dev++) {
        droidcam_device_fd = -1;
        sprintf(device, "/dev/video%d", crt_video_dev);
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
    MSG_ERROR("Device not found (/dev/video[0-9]).\nDid you install it?\n");
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

    fatal_error = 0;
    memset(&jpg_decoder, 0, sizeof(struct jpg_dec_ctx_s));
    jpg_decoder.dinfo.err = jpeg_std_error(&jpg_decoder.jerr);
    jpg_decoder.jerr.output_message = joutput_message;
    jpg_decoder.jerr.error_exit = jerror_exit;
    jpeg_create_decompress(&jpg_decoder.dinfo);
    if (fatal_error) return 0;
    jpg_decoder.init = 1;
    jpg_decoder.subsamp = TJSAMP_NIL;
    jpg_decoder.m_webcamYuvSize  = WEBCAM_W * WEBCAM_H * 3 / 2;
    jpg_decoder.m_webcam_ySize   = WEBCAM_W * WEBCAM_H;
    jpg_decoder.m_webcam_uvSize  = jpg_decoder.m_webcam_ySize / 4;
    jpg_decoder.transform = 0;
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
    dbgprint("spx_decoder.state=%p\n", spx_decoder.state);
    if (spx_decoder.state != NULL) {
#if 0
        speex_bits_destroy(&spx_decoder.bits);
        speex_decoder_destroy(spx_decoder.state);
#endif
        spx_decoder.state = NULL;
    }
    fatal_error = 0;
    if (jpg_decoder.init != 0) {
        jpeg_destroy_decompress(&jpg_decoder.dinfo);
        jpg_decoder.init = 0;
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

    dbgprint("Stream W=%d H=%d\n", jpg_decoder.m_width, jpg_decoder.m_height);

    jpg_decoder.m_ySize       = jpg_decoder.m_width * jpg_decoder.m_height;
    jpg_decoder.m_uvSize      = jpg_decoder.m_ySize / 4;
    jpg_decoder.m_Yuv420Size  = jpg_decoder.m_ySize * 3 / 2;
    jpg_decoder.m_inBuf       = (BYTE*)malloc((jpg_decoder.m_Yuv420Size * JPG_BACKBUF_MAX + 4096) * sizeof(BYTE));
    jpg_decoder.m_decodeBuf   = (BYTE*)malloc(jpg_decoder.m_Yuv420Size * sizeof(BYTE));
    jpg_decoder.scratchBuf    = (BYTE*)malloc(jpg_decoder.m_webcam_ySize * 2 * sizeof(BYTE));

    if (jpg_decoder.m_webcamYuvSize != jpg_decoder.m_Yuv420Size) {
        jpg_decoder.m_webcamBuf = (BYTE*)malloc(jpg_decoder.m_webcamYuvSize * sizeof(BYTE));
        jpg_decoder.swc = sws_getCachedContext(NULL,
                jpg_decoder.m_width, jpg_decoder.m_height, PIX_FMT_YUV420P, /* src */
                WEBCAM_W, WEBCAM_H , PIX_FMT_YUV420P, /* dst */
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
    decoder_set_stransform(jpg_decoder.transform);

    for(i=0; i<MAX_COMPONENTS; i++){
        jpg_decoder.outbuf[i]=NULL;
    }

    return TRUE;
}

void decoder_cleanup() {
    int i;
    dbgprint("Cleanup\n");
    for(i=0; i<MAX_COMPONENTS; i++){
        FREE_OBJECT(jpg_decoder.outbuf[i], free);
    }

    FREE_OBJECT(jpg_decoder.m_inBuf, free);
    FREE_OBJECT(jpg_decoder.m_decodeBuf, free);
    FREE_OBJECT(jpg_decoder.m_webcamBuf, free);
    FREE_OBJECT(jpg_decoder.scratchBuf, free);
    FREE_OBJECT(jpg_decoder.swc, sws_freeContext);
}

static void decode_next_frame() {
    struct jpeg_decompress_struct *dinfo = &jpg_decoder.dinfo;
    BYTE *p = jpg_frames[jpg_decoder.m_NextFrame].data;
    unsigned long len = (unsigned long)jpg_frames[jpg_decoder.m_NextFrame].length;

    int i,k, row, usetmpbuf=0;
    JSAMPLE *ptr=jpg_decoder.m_decodeBuf;

    jpeg_mem_src_tj(dinfo, p, len);
    jpeg_read_header(dinfo, TRUE);
    if (fatal_error) return;
    dinfo->raw_data_out=TRUE;
    dinfo->do_fancy_upsampling=FALSE;
    dinfo->dct_method=JDCT_FASTEST;
    dinfo->out_color_space=JCS_YCbCr;

    if (jpg_decoder.subsamp == TJSAMP_NIL) {
        int retval=TJSAMP_NIL;
        for(i=0; i<NUMSUBOPT; i++) {
            if(dinfo->num_components==pixelsize[i]){
                if(dinfo->comp_info[0].h_samp_factor==tjMCUWidth[i]/8
                    && dinfo->comp_info[0].v_samp_factor==tjMCUHeight[i]/8) {
                    int match=0;
                    for(k=1; k<dinfo->num_components; k++) {
                        if(dinfo->comp_info[k].h_samp_factor==1
                            && dinfo->comp_info[k].v_samp_factor==1)
                            match++;
                    }
                    if(match==dinfo->num_components-1) {
                        retval=i;  break;
                    }
                }
            }
        }
        dbgprint("subsampling=%d\n", retval);
        if (retval >= 0 && retval < TJSAMP_NIL) {
            jpg_decoder.subsamp = retval;
        } else {
            jpg_decoder.subsamp = TJSAMP_UNK;
        }
    }

    if (jpg_decoder.subsamp != TJSAMP_420) {
        fprintf(stderr, "Error: Unexpected video image stream subsampling\n");
        jpeg_abort_decompress(dinfo);
        return;
    }

    if (jpg_decoder.outbuf[i] == NULL) {
        int ih;
        int *cw = jpg_decoder.cw;
        int *ch = jpg_decoder.ch;
        int *iw = jpg_decoder.iw;
        int *th = jpg_decoder.th;
        JSAMPROW **outbuf = jpg_decoder.outbuf;
        for(i=0; i<dinfo->num_components; i++) {
            jpeg_component_info *compptr=&dinfo->comp_info[i];
            iw[i]=compptr->width_in_blocks*DCTSIZE;
            ih=compptr->height_in_blocks*DCTSIZE;
            cw[i]=PAD(dinfo->image_width, dinfo->max_h_samp_factor)*compptr->h_samp_factor/dinfo->max_h_samp_factor;
            ch[i]=PAD(dinfo->image_height, dinfo->max_v_samp_factor)*compptr->v_samp_factor/dinfo->max_v_samp_factor;
            if(iw[i]!=cw[i] || ih!=ch[i]) {
                usetmpbuf=1;
                fprintf(stderr, "error: need a temp buffer, this shouldnt happen!\n");
                jpg_decoder.subsamp = TJSAMP_UNK;
            }
            th[i]=compptr->v_samp_factor*DCTSIZE;

            dbgprint("extra alloc: %d\n", (int)(sizeof(JSAMPROW)*ch[i]));
            if((outbuf[i]=(JSAMPROW *)malloc(sizeof(JSAMPROW)*ch[i]))==NULL) {
                fprintf(stderr, "error: malloc failure\n");
                jpeg_abort_decompress(dinfo);
                return;
            }
            for(row=0; row<ch[i]; row++){
                outbuf[i][row]=ptr;
                ptr+=PAD(cw[i], 4);
            }
        }
    }

    if(usetmpbuf) {
        fprintf(stderr, "error: Unexpected video image dimensions\n");
        jpeg_abort_decompress(dinfo);
        return;
    }

    jpeg_start_decompress(dinfo);
    if (fatal_error) {
        jpeg_abort_decompress(dinfo);
        return;
    }

    if ((int)dinfo->output_width != jpg_decoder.m_width || (int)dinfo->output_height != jpg_decoder.m_height) {
        dbgprint("error: decoder output %dx%d differs from expected %dx%d size\n",
            dinfo->output_width, dinfo->output_height, jpg_decoder.m_width, jpg_decoder.m_height);
        jpeg_abort_decompress(&jpg_decoder.dinfo);
        return;
    }

    for(row=0; row<(int)dinfo->output_height;row+=dinfo->max_v_samp_factor*DCTSIZE){
        JSAMPARRAY yuvptr[MAX_COMPONENTS];
        int crow[MAX_COMPONENTS];
        for(i=0; i<dinfo->num_components; i++){
            jpeg_component_info *compptr=&dinfo->comp_info[i];
            crow[i]=row*compptr->v_samp_factor/dinfo->max_v_samp_factor;
            yuvptr[i]=&jpg_decoder.outbuf[i][crow[i]];
        }
        jpeg_read_raw_data(dinfo, yuvptr, dinfo->max_v_samp_factor*DCTSIZE);
    }
    jpeg_finish_decompress(dinfo);
    decoder_share_frame();
}

static void apply_transform_helper(const uint8_t *src, uint8_t *dst,
                        int width, int height, int transformNull, const float *matrix)
{
    int x, y;
    float x_s, y_s, d;
    #define PIXEL(img, x, y, w, h, stride, def) \
        ((x) < 0 || (y) < 0) ? (def) : \
        (((x) >= (w) || (y) >= (h)) ? (def) : \
        img[(x) + (y) * (stride)])

    for (y = 0; y < height; y++) {
        for(x = 0; x < width; x++) {
            x_s = (float)x * matrix[0] + (float)y * matrix[1] + matrix[2];
            y_s = (float)x * matrix[3] + (float)y * matrix[4] + matrix[5];

            d = PIXEL(src, (int)(x_s + 0.5), (int)(y_s + 0.5), width, height, width, 0);
            dst[y * width + x] = (transformNull > 0 && d == 0) ? transformNull : d;
        }
    }
}

/* scratch is a working buffer of 2*ySize (2 * w * h) length */
static void apply_transform(BYTE *yuv420image, BYTE *scratch){

    // Transform Y component as is
    apply_transform_helper(yuv420image, scratch,
        WEBCAM_W, WEBCAM_H, 0,
        jpg_decoder.scale_matrix);

    apply_transform_helper(scratch, yuv420image,
        WEBCAM_W, WEBCAM_H, 0,
        jpg_decoder.angle_matrix);

    // Expand U component, transform, then sub-sample back down
    int row, col;
    BYTE *p = &yuv420image[jpg_decoder.m_webcam_ySize];
    BYTE *d = &scratch[jpg_decoder.m_webcam_ySize];

    for (row = 0; row < WEBCAM_H; row += 2) {
        for (col = 0; col < WEBCAM_W; col += 2) {
            BYTE u = *p++;
            scratch[(row+0) * WEBCAM_W + col+0] = u;
            scratch[(row+0) * WEBCAM_W + col+1] = u;
            scratch[(row+1) * WEBCAM_W + col+0] = u;
            scratch[(row+1) * WEBCAM_W + col+1] = u;
        }
    }

    apply_transform_helper(scratch, d,
        WEBCAM_W, WEBCAM_H, 0,
        jpg_decoder.scale_matrix);

    apply_transform_helper(d, scratch,
        WEBCAM_W, WEBCAM_H, 128,
        jpg_decoder.angle_matrix);

    p = &yuv420image[jpg_decoder.m_webcam_ySize];
    for (row = 0; row < WEBCAM_H; row += 2) {
        for (col = 0; col < WEBCAM_W; col += 2) {
            *p++ = scratch[row*WEBCAM_W + col];
        }
    }

    // Expand V component, transform, then sub-sample back down
    p = &yuv420image[jpg_decoder.m_webcam_ySize + jpg_decoder.m_webcam_uvSize];
    d = &scratch[jpg_decoder.m_webcam_ySize];

    for (row = 0; row < WEBCAM_H; row += 2) {
        for (col = 0; col < WEBCAM_W; col += 2) {
            BYTE v = *p++;
            scratch[(row+0) * WEBCAM_W + col+0] = v;
            scratch[(row+0) * WEBCAM_W + col+1] = v;
            scratch[(row+1) * WEBCAM_W + col+0] = v;
            scratch[(row+1) * WEBCAM_W + col+1] = v;
        }
    }
    apply_transform_helper(scratch, d,
        WEBCAM_W, WEBCAM_H, 0,
        jpg_decoder.scale_matrix);

    apply_transform_helper(d, scratch,
        WEBCAM_W, WEBCAM_H, 128,
        jpg_decoder.angle_matrix);

    p = &yuv420image[jpg_decoder.m_webcam_ySize + jpg_decoder.m_webcam_uvSize];
    for (row = 0; row < WEBCAM_H; row += 2) {
        for (col = 0; col < WEBCAM_W; col += 2) {
            *p++ = scratch[row*WEBCAM_W + col];
        }
    }
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

        sws_scale(jpg_decoder.swc, srcSlice, srcStride, 0, jpg_decoder.m_height, dstSlice, dstStride);
        p = jpg_decoder.m_webcamBuf;
    }

    // todo: This is currently super inefficient unfortunately :(
    if (jpg_decoder.transform != 0) {
        apply_transform(p, jpg_decoder.scratchBuf);
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
    decoder_rotate();
}

static void decoder_set_stransform(int value) {
    float scale =  1.0f;
    float moveX = 0;
    float moveY = 0;
    float rot = 0;

    // FILE *fp = fopen("/tmp/specs", "r");
    // if (fp) {
    //     char buf[96];
    //     if (fgets(buf, sizeof(buf), fp)) {
    //         buf[strlen(buf)-1] = '\0';
    //         sscanf(buf, "%f,%f,%f,%f", &rot, &moveX, &moveY, &scale);
    //     }
    //     fclose (fp);
    // }
    // printf("r=%f,sx=%f,sy=%f,sc=%f\n", rot, moveX, moveY, scale);

    jpg_decoder.transform = value;
    if (value == 1) {
        rot = 90;
        scale = WEBCAM_Wf / WEBCAM_Hf;
        moveX = WEBCAM_Hf;
        moveY = (WEBCAM_Hf / scale - WEBCAM_Wf) / 2.0f;
    }
    else if (value == 2) {
        rot = 180;
        moveX = WEBCAM_Wf;
        moveY = WEBCAM_Hf;
    }
    else if (value == 3) {
        rot = 270;
        scale = WEBCAM_Wf / WEBCAM_Hf;
        moveY = WEBCAM_Hf;
    }
    else {
        jpg_decoder.transform = 0;
    }

    rot = rot * M_PI / 180.0f; // deg -> rad

    fill_matrix(0, 0, 0, scale, jpg_decoder.scale_matrix);
    fill_matrix(moveX, moveY, rot, 1.0f, jpg_decoder.angle_matrix);
}

void decoder_rotate() {
    decoder_set_stransform(jpg_decoder.transform+1);
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
