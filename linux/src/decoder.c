/* DroidCam & DroidCamX (C) 2010-
 * Author: Aram G. (dev47@dev47apps.com)
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Use at your own risk. See README file for more details.
 */
#ifdef HAVE_AV_CONFIG_H
#undef HAVE_AV_CONFIG_H
#endif

#include <gtk/gtk.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"
#include "decoder.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"

#define VIDEO_BUF_FRAMES   16 // H.263 Frames to Buffer
#define VIDEO_BUF_SLEEP_MS 40 // 25fps (MediaRecoder seems to ignore the specified fps)

#define FREE_OBJECT(obj, free_func) if(obj){dbgprint(" " #obj " %p\n", obj); free_func(obj); obj=NULL;}
#define SHARE_FRAME(ptr, len) \
    if(write(droidcam_device_fd,ptr,len) == -1 && errno != EAGAIN) \
        printf("WARN: Failed to write frame (err#%d='%s')\n", errno, strerror(errno))

static int droidcam_device_fd;

int m_width, m_height, m_format; // stream params (sent from phone)
int share_w, share_h;            // webcam params (from settings file)

static uint8_t * m_videoStreamBuf = NULL;// buffer for incoming stream
static int m_videoStreamBufSize;
static int m_videoStreamFrameLen;

static uint8_t * m_shareFrameBuf = NULL; // buffer for the webcam
static int m_shareFrameBufSize;

static GThread* hDisplayThread = NULL;
static unsigned m_DecodeSeqNum, m_DisplaySeqNum; // Used in H263 for thread sync

static AVPacket        v_packet;
static AVCodec        *v_codec_b, *v_codec_c;
static AVCodecContext *v_context = NULL;
struct SwsContext     *swc  = NULL;
static AVFrame        *decode_frame = NULL; // decoded frames
static AVFrame        *share_frame  = NULL; // resized frames (mapped to m_shareFrameBuf)

static AVFrame * scan_frame = NULL;// used for H263 to resize into back-buffer
static uint8_t * scan_ptr   = NULL;// scans back-buffer (m_videoStreamBuf)

static int xioctl(int fd, int request, void *arg){
    int r;
    do r = ioctl (fd, request, arg);
    while (-1 == r && EINTR == errno);
    return r;
}

static int find_droidcam_v4l(){
    int crt_video_dev = 0;
    char device[12];
    struct stat st;
    struct v4l2_capability v4l2cap;

    // look at the first 10 video devices
    for(crt_video_dev = 0; crt_video_dev < 9; crt_video_dev++)
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
    MSG_ERROR("Device not found (/dev/video[0-9]).\nDid you install it? \n");
    return 0;
}

void * DisplayThreadProc(void * args){

    while (m_DecodeSeqNum < 1 && m_videoStreamBuf != NULL) // Wait for at least one frame
        usleep(1000);

    uint8_t * ptrRead = m_videoStreamBuf;
    uint8_t * buf_end = m_videoStreamBuf + m_videoStreamBufSize;

    dbgprint("Video Display Thread avast!\n");

    while (m_videoStreamBuf != NULL)
    {
        if (m_DisplaySeqNum < m_DecodeSeqNum)
        {
            SHARE_FRAME(ptrRead, m_videoStreamFrameLen);
            m_DisplaySeqNum ++;

            ptrRead += m_videoStreamFrameLen;
            if (ptrRead >= buf_end) ptrRead = m_videoStreamBuf;
            usleep(VIDEO_BUF_SLEEP_MS * 1000);
        } else {
            g_thread_yield ();
        }
    }

    dbgprint("Video Display Thread End\n");
    return NULL;
}

int  decoder_init(int w, int h){
    int ret = 0;
    if (!find_droidcam_v4l())
        goto _error_out;

    share_w = w;
    share_h = h;
    m_shareFrameBufSize = YUV_BUFFER_SZ(w, h); //avpicture_get_size(PIX_FMT_YUV420P,w,h);

    if (m_shareFrameBufSize < VIDEO_INBUF_SZ){
        MSG_ERROR("Invalid webcam resolution in settings");
        goto _error_out;
    }

    avcodec_init();
    avcodec_register_all();

    share_frame = avcodec_alloc_frame();
    m_shareFrameBuf = (uint8_t*)av_malloc( m_shareFrameBufSize * sizeof(uint8_t));
    avpicture_fill((AVPicture *)share_frame, m_shareFrameBuf, PIX_FMT_YUV420P, share_w, share_h);
    SHARE_FRAME(m_shareFrameBuf, m_shareFrameBufSize);

    av_init_packet(&v_packet);
    //av_init_packet(&a_packet);

    v_codec_b = avcodec_find_decoder(CODEC_ID_H263);
    v_codec_c = avcodec_find_decoder(CODEC_ID_MJPEG);
    //a_codec = avcodec_find_decoder(CODEC_ID_AMR_NB);

    if (!v_codec_b || !v_codec_c ){//|| !a_codec) {
        MSG_ERROR("Decoder Error (1)");
        goto _error_out;
    }

    ret = 1;
_error_out:
    return ret;
}

void decoder_fini()
{
    if (droidcam_device_fd) close(droidcam_device_fd);
    FREE_OBJECT(m_shareFrameBuf, av_free);
    FREE_OBJECT(share_frame, av_free);
}

int decoder_prepare_video(char * header)
{
    int ret = FALSE;

    #define make_int(num, b1, b2)   num = 0; num |=(b1&0xFF); num <<= 8; num |= (b2&0xFF);

    make_int(m_width,  header[0], header[1]);
    make_int(m_height, header[2], header[3]);

    if (m_width <= 0 || m_height <= 0) {
        MSG_ERROR("Invalid data stream!");
        goto _error_out;
    }
    m_format = (int) header[4];
    dbgprint("W=%d H=%d Fmt=%d\n", m_width, m_height, m_format);

    // ignore phone app version
    m_format = m_format % 3;
    if (m_format == 0) m_format = 3;

    decode_frame = avcodec_alloc_frame();

    if (m_format == VIDEO_FMT_YUV) {
        m_videoStreamBufSize = 0;
        m_videoStreamFrameLen = YUV_BUFFER_SZ(m_width, m_height);
        m_videoStreamBuf = (uint8_t*)av_malloc((m_videoStreamFrameLen + VIDEO_INBUF_SZ) * sizeof(uint8_t));

        swc = sws_getContext(m_width, m_height, PIX_FMT_NV21, /* src */
                             share_w, share_h , PIX_FMT_YUV420P, /* dst */
                             SWS_FAST_BILINEAR /* flags */, NULL, NULL, NULL);
        dbgprint("yuv buffer %p\n", m_videoStreamBuf);
    } else {
        v_context = avcodec_alloc_context();

        if (!v_context || !decode_frame){
            MSG_ERROR("Decoder Error (2)");
            goto _error_out;
        }

        v_context->width   = m_width;
        v_context->height  = m_height;
        dbgprint("v_context ... w=%d, h=%d \n", m_width, m_height);

        swc = sws_getContext(m_width, m_height, PIX_FMT_YUV420P, /* src */
                                 share_w, share_h , PIX_FMT_YUV420P, /* dst */
                                 SWS_FAST_BILINEAR /* flags */, NULL, NULL, NULL);

        if (m_format == VIDEO_FMT_H263){
            v_context->flags |= CODEC_FLAG_TRUNCATED;
            v_context->flags |= CODEC_FLAG_LOW_DELAY;

            if (avcodec_open(v_context, v_codec_b) < 0) {
                MSG_ERROR("Decoder Error (3)");
                goto _error_out;
            }

            // H.263 frames are decoded in 'batches' resulting in bursts
            // We have to buffer the frames and play them smoothly
            // in a seperate thread (hDisplayThread).
            // Frames will be decoded then resized INTO m_videoStreamBuf
            // via scan_ptr and scan_frame.

            m_videoStreamFrameLen = YUV_BUFFER_SZ(share_w, share_h);
            m_videoStreamBufSize  = m_videoStreamFrameLen * VIDEO_BUF_FRAMES;
            m_videoStreamBuf = (uint8_t*)av_malloc(m_videoStreamBufSize * sizeof(uint8_t));

            scan_frame = avcodec_alloc_frame();
            scan_ptr   = m_videoStreamBuf;

            m_DecodeSeqNum = m_DisplaySeqNum = 0;
            hDisplayThread = g_thread_create(DisplayThreadProc, NULL, TRUE, NULL);
            dbgprint("Allocated h263 buffer %p (len=%d)\n", m_videoStreamBuf, m_videoStreamBufSize);
        }
        else {
            if (avcodec_open(v_context, v_codec_c) < 0) {
                MSG_ERROR("Decoder Error (4)");
                goto _error_out;
            }

            m_videoStreamBufSize = 0;
            m_videoStreamFrameLen = YUV_BUFFER_SZ(m_width, m_height);
            m_videoStreamBuf = (uint8_t*)av_malloc((m_videoStreamFrameLen) * sizeof(uint8_t));

            // Uncompressed jpeg is yuv420p size
            // We would need to scan the incoming buffer for EOI marker
            // But instead we just fill the buffer a bit and consume
            m_videoStreamFrameLen /= 4;

            dbgprint("Allocated jpeg buffer %p (limit = %d)\n", m_videoStreamBuf, m_videoStreamFrameLen);
        }

    }
    ret = TRUE;

_error_out:
    return ret;
}
void decoder_cleanup()
{
    dbgprint("Cleanup\n");

    if (v_context) avcodec_close(v_context);
    FREE_OBJECT(v_context, av_free);

    FREE_OBJECT(m_videoStreamBuf, av_free);
    FREE_OBJECT(decode_frame, av_free);
    FREE_OBJECT(scan_frame, av_free);
    FREE_OBJECT(swc, sws_freeContext);

    FREE_OBJECT(hDisplayThread, g_thread_join);
}


int DecodeVideo(char * data, int length)
{
    if (m_format == VIDEO_FMT_YUV){
        memcpy(m_videoStreamBuf + m_videoStreamBufSize, data, length);
        m_videoStreamBufSize += length;

        if ( m_videoStreamBufSize >= m_videoStreamFrameLen ) // Have a complete YUV frame
        {
            avpicture_fill((AVPicture *)decode_frame, m_videoStreamBuf, PIX_FMT_NV21, m_width, m_height);
            sws_scale(swc, decode_frame->data, decode_frame->linesize, 0, m_height, share_frame->data, share_frame->linesize);
            SHARE_FRAME(m_shareFrameBuf, m_shareFrameBufSize);

            m_videoStreamBufSize -= m_videoStreamFrameLen; // shift back
            memmove(m_videoStreamBuf, m_videoStreamBuf + m_videoStreamFrameLen, m_videoStreamBufSize);
            #if 0
            int i, area = m_width * m_height;
            memcpy(m_videoDecodeBuf, m_videoBackBuf, area); // y
            uint8_t * pbb = m_videoBackBuf + area; // Andoird NV21: "YYY..VUVU.."
            uint8_t * pu = m_videoDecodeBuf + area;
            uint8_t * pv = m_videoDecodeBuf + area;
            area = area >> 2;
            pv += area;
            for (i = 0; i < area; i++)
            {
                *pv++ = *pbb++;
                *pu++ = *pbb++;
            }
            #endif
        }
    }
    else if(m_format == VIDEO_FMT_JPEG)
    {
        memcpy(m_videoStreamBuf + m_videoStreamBufSize, data, length);
        m_videoStreamBufSize += length;

        while ( m_videoStreamBufSize >= m_videoStreamFrameLen )
        {
            v_packet.data = m_videoStreamBuf;
            v_packet.size = m_videoStreamBufSize;

            int len, have_frame = 0;
            if ((len = avcodec_decode_video2(v_context, decode_frame, &have_frame, &v_packet)) < 0){
                printf("VIDEO DECODE ERROR (%d)\n", have_frame);
                return FALSE;
            }
            sws_scale(swc, decode_frame->data, decode_frame->linesize, 0, m_height, share_frame->data, share_frame->linesize);
            SHARE_FRAME(m_shareFrameBuf, m_shareFrameBufSize);

            m_videoStreamBufSize -= len; // shift back
            memmove(m_videoStreamBuf, m_videoStreamBuf + len, m_videoStreamBufSize);
            #if 0
            uint8_t * pbuf = m_shareFrameBuf;
            uint8_t * pyuv = frame->data[0]; // y
            int i;
            for (i =0; i < share_h; i++) {
                memcpy(pbuf, pyuv, share_w);
                pbuf += share_w;
                pyuv += frame->linesize[0];
            }

            pyuv = frame->data[1]; // u
            for (i =0; i < share_h/2; i++) {
                memcpy(pbuf, pyuv, share_w/2);
                pbuf += share_w/2;
                pyuv += frame->linesize[1];
            }
            pyuv = frame->data[2]; // v
            for (i =0; i < share_h/2; i++) {
                memcpy(pbuf, pyuv, share_w/2);
                pbuf += share_w/2;
                pyuv += frame->linesize[2];
            }
            #endif
        }
    }
    else { // H263
        v_packet.data = (uint8_t*)data;
        v_packet.size = length;

        uint8_t * buf_end = m_videoStreamBuf + m_videoStreamBufSize;
        int len=0, have_frame=0;
        while (v_packet.size > 0)
        {
            len = avcodec_decode_video2(v_context, decode_frame, &have_frame, &v_packet);
            if (len < 0 ){
                printf("VIDEO DECODE ERROR len=%d fmt=%d\n", len, v_context->pix_fmt);
                return FALSE;
            }
            if (have_frame)
            {
                avpicture_fill((AVPicture *)scan_frame, scan_ptr, PIX_FMT_YUV420P, share_w, share_h);
                sws_scale(swc, decode_frame->data, decode_frame->linesize, 0, m_height, scan_frame->data, scan_frame->linesize);
                m_DecodeSeqNum ++;

                scan_ptr += m_videoStreamFrameLen;
                if (scan_ptr >= buf_end) scan_ptr = m_videoStreamBuf;

                while ((m_DecodeSeqNum - m_DisplaySeqNum) >= VIDEO_BUF_FRAMES){
                    g_thread_yield ();
                }
            }
            v_packet.size -= len;
            v_packet.data += len;
        }
    }
    return TRUE;
}

int DecodeAudio(char * data, int length){
    return FALSE;
}

int GetVideoWidth(){
    return m_width;
}
int GetVideoHeight(){
    return m_height;
}
