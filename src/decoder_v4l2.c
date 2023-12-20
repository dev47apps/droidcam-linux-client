/* DroidCam & DroidCamX (C) 2010-2021
 * https://github.com/dev47apps
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include "common.h"
#include "decoder.h"

char v4l2_device[32];

static int xioctl(int fd, int request, void *arg){
    int r;
    do r = ioctl (fd, request, arg);
    while (-1 == r && EINTR == errno);
    return r;
}

static unsigned int _get_control_id(int fd, const char *control){
	const size_t length = strnlen(control, 1024);
	const unsigned next = V4L2_CTRL_FLAG_NEXT_CTRL;
	struct v4l2_queryctrl qctrl;
	int id;

	memset(&qctrl, 0, sizeof(qctrl));
	while (ioctl(fd, VIDIOC_QUERYCTRL, &qctrl) == 0) {
		if (!strncmp((const char*)qctrl.name, control, length))
			return qctrl.id;
		qctrl.id |= next;
	}
	for (id = V4L2_CID_USER_BASE; id < V4L2_CID_LASTP1; id++) {
		qctrl.id = id;
		if (ioctl(fd, VIDIOC_QUERYCTRL, &qctrl) == 0) {
			if (!strncmp((const char*)qctrl.name, control, length))
				return qctrl.id;
		}
	}
	for (qctrl.id = V4L2_CID_PRIVATE_BASE;
	     ioctl(fd, VIDIOC_QUERYCTRL, &qctrl) == 0; qctrl.id++) {
		if (!strncmp((const char*)qctrl.name, control, length)) {
			unsigned int id = qctrl.id;
			return id;
		}
	}
	return 0;
}

static int set_control_i(int fd, const char *control, int value){
	struct v4l2_control ctrl;
	memset(&ctrl, 0, sizeof(ctrl));
	ctrl.id = _get_control_id(fd, control);
	ctrl.value = value;
	if (ctrl.id && ioctl(fd, VIDIOC_S_CTRL, &ctrl) == 0) {
		int value = ctrl.value;
		return value;
	}
	return 0;
}
#if 0
static int get_control_i(int fd, const char *control){
	struct v4l2_control ctrl;
	memset(&ctrl, 0, sizeof(ctrl));
	ctrl.id = _get_control_id(fd, control);

	if (ctrl.id && ioctl(fd, VIDIOC_G_CTRL, &ctrl) == 0) {
		int value = ctrl.value;
		return value;
	}
	return 0;
}
#endif

int open_v4l2_device(void) {
    int fd;
    struct stat st;

    if (stat(v4l2_device, &st) < 0)
        return 0;

    if (!S_ISCHR(st.st_mode))
        return 0;

    fd = open(v4l2_device, O_RDWR | O_NONBLOCK, 0);
    if (fd <= 0) {
        errprint("Error opening '%s': %d '%s'\n", v4l2_device, errno, strerror(errno));
        return 0;
    }

    dbgprint("Opened %s, fd:%d\n", v4l2_device, fd);
    return fd;
}

int find_v4l2_device(const char* bus_info) {
    int bus_info_len = strlen(bus_info);
    int video_dev_fd;
    int video_dev_nr = 0;
    struct v4l2_capability v4l2cap;

    dbgprint("Looking for v4l2 card: %s\n", bus_info);
    for (video_dev_nr = 0; video_dev_nr < 99; video_dev_nr++) {
        snprintf(v4l2_device, sizeof(v4l2_device), "/dev/video%d", video_dev_nr);

        video_dev_fd = open_v4l2_device();
        if (video_dev_fd <= 0)
            continue;

        if (xioctl(video_dev_fd, VIDIOC_QUERYCAP, &v4l2cap) < 0) {
            close(video_dev_fd);
            continue;
        }

        dbgprint("Device %s is '%s' @ %s\n", v4l2_device, v4l2cap.card, v4l2cap.bus_info);
        if (0 == strncmp(bus_info, (const char*) v4l2cap.bus_info, bus_info_len)) {
            return video_dev_fd;
        }

        close(video_dev_fd);
        continue;
    }

    v4l2_device[0] = 0;
    return -1;
}

void set_v4l2_device(const char* device) {
    strncpy(v4l2_device, device, sizeof(v4l2_device) - 1);
    v4l2_device[sizeof(v4l2_device) - 1] = '\0';
}

void query_v4l_device(int fd, unsigned *WEBCAM_W, unsigned *WEBCAM_H) {
    struct v4l2_capability v4l2cap = {0};
    struct v4l2_format vid_format = {0};
    vid_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    int in_width = *WEBCAM_W;
    int in_height = *WEBCAM_H;
    *WEBCAM_W = 0;
    *WEBCAM_H = 0;

    if (xioctl(fd, VIDIOC_QUERYCAP, &v4l2cap) < 0) {
        errprint("Error: Unable to query video device. dev=%s errno=%d\n",
            v4l2_device, errno);
        return;
    }

    dbgprint("using '%s' (%s)\n", v4l2cap.card, v4l2cap.bus_info);

    const char* bus_info_dc = V4L2_PLATFORM_DC;
    if (0 != strncmp(bus_info_dc, (const char*) v4l2cap.bus_info, strlen(bus_info_dc))) {
        vid_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        xioctl(fd, VIDIOC_G_FMT, &vid_format);

        dbgprint("set fmt YU12:%dx%d\n", in_width, in_height);
        vid_format.fmt.pix.width = in_width;
        vid_format.fmt.pix.height = in_height;
        vid_format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
        vid_format.fmt.pix.field = V4L2_FIELD_NONE;
        if (xioctl(fd, VIDIOC_S_FMT, &vid_format) >= 0) {
            set_control_i(fd, "keep_format", 1);
            goto early_out;
        }

        dbgprint("set fmt failed, trying to query fmt\n");
    }

    int ret = xioctl(fd, VIDIOC_G_FMT, &vid_format);
    if (ret < 0) {
        errprint("Error: Unable to determine video device fmt. dev=%s errno=%d\n",
            v4l2_device, errno);
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
        unsigned pixelfmt = vid_format.fmt.pix.pixelformat;
        BYTE fourcc[5] = { (BYTE)(pixelfmt >> 0), (BYTE)(pixelfmt >> 8),
            (BYTE)(pixelfmt >> 16), (BYTE)(pixelfmt >> 24), '\0' };

        errprint("Fatal: video device reported pixel format %x (%s), expected %x (YU12/I420)\n"
                 "Try `v4l2loopback-ctl set-caps %s \"YU12:%dx%d\"`, or specify a different video device\n",
            vid_format.fmt.pix.pixelformat, fourcc, V4L2_PIX_FMT_YUV420,
            v4l2_device, in_width, in_height);
        return;
    }
    if (vid_format.fmt.pix.width <= 0 ||  vid_format.fmt.pix.height <= 0) {
        errprint("Fatal: droidcam video device reported invalid resolution: %dx%d\n",
            vid_format.fmt.pix.width, vid_format.fmt.pix.height);
        return;
    }

early_out:
    *WEBCAM_W = vid_format.fmt.pix.width;
    *WEBCAM_H = vid_format.fmt.pix.height;
}
