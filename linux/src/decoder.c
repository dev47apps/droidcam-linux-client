/* DroidCam & DroidCamX (C) 2010-
 * Author: Aram G. (dev47apps.com)
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

#include "jpeglib.h"
#if 0
#include "speex/speex.h"
#endif

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
 int m_YuvSize, m_ySize;
 int m_NextFrame, m_NextSlot, m_BufferLimit, m_BufferedFrames;
 BYTE *m_rawBuf,*m_jpgBuf;

 /* these should be alloced for each frame but sunce the stream
  * from the app will be consistent, we'll optimize by only allocing
  * once */
 int cw[MAX_COMPONENTS], ch[MAX_COMPONENTS], iw[MAX_COMPONENTS], th[MAX_COMPONENTS];
 JSAMPROW *outbuf[MAX_COMPONENTS];

 int bc_lut_used;
 int save_next_frame;

 BYTE bc_lut[256];
 char save_file_name[PATH_MAX];

 int transform, rescale, doMirror;
 float scaleX, scaleY;
 float moveX , moveY;
 float rot;
};

#define JPG_BACKBUF_MAX 10
struct jpg_frame_s    jpg_frames[JPG_BACKBUF_MAX];
struct jpg_dec_ctx_s  jpg_decoder;
struct spx_decoder_s  spx_decoder;

static int WEBCAM_W, WEBCAM_H;
static int droidcam_device_fd;


#define MAX_COMPONENTS  10
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

    for(crt_video_dev = 0; crt_video_dev < 99; crt_video_dev++)
    {
        droidcam_device_fd = -1;
        sprintf(device, "/dev/video%d", crt_video_dev);
        if(-1 == stat(device, &st)){
            continue;
        }

        if(!S_ISCHR(st.st_mode)){
            continue;
        }

        droidcam_device_fd = open(device, O_RDWR | O_NONBLOCK, 0);

        if(-1 == droidcam_device_fd)
        {
            printf("Error opening '%s': %d '%s'\n", device, errno, strerror(errno));
            continue;
        }
        if(-1 == xioctl(droidcam_device_fd, VIDIOC_QUERYCAP, &v4l2cap))
        {
            close(droidcam_device_fd);
            continue;
        }
        printf("Device: %s\n", v4l2cap.card);
        if(0 == strncmp((const char*) v4l2cap.card, "Droidcam", 8))
        {
            printf("Found driver: %s (fd:%d)\n", device, droidcam_device_fd);
            return 1;
        }
        close(droidcam_device_fd); // not DroidCam .. keep going
        continue;
    }
    MSG_ERROR("Device not found (/dev/video[0-9]).\nDid you install it?\n");
    return 0;
}


void decoder_set_video_delay(unsigned v) {
    if (v > JPG_BACKBUF_MAX) v = JPG_BACKBUF_MAX;
    else if (v < 1) v = 1;
    jpg_decoder.m_BufferLimit = v;
    dbgprint("buffer %d frames\n", jpg_decoder.m_BufferLimit);
}

int  decoder_init(int webcam_w, int webcam_h) {
    int ret = 0;
    WEBCAM_W = webcam_w;
    WEBCAM_H = webcam_h;
    dbgprint("WEBCAM_W=%d, WEBCAM_H=%d\n", WEBCAM_W, WEBCAM_H);
    if (WEBCAM_W < 2 || WEBCAM_H < 2 || WEBCAM_W > 9999 || WEBCAM_H > 9999){
        MSG_ERROR("Invalid webcam resolution in settings");
        goto _error_out;
    }

    find_droidcam_v4l();

    fatal_error = 0;
    jpg_decoder.dinfo.err = jpeg_std_error(&jpg_decoder.jerr);
    jpg_decoder.jerr.output_message = joutput_message;
    jpg_decoder.jerr.error_exit = jerror_exit;
    jpeg_create_decompress(&jpg_decoder.dinfo);
    if (fatal_error) goto _error_out;
    jpg_decoder.init = 1;
    jpg_decoder.subsamp = TJSAMP_NIL;

#if 0
    speex_bits_init(&spx_decoder.bits);
    spx_decoder.state = speex_decoder_init(speex_lib_get_mode(SPEEX_MODEID_WB));
    speex_decoder_ctl(spx_decoder.state, SPEEX_GET_FRAME_SIZE, &spx_decoder.frame_size);
    dbgprint("spx_decoder.state=%p\n", spx_decoder.state);
#endif
    // FIXME -- GDI+ replacement? How to resize and flip ...?

    ret = 1;
_error_out:
    return ret;
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
    jpg_decoder.save_next_frame = 0;

    make_int(jpg_decoder.m_width,  header[0], header[1]);
    make_int(jpg_decoder.m_height, header[2], header[3]);

    if (jpg_decoder.m_width <= 0 || jpg_decoder.m_height <= 0) {
        MSG_ERROR("Invalid data stream!");
        return FALSE;
    }

    dbgprint("Stream W=%d H=%d\n", jpg_decoder.m_width, jpg_decoder.m_height);

    jpg_decoder.m_ySize   = jpg_decoder.m_width * jpg_decoder.m_height;
    jpg_decoder.m_YuvSize = jpg_decoder.m_ySize * 3;
    int jpegMaxlen = jpg_decoder.m_YuvSize/2;
    jpg_decoder.m_rawBuf = (BYTE*)malloc(jpg_decoder.m_YuvSize * sizeof(BYTE));
    jpg_decoder.m_jpgBuf = (BYTE*)malloc((jpegMaxlen * JPG_BACKBUF_MAX + 4096) * sizeof(BYTE));
    dbgprint("jpg: raw buf: %p\n", jpg_decoder.m_rawBuf);
    dbgprint("jpg: jpg buf: %p\n", jpg_decoder.m_jpgBuf);
    int i;
    for (i = 0; i < JPG_BACKBUF_MAX; i++) {
        jpg_frames[i].data = &jpg_decoder.m_jpgBuf[i*jpegMaxlen];
        jpg_frames[i].length = 0;
        dbgprint("jpg: jpg_frames[%d]: %p\n", i, jpg_frames[i].data);
    }

    jpg_decoder.m_BufferedFrames  = jpg_decoder.m_NextFrame = jpg_decoder.m_NextSlot = 0;
    decoder_set_video_delay(0);

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
    FREE_OBJECT(jpg_decoder.m_jpgBuf, free);
    FREE_OBJECT(jpg_decoder.m_rawBuf, free);
}

static void decode_next_jpg_frame() {
    struct jpeg_decompress_struct *dinfo = &jpg_decoder.dinfo;
    BYTE *p = jpg_frames[jpg_decoder.m_NextFrame].data;
    unsigned long len = (unsigned long)jpg_frames[jpg_decoder.m_NextFrame].length;

    int i,k, row, usetmpbuf=0;
    JSAMPLE *ptr=jpg_decoder.m_rawBuf;

    //dbgprint("frame #%2d: @%p len:%d\n", jpg_decoder.m_NextFrame, p, (int)len);
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

            fprintf(stderr, "alloc: %d\n", (int)(sizeof(JSAMPROW)*ch[i]));
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

    // fprintf(stderr, "dec_ctx.dinfo.scale_num=%d, dec_ctx.dinfo.scale_denom=%d\n",
    //   dinfo->scale_num, dinfo->scale_denom);
    // fprintf(stderr, "output_width=%d output_height=%d out_color_components=%d out_color_space=%d (yuv=%d)\n",
    //   dinfo->output_width, dinfo->output_height,
    //   dinfo->out_color_components, dinfo->out_color_space, JCS_YCbCr);

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

    if(write(droidcam_device_fd, jpg_decoder.m_rawBuf, jpg_decoder.m_YuvSize/2) == -1 && errno != EAGAIN)
        fprintf(stderr, "WARN: Failed to write frame (err#%d='%s')\n", errno, strerror(errno));
}

struct jpg_frame_s* decoder_get_next_frame() {
    while (jpg_decoder.m_BufferedFrames > jpg_decoder.m_BufferLimit) {
        jpg_decoder.m_BufferedFrames--;
        jpg_decoder.m_NextFrame = (jpg_decoder.m_NextFrame < (JPG_BACKBUF_MAX-1)) ? (jpg_decoder.m_NextFrame + 1) : 0;
    }
    if (jpg_decoder.m_BufferedFrames == jpg_decoder.m_BufferLimit) {
        // dbgprint("decoding #%2d (have buffered: %d)\n", jpg_decoder.m_NextFrame, jpg_decoder.m_BufferedFrames);
        decode_next_jpg_frame();
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
