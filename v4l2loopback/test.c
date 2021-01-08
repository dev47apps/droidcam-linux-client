/*
 * How to test v4l2loopback:
 * 1. launch this test program (even in background), it will initialize the
 *    loopback device and keep it open so it won't loose the settings.
 * 2. Feed the video device with data according to the settings specified
 *    below: size, pixelformat, etc.
 *    For instance, you can try the default settings with this command:
 *    mencoder video.avi -ovc raw -nosound -vf scale=640:480,format=yuy2 -o /dev/video1
 *    TODO: a command that limits the fps would be better :)
 *
 * Test the video in your favourite viewer, for instance:
 *   luvcview -d /dev/video1 -f yuyv
 */

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#define ROUND_UP_2(num)  (((num)+1)&~1)
#define ROUND_UP_4(num)  (((num)+3)&~3)
#define ROUND_UP_8(num)  (((num)+7)&~7)
#define ROUND_UP_16(num) (((num)+15)&~15)
#define ROUND_UP_32(num) (((num)+31)&~31)
#define ROUND_UP_64(num) (((num)+63)&~63)

#if 0
# define CHECK_REREAD
#endif

#define VIDEO_DEVICE "/dev/video1"

# define FRAME_WIDTH  320
# define FRAME_HEIGHT 240

# define FRAME_FORMAT V4L2_PIX_FMT_YUV420

static int debug=1;

int format_properties(const unsigned int format,
        const unsigned int width,
        const unsigned int height,
        size_t*linewidth,
        size_t*framewidth) {
size_t lw, fw;
    switch(format) {
    case V4L2_PIX_FMT_YUV420: case V4L2_PIX_FMT_YVU420:
        lw = width; /* ??? */
        fw = ROUND_UP_4 (width) * ROUND_UP_2 (height);
        fw += 2 * ((ROUND_UP_8 (width) / 2) * (ROUND_UP_2 (height) / 2));
    break;
    case V4L2_PIX_FMT_UYVY: case V4L2_PIX_FMT_Y41P: case V4L2_PIX_FMT_YUYV: case V4L2_PIX_FMT_YVYU:
        lw = (ROUND_UP_2 (width) * 2);
        fw = lw * height;
    break;
    default:
        return 0;
    }

    if(linewidth)*linewidth=lw;
    if(framewidth)*framewidth=fw;

    return 1;
}


void print_format(struct v4l2_format*vid_format) {
  printf("  vid_format->type                =%d\n", vid_format->type );
  printf("  vid_format->fmt.pix.width       =%d\n", vid_format->fmt.pix.width );
  printf("  vid_format->fmt.pix.height      =%d\n", vid_format->fmt.pix.height );
  printf("  vid_format->fmt.pix.pixelformat =%d\n", vid_format->fmt.pix.pixelformat);
  printf("  vid_format->fmt.pix.sizeimage   =%d\n", vid_format->fmt.pix.sizeimage );
  printf("  vid_format->fmt.pix.field       =%d\n", vid_format->fmt.pix.field );
  printf("  vid_format->fmt.pix.bytesperline=%d\n", vid_format->fmt.pix.bytesperline );
  printf("  vid_format->fmt.pix.colorspace  =%d\n", vid_format->fmt.pix.colorspace );
}


struct buffer {
        __u8 *                  start;
        size_t                  length;
};


int main(int argc, char**argv)
{
    struct v4l2_capability vid_caps;
    struct v4l2_format vid_format;
    struct v4l2_requestbuffers reqbuf;
    struct v4l2_buffer buf;

    
    struct buffer * buffers;

    size_t framesize;
    size_t linewidth;

  const char*video_device=VIDEO_DEVICE;
  int fdwr = 0;
  int ret_code = 0;

   int i, j;

    if(argc>1) {
        video_device=argv[1];
        printf("using output device: %s\n", video_device);
    }

    fdwr = open(video_device, O_RDWR);
    assert(fdwr >= 0);

    ret_code = ioctl(fdwr, VIDIOC_QUERYCAP, &vid_caps);
    assert(ret_code != -1);
    if (!(vid_caps.capabilities & V4L2_CAP_STREAMING)) {
            fprintf (stderr, "%s does not support streaming i/o\n", video_device);
            exit (EXIT_FAILURE);
    }

    memset(&vid_format, 0, sizeof(vid_format));
    memset(&reqbuf, 0, sizeof(reqbuf));
    memset(&buf, 0, sizeof(buf));

    ret_code = ioctl(fdwr, VIDIOC_G_FMT, &vid_format);
    printf("VIDIOC_G_FMT return %d\n", ret_code);
    print_format(&vid_format);

    vid_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    vid_format.fmt.pix.width = FRAME_WIDTH;
    vid_format.fmt.pix.height = FRAME_HEIGHT;
    vid_format.fmt.pix.pixelformat = FRAME_FORMAT;
    vid_format.fmt.pix.sizeimage = framesize;
    vid_format.fmt.pix.field = V4L2_FIELD_NONE;
    vid_format.fmt.pix.bytesperline = linewidth;
    vid_format.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
    printf("Updated format:\n");
    print_format(&vid_format);

    ret_code = ioctl(fdwr, VIDIOC_S_FMT, &vid_format);
    printf("VIDIOC_S_FMT return %d\n", ret_code);
    assert(ret_code != -1);

    print_format(&vid_format);

    if(!format_properties(vid_format.fmt.pix.pixelformat,
                        vid_format.fmt.pix.width, vid_format.fmt.pix.height,
                        &linewidth,
                        &framesize)) {
        printf("unable to guess correct settings for format '%d'\n", FRAME_FORMAT);
    }
    
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = 1;
    ret_code = ioctl (fdwr, VIDIOC_REQBUFS, &reqbuf);
    printf("VIDIOC_REQBUFS return %d\n", ret_code);
    assert(ret_code != -1);

    buffers = calloc(reqbuf.count, sizeof (buffers));
    assert(buffers != NULL);
    
    printf("Mapping buffer\n");
    buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory      = V4L2_MEMORY_MMAP;
    buf.index       = 1;
    ioctl (fdwr, VIDIOC_QUERYBUF, &buf);
    buffers[0].length = buf.length;
    buffers[0].start =  mmap (NULL /* start anywhere */,
                              buf.length,
                              PROT_READ | PROT_WRITE /* required */,
                              MAP_SHARED /* recommended */,
                              fdwr, buf.m.offset);

    assert(MAP_FAILED != buffers[0].start);

    for (j = 0; j < 256; i++)
    {
        for (i = 0; i < framesize; ++i) {
            buffers[0].start[i] = 100;//i%j;
        }
        usleep(100000);
    }

    pause();

    munmap(buffers[0].start, buffers[0].length);
    close(fdwr);
    return 0;
}
