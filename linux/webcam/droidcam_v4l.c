/* DroidCam V4L2 Driver (C) 2010-
 * Author: Aram G. (dev47@dev47apps.com)
 * Feel free to improve this implementation .. do let me know if you do.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Use at your own risk. See README file for more details.
 * 
 * Driver code based on http://sourceforge.net/projects/smartcam/
 * Credits to Ionut Dediu & Tomas Janousek
 */

#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-dev.h>
#include <linux/version.h>
#include <linux/sched.h>

#ifndef module_init
    #include <module.h>
#endif

#ifdef CONFIG_VIDEO_V4L1_COMPAT
#include <linux/videodev.h>
#endif

//#define DEBUG_MSG(fmt, args...) printk(KERN_ALERT "$ " fmt, ## args)
#define DEBUG_MSG(fmt, args...)

#define DROIDCAM_MAJOR_VERSION  1
#define DROIDCAM_MINOR_VERSION  0
#define DROIDCAM_RELEASE        3
#define DROIDCAM_VERSION        KERNEL_VERSION(DROIDCAM_MAJOR_VERSION, DROIDCAM_MINOR_VERSION, DROIDCAM_RELEASE)
#define MAX_STREAMING_BUFFERS   4
#define NFORMATS                1

#define DEFAULT_FRAME_WIDTH  320
#define DEFAULT_FRAME_HEIGHT 240

static const char fmtdesc[][] = { "[YU12]" };
static int        format = 0;

static DECLARE_WAIT_QUEUE_HEAD(wq);

static int width = DEFAULT_FRAME_WIDTH, height = DEFAULT_FRAME_HEIGHT;
static int yuv_frame_size, buffer_size;

static char* frame_data = NULL;
static __u32 frame_sequence = 0;
static __u32 last_read_frame = 0;
static struct timeval frame_timestamp;

static struct v4l2_pix_format formats[] = {
{
    .width          = 0,
    .height         = 0,
    .pixelformat    = V4L2_PIX_FMT_YVU420,
    .field          = V4L2_FIELD_NONE,
    .bytesperline   = 0,
    .sizeimage      = 0,
    .colorspace     = V4L2_COLORSPACE_SRGB,
    .priv           = 0,
}};

// == MMAP ==
static int droidcam_mmap(struct file *file, struct vm_area_struct *vma)
{
    int ret;
    long length = vma->vm_end - vma->vm_start;
    unsigned long start = vma->vm_start;
    char *vmalloc_area_ptr = frame_data;
    unsigned long pfn;

    DEBUG_MSG("(%s) %s called\n", current->comm, __FUNCTION__);

    if (length > buffer_len)
            return -EIO;

    /* loop over all pages, map each page individually */
    while (length > 0) {
            pfn = vmalloc_to_pfn (vmalloc_area_ptr);
        ret = remap_pfn_range(vma, start, pfn, PAGE_SIZE, PAGE_SHARED);
        if(ret < 0) {
            return ret;
        }
        start += PAGE_SIZE;
        vmalloc_area_ptr += PAGE_SIZE;
        length -= PAGE_SIZE;
    }
    return 0;
}

// == File operations ==
static int droidcam_open(struct inode *inode, struct file *file)
{
    int minor = 0;
    minor = iminor(inode);
    DEBUG_MSG("(%s) %s called (minor=%d)\n", current->comm, __FUNCTION__, minor);
    return 0;
}

static ssize_t droidcam_read(struct file *file, char __user *data, size_t count, loff_t *f_pos)
{
    DEBUG_MSG("(%s) %s called (count=%d, f_pos = %d)\n", current->comm, __FUNCTION__, (int)count, (int) *f_pos);

    if(*f_pos >= formats[format].sizeimage)
        return 0;

    if (!(file->f_flags & O_NONBLOCK))
        interruptible_sleep_on_timeout(&wq, HZ/10); /* wait max 1 second */
    last_read_frame = frame_sequence;

    if(*f_pos + count > formats[format].sizeimage)
        count = formats[format].sizeimage - *f_pos;

    if(copy_to_user(data, frame_data + *f_pos, count))
    {
        return -EFAULT;
    }
    return 0;
}

static ssize_t droidcam_write(struct file *file, const char __user *data, size_t count, loff_t *f_pos)
{
    DEBUG_MSG("(%s) %s called (count=%d, f_pos = %d)\n", current->comm, __FUNCTION__, (int)count, (int) *f_pos);

    if (count >= buffer_size)
        count = buffer_size;

    // Grab data from DroidCam app
    if(copy_from_user(frame_data, data, count))
    {
        return -EFAULT;
    }
    ++ frame_sequence;
    do_gettimeofday(&frame_timestamp);
    wake_up_interruptible_all(&wq);
    return count;
}

static unsigned int droidcam_poll(struct file *file, struct poll_table_struct *wait)
{
    DEBUG_MSG("(%s) %s called // %s\n", current->comm, __FUNCTION__, (last_read_frame != frame_sequence) ? "R" : "W");

    interruptible_sleep_on_timeout(&wq, HZ/100); // delay to prevent CPU hogging
    poll_wait(file, &wq, wait);

    return (POLLIN | POLLOUT | POLLWRNORM);//mask;
}

static int droidcam_release(struct inode *inode, struct file *file)
{
    DEBUG_MSG("(%s) %s called\n", current->comm, __FUNCTION__);
    return 0;
}

// == IOCTL vidioc handling ==

static int vidioc_querycap(struct file *file, void  *priv, struct v4l2_capability *cap)
{
    DEBUG_MSG("(%s) %s called\n", current->comm, __FUNCTION__);
    strcpy(cap->driver, "DroidCam");
    strcpy(cap->card,   "DroidCam"); // shows up as webcam name
    cap->version = DROIDCAM_VERSION;
    cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
    return 0;
}

static int vidioc_enum_fmt_cap(struct file *file, void  *priv, struct v4l2_fmtdesc *f)
{
    DEBUG_MSG("(%s) %s called, index=%d\n", current->comm, __FUNCTION__, f->index);
    if(f->index < NFORMATS)
    {
        strlcpy(f->description, fmtdesc[f->index], sizeof(f->description));
        f->pixelformat = formats[f->index].pixelformat;
        f->flags = 0;
        return 0;
    }
    return -EINVAL;
}

static int vidioc_g_fmt_cap(struct file *file, void *priv, struct v4l2_format *f)
{
    DEBUG_MSG("(%s) %s called\n", current->comm, __FUNCTION__);
    f->fmt.pix = formats[format];
    return 0;
}

static int vidioc_try_fmt_cap(struct file *file, void *priv, struct v4l2_format *f)
{
    int i,ret = -EINVAL;
    DEBUG_MSG("(%s) %s called, type=%d fmt=%d\n", current->comm, __FUNCTION__, f->type, f->fmt.pix.pixelformat);
    for (i = 0; i < NFORMATS; i++) {
        if (f->fmt.pix.pixelformat == formats[i].pixelformat) {
            f->fmt.pix = formats[i];
            ret = 0;
        }
    }
    return ret;
}

static int vidioc_s_fmt_cap(struct file *file, void *priv, struct v4l2_format *f)
{
    return vidioc_try_fmt_cap(file, priv, f);
}

// Streaming IO
static int vidioc_reqbufs(struct file *file, void *priv, struct v4l2_requestbuffers *reqbuf)
{
    int ret = 0;
    DEBUG_MSG("(%s) %s called\n", current->comm, __FUNCTION__);

    if(reqbuf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE || reqbuf->memory != V4L2_MEMORY_MMAP)
    {
        ret = -EINVAL;
        goto OUT;
    }

    if(reqbuf->count < 1)
        reqbuf->count = 1;
    if(reqbuf->count > MAX_STREAMING_BUFFERS)
        reqbuf->count = MAX_STREAMING_BUFFERS;
OUT:
    DEBUG_MSG(" ==> return %d //  reqbuf->count=%d\n", ret, reqbuf->count);
    return ret;
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *vidbuf)
{
    DEBUG_MSG("(%s) %s called\n", current->comm, __FUNCTION__);

    if(vidbuf->index < 0 || vidbuf->index >= MAX_STREAMING_BUFFERS)
    {
        DEBUG_MSG("vidioc_querybuf called - invalid buf index\n");
        return -EINVAL;
    }
    if(vidbuf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
    {
        DEBUG_MSG("vidioc_querybuf called - invalid buf type\n");
        return -EINVAL;
    }
    vidbuf->memory = V4L2_MEMORY_MMAP;
    vidbuf->length = buffer_len;
    vidbuf->bytesused = formats[format].sizeimage;
    vidbuf->flags = V4L2_BUF_FLAG_MAPPED;
    vidbuf->m.offset = 2 * vidbuf->index * vidbuf->length;
    vidbuf->reserved = 0;
    return 0;
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *vidbuf)
{
    DEBUG_MSG("(%s) %s called\n", current->comm, __FUNCTION__);

    if(vidbuf->index < 0 || vidbuf->index >= MAX_STREAMING_BUFFERS)
    {
        return -EINVAL;
    }
    if(vidbuf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
    {
        return -EINVAL;
    }
    if(vidbuf->memory != V4L2_MEMORY_MMAP)
    {
        return -EINVAL;
    }
    vidbuf->length = buffer_len;
    vidbuf->bytesused = formats[format].sizeimage;
    vidbuf->flags = V4L2_BUF_FLAG_MAPPED;
    return 0;
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *vidbuf)
{
    if(file->f_flags & O_NONBLOCK)
        DEBUG_MSG("(%s) %s called (non-blocking)\n", current->comm, __FUNCTION__);
    else
        DEBUG_MSG("(%s) %s called (blocking)\n", current->comm, __FUNCTION__);

    if(vidbuf->index < 0 || vidbuf->index >= MAX_STREAMING_BUFFERS)
    {
        return -EINVAL;
    }
    if(vidbuf->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
    {
        return -EINVAL;
    }
    if(vidbuf->memory != V4L2_MEMORY_MMAP)
    {
        return -EINVAL;
    }

    if(!(file->f_flags & O_NONBLOCK))
        interruptible_sleep_on_timeout(&wq, HZ); /* wait max 1 second */

    vidbuf->length = buffer_len;
    vidbuf->bytesused = formats[format].sizeimage;
    vidbuf->flags = V4L2_BUF_FLAG_MAPPED;
    vidbuf->timestamp = frame_timestamp;
    vidbuf->sequence = frame_sequence;
    last_read_frame = frame_sequence;
    return 0;
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf (struct file *file, void *priv, struct video_mbuf *mbuf)
{
    DEBUG_MSG("(%s) %s called\n", current->comm, __FUNCTION__);
    return 0;
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
    DEBUG_MSG("(%s) %s called\n", current->comm, __FUNCTION__);
    return 0;
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
    DEBUG_MSG("(%s) %s called\n", current->comm, __FUNCTION__);
    return 0;
}

static int vidioc_s_std (struct file *file, void *priv, v4l2_std_id *i)
{
    DEBUG_MSG("(%s) %s called\n", current->comm, __FUNCTION__);
    return 0;
}

// == Input (only one) ==
static int vidioc_enum_input(struct file *file, void *priv, struct v4l2_input *inp)
{
    if(inp->index != 0)
    {
        DEBUG_MSG("(%s) %s called - return EINVAL\n", current->comm, __FUNCTION__);
        return -EINVAL;
    }
    else
    {
        DEBUG_MSG("(%s) %s called - return 0\n", current->comm, __FUNCTION__);
    }
    inp->type = V4L2_INPUT_TYPE_CAMERA;
    inp->std = V4L2_STD_NTSC_M;
    strcpy(inp->name, "droidcam input");

    return 0;
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
    DEBUG_MSG("(%s) %s called\n", current->comm, __FUNCTION__);
    *i = 0;
    return 0;
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
    DEBUG_MSG("(%s) %s called, input = %d\n", current->comm, __FUNCTION__, i);
    if(i > 0)
        return -EINVAL;

    return 0;
}

// == Controls ==
static int vidioc_queryctrl(struct file *file, void *priv, struct v4l2_queryctrl *qc)
{
    DEBUG_MSG("(%s) %s called\n", current->comm, __FUNCTION__);
    return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv, struct v4l2_control *ctrl)
{
    DEBUG_MSG("(%s) %s called - return EINVAL\n", current->comm, __FUNCTION__);
    return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv, struct v4l2_control *ctrl)
{
    DEBUG_MSG("(%s) %s called - return EINVAL\n", current->comm, __FUNCTION__);
    return -EINVAL;
}

static int vidioc_cropcap(struct file *file, void *priv, struct v4l2_cropcap *cropcap)
{
    struct v4l2_rect defrect;

    DEBUG_MSG("(%s) %s called - return 0\n", current->comm, __FUNCTION__);

    defrect.left = defrect.top = 0;
    defrect.width =  width;
    defrect.height = height;

    cropcap->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cropcap->bounds = cropcap->defrect = defrect;
    return 0;

}

static int vidioc_g_crop(struct file *file, void *priv, struct v4l2_crop *crop)
{
    DEBUG_MSG("%s called - return EINVAL\n", __FUNCTION__);
    return -EINVAL;
}

static int vidioc_s_crop(struct file *file, void *priv, struct v4l2_crop *crop)
{
    DEBUG_MSG("(%s) %s called - return EINVAL\n", current->comm, __FUNCTION__);
    //return -EINVAL;
    return 0;
}

static int vidioc_g_parm(struct file *file, void *priv, struct v4l2_streamparm *streamparm)
{
    DEBUG_MSG("(%s) %s called - return 0\n", current->comm, __FUNCTION__);

    memset(streamparm, 0, sizeof(*streamparm));
    streamparm->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    streamparm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
    streamparm->parm.capture.capturemode = 0;
    streamparm->parm.capture.timeperframe.numerator = 1;
    streamparm->parm.capture.timeperframe.denominator = 10;
    streamparm->parm.capture.extendedmode = 0;
    streamparm->parm.capture.readbuffers = 3;

    return 0;
}

static int vidioc_s_parm(struct file *file, void *priv, struct v4l2_streamparm *streamparm)
{
    if(streamparm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
    {
        DEBUG_MSG("(%s) %s called; numerator=%d, denominator=%d - return EINVAL\n", current->comm, __FUNCTION__,
             streamparm->parm.capture.timeperframe.numerator, streamparm->parm.capture.timeperframe.denominator);
        return -EINVAL;
    }
    DEBUG_MSG("(%s) %s called; numerator=%d, denominator=%d, readbuffers=%d - return 0\n", current->comm, __FUNCTION__,
    streamparm->parm.capture.timeperframe.numerator,
    streamparm->parm.capture.timeperframe.denominator,
    streamparm->parm.capture.readbuffers);

    return 0;
}

// Tell OS about the functions
static const struct v4l2_file_operations droidcam_fops = {
    .owner      = THIS_MODULE,
    .open       = droidcam_open,
    .release    = droidcam_release,
    .read       = droidcam_read,
    .write      = droidcam_write,
    .poll       = droidcam_poll,
    .ioctl      = video_ioctl2, /* V4L2 ioctl handler */
    .mmap       = droidcam_mmap,
};

static const struct v4l2_ioctl_ops droidcam_ioctl_ops = {
    .vidioc_querycap            = vidioc_querycap,
    .vidioc_enum_fmt_vid_cap    = vidioc_enum_fmt_cap,
    .vidioc_g_fmt_vid_cap       = vidioc_g_fmt_cap,
    .vidioc_try_fmt_vid_cap     = vidioc_try_fmt_cap,
    .vidioc_s_fmt_vid_cap       = vidioc_s_fmt_cap,
    .vidioc_reqbufs     = vidioc_reqbufs,
    .vidioc_querybuf    = vidioc_querybuf,
    .vidioc_qbuf        = vidioc_qbuf,
    .vidioc_dqbuf       = vidioc_dqbuf,
    .vidioc_s_std       = vidioc_s_std,
    .vidioc_enum_input  = vidioc_enum_input,
    .vidioc_g_input     = vidioc_g_input,
    .vidioc_s_input     = vidioc_s_input,
    .vidioc_queryctrl   = vidioc_queryctrl,
    .vidioc_g_ctrl      = vidioc_g_ctrl,
    .vidioc_s_ctrl      = vidioc_s_ctrl,
    .vidioc_cropcap     = vidioc_cropcap,
    .vidioc_g_crop      = vidioc_g_crop,
    .vidioc_s_crop      = vidioc_s_crop,
    .vidioc_g_parm      = vidioc_g_parm,
    .vidioc_s_parm      = vidioc_s_parm,
    .vidioc_streamon    = vidioc_streamon,
    .vidioc_streamoff   = vidioc_streamoff,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
    .vidiocgmbuf          = vidiocgmbuf,
#endif
};

#ifndef VID_TYPE_CAPTURE
#   define VID_TYPE_CAPTURE 1
#endif

static struct video_device droidcam_vid = {
    .name       = "droidcam",
    .vfl_type   = VID_TYPE_CAPTURE,
    //.hardware = 0,
    .fops       = &droidcam_fops,
    .minor      = -1,
    .release    = video_device_release_empty,
    .tvnorms        = V4L2_STD_NTSC_M,
    .current_norm   = V4L2_STD_NTSC_M,
    .ioctl_ops  = &droidcam_ioctl_ops,
};

// == Init / Exit ==
static int __init droidcam_init(void)
{
    int ret = 0;

    yuv_frame_len = width * height * 2;
    buffer_len     = ((rgb_frame_len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));

    formats[0].width        = width;
    formats[0].height       = height;
    formats[0].bytesperline = width * 3;
    formats[0].sizeimage    = rgb_frame_len;

    formats[1].width        = width;
    formats[1].height       = height;
    formats[1].bytesperline = width * 2;
    formats[1].sizeimage    = yuv_frame_len;

    frame_data =  (char*) vmalloc(buffer_len); // allocate some memory
    if(frame_data)
    {
        memset(frame_data, 200, buffer_len);
        frame_sequence = last_read_frame = 0;
        ret = video_register_device(&droidcam_vid, VFL_TYPE_GRABBER, -1); // register video capture device
    }
    else
    {
        ret = -ENOMEM;
    }

    DEBUG_MSG("(%s) droidcam_init status: %d\n", current->comm, ret);
    return ret;
}

static void __exit droidcam_exit(void)
{
    DEBUG_MSG("(%s) %s called\n", current->comm, __FUNCTION__);
    frame_sequence = 0;
    vfree(frame_data);
    video_unregister_device(&droidcam_vid);
}
//  == Module Stuff ==
module_init(droidcam_init);
module_exit(droidcam_exit);

module_param(width,  int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
module_param(height, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

MODULE_DESCRIPTION("Android Webcam");
MODULE_AUTHOR("DEV47APPS");
MODULE_LICENSE("Apache");
