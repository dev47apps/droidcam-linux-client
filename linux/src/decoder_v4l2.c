#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/videodev2.h>

#include "common.h"
#include "decoder.h"

char v4l2_device[32];

static int xioctl(int fd, int request, void *arg){
    int r;
    do r = ioctl (fd, request, arg);
    while (-1 == r && EINTR == errno);
    return r;
}

int find_droidcam_v4l() {
	int droidcam_device_fd;
    int crt_video_dev = 0;
    struct stat st;
    struct v4l2_capability v4l2cap;

    for(crt_video_dev = 0; crt_video_dev < 99; crt_video_dev++) {
        snprintf(v4l2_device, sizeof(v4l2_device), "/dev/video%d", crt_video_dev);
        if (-1 == stat(v4l2_device, &st))
            continue;

        if (!S_ISCHR(st.st_mode))
            continue;

        droidcam_device_fd = open(v4l2_device, O_RDWR | O_NONBLOCK, 0);
        if (-1 == droidcam_device_fd) {
            printf("Error opening '%s': %d '%s'\n", v4l2_device, errno, strerror(errno));
            continue;
        }

        if (-1 == xioctl(droidcam_device_fd, VIDIOC_QUERYCAP, &v4l2cap)) {
            close(droidcam_device_fd);
            continue;
        }

        printf("Device %s is '%s'\n", v4l2_device, v4l2cap.card);
        if (0 == strncmp((const char*) v4l2cap.card, "Droidcam", 8)) {
            printf("Opened %s, fd:%d\n", v4l2_device, droidcam_device_fd);
            return droidcam_device_fd;
        }

        close(droidcam_device_fd);
        continue;
    }

    v4l2_device[0] = 0;
    return -1;
}

void query_droidcam_v4l(int droidcam_device_fd, int *WEBCAM_W, int *WEBCAM_H) {
    struct v4l2_format vid_format = {0};
    vid_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    vid_format.fmt.pix.width = 0;
    vid_format.fmt.pix.height = 0;

    int ret = xioctl(droidcam_device_fd, VIDIOC_G_FMT, &vid_format);
    if (ret < 0 && errno == EINVAL) {
        fprintf(stderr, "No format set, querying default format\n");
        vid_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        vid_format.fmt.pix.width = 0;
        vid_format.fmt.pix.height = 0;
        vid_format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
        vid_format.fmt.pix.field = V4L2_FIELD_ANY;
        if (xioctl(droidcam_device_fd, VIDIOC_TRY_FMT, &vid_format) < 0) {
            fprintf(stderr, "Fatal: Unable to query droidcam video device. errno=%d\n", errno);
            return;
        }
        if (xioctl(droidcam_device_fd, VIDIOC_S_FMT, &vid_format) < 0) {
            fprintf(stderr, "Fatal: Unable to adjust droidcam video device. errno=%d\n", errno);
            return;
        }
    } else if (ret < 0) {
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
        uint32_t pixelfmt = vid_format.fmt.pix.pixelformat;
        uint8_t fourcc[5] = { (uint8_t)(pixelfmt >> 0), (uint8_t)(pixelfmt >> 8),
            (uint8_t)(pixelfmt >> 16), (uint8_t)(pixelfmt >> 24), '\0' };
        errprint("Fatal: droidcam video device reported pixel format %x (%s), expected %x (YU12/I420)\n"
                 "Try 'v4l2loopback-ctl set-caps \"video/x-raw, format=I420, width=640, height=480\" %s'\n",
            vid_format.fmt.pix.pixelformat, fourcc, V4L2_PIX_FMT_YUV420, "/dev/video<N>");
        return;
    }
    if (vid_format.fmt.pix.width <= 0 ||  vid_format.fmt.pix.height <= 0) {
        errprint("Fatal: droidcam video device reported invalid resolution: %dx%d\n",
            vid_format.fmt.pix.width, vid_format.fmt.pix.height);
        return;
    }

    *WEBCAM_W = vid_format.fmt.pix.width;
    *WEBCAM_H = vid_format.fmt.pix.height;
}
