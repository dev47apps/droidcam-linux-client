/*
 * v4l2loopback-dc.c
 *
 * Copyright (C) 2005-2009 Vasily Levin (vasaka@gmail.com)
 * Copyright (C) 2010-2023 IOhannes m zmoelnig (zmoelnig@iem.at)
 * Copyright (C) 2011 Stefan Diewald (stefan.diewald@mytum.de)
 * Copyright (C) 2012 Anton Novikov (random.plant@gmail.com)
 * Copyright (C) 2013 Dev47Apps (https://github.com/dev47apps)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/time.h>
#include <linux/module.h>
#include <linux/videodev2.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>

static inline void get_timestamp(struct v4l2_buffer *b) {
  struct timespec64 ts;
  ktime_get_ts64(&ts);

  b->timestamp.tv_sec = ts.tv_sec;
  b->timestamp.tv_usec = (ts.tv_nsec / NSEC_PER_USEC);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)
#error This module is not supported on kernels before 4.0.0.
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 3, 0)
#define strscpy strlcpy
#endif

#if defined(timer_setup)
#define HAVE_TIMER_SETUP
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 7, 0)
# define VFL_TYPE_VIDEO VFL_TYPE_GRABBER
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 2, 0)
# define timer_delete_sync del_timer_sync
#endif


#define V4L2LOOPBACK_VERSION_CODE KERNEL_VERSION(0,6,4)


#define DEBUG 0

MODULE_DESCRIPTION("V4L2 loopback video device");
MODULE_AUTHOR("Vasily Levin, IOhannes m zmoelnig <zmoelnig@iem.at>, Stefan Diewald, Anton Novikov");
MODULE_LICENSE("GPL");


/* helpers */
#define STRINGIFY(s) #s
#define STRINGIFY2(s) STRINGIFY(s)

#if DEBUG

#define dprintk(fmt, args...)                                           \
  do { if (DEBUG > 0) {                                                 \
      printk(KERN_INFO "v4l2-loopback[" STRINGIFY2(__LINE__) "]: " fmt, ##args); \
    } } while(0)

#define MARK()                                                          \
  do{ if (DEBUG > 1) {                                                  \
      printk(KERN_INFO "%s:%d[%s]", __FILE__, __LINE__, __FUNCTION__);	\
    } } while (0)

#define dprintkrw(fmt, args...)                                         \
  do { if (DEBUG > 2) {                                                 \
      printk(KERN_INFO "v4l2-loopback[" STRINGIFY2(__LINE__)"]: " fmt, ##args); \
    } } while (0)

#else

#define MARK()
#define dprintk(fmt, args...)
#define dprintkrw(fmt, args...)

#endif

/* module constants */
#define MAX_TIMEOUT (100 * 1000 * 1000) /* in msecs */

#define MAX_BUFFERS 16  /* max buffers that can be mapped, actually they
                         * are all mapped to MAX_BUFFERS buffers */

/* how many times a device can be opened
 * the per-module default value can be overridden on a per-device basis using
 * the /sys/devices interface
 *
 * note that max_openers should be at least 2 in order to get a working system:
 *   one opener for the producer and one opener for the consumer
 */
#define MAX_OPENERS 8;
#define MAX_DEVICES 1
#define DEVICES     1

/* format specifications */
#define V4L2LOOPBACK_SIZE_MIN_WIDTH   48
#define V4L2LOOPBACK_SIZE_MIN_HEIGHT  32
#define V4L2LOOPBACK_SIZE_MAX_WIDTH   8192
#define V4L2LOOPBACK_SIZE_MAX_HEIGHT  8192

#define V4L2LOOPBACK_SIZE_DEFAULT_WIDTH   640
#define V4L2LOOPBACK_SIZE_DEFAULT_HEIGHT  480


#define V4L2LOOPBACK_VIDEO_NR_DEFAULT -1

/* module parameters */
static int width = V4L2LOOPBACK_SIZE_DEFAULT_WIDTH;
module_param(width, int, S_IRUGO);
MODULE_PARM_DESC(width, "frame width");

static int height = V4L2LOOPBACK_SIZE_DEFAULT_HEIGHT;
module_param(height, int, S_IRUGO);
MODULE_PARM_DESC(height, "frame height");

static int video_nr = V4L2LOOPBACK_VIDEO_NR_DEFAULT;
module_param(video_nr, int, S_IRUGO);
MODULE_PARM_DESC(video_nr, "video device numbers (-1=auto, 0=/dev/video0, etc.)");


/* control IDs */
#define CID_KEEP_FORMAT        (V4L2_CID_PRIVATE_BASE+0)
#define CID_SUSTAIN_FRAMERATE  (V4L2_CID_PRIVATE_BASE+1)
#define CID_TIMEOUT            (V4L2_CID_PRIVATE_BASE+2)
#define CID_TIMEOUT_IMAGE_IO   (V4L2_CID_PRIVATE_BASE+3)


/* module structures */
struct v4l2loopback_private {
  int devicenr;
};

typedef struct v4l2loopback_private *priv_ptr;

/* TODO(vasaka) use typenames which are common to kernel, but first find out if
 * it is needed */
/* struct keeping state and settings of loopback device */

struct v4l2l_buffer {
  struct v4l2_buffer buffer;
  struct list_head list_head;
  int use_count;
};

struct v4l2_loopback_device {
  struct v4l2_device   v4l2_dev;
  struct video_device *vdev;
  /* pixel and stream format */
  struct v4l2_pix_format pix_format;
  struct v4l2_captureparm capture_param;
  unsigned long frame_jiffies;

  /* ctrls */
  int keep_format; /* CID_KEEP_FORMAT; stay ready_for_capture even when all
                      openers close() the device */
  int sustain_framerate; /* CID_SUSTAIN_FRAMERATE; duplicate frames to maintain
                            (close to) nominal framerate */

  /* buffers stuff */
  u8 *image;         /* pointer to actual buffers data */
  unsigned long int imagesize;  /* size of buffers data */
  int buffers_number;  /* should not be big, 4 is a good choice */
  struct v4l2l_buffer buffers[MAX_BUFFERS];	/* inner driver buffers */
  int used_buffers; /* number of the actually used buffers */
  int max_openers;  /* how many times can this device be opened */

  int write_position; /* number of last written frame + 1 */
  struct list_head outbufs_list; /* buffers in output DQBUF order */
  int bufpos2index[MAX_BUFFERS]; /* mapping of (read/write_position % used_buffers)
                                  * to inner buffer index */
  long buffer_size;

  /* sustain_framerate stuff */
  struct timer_list sustain_timer;
  unsigned int reread_count;

  /* timeout stuff */
  unsigned long timeout_jiffies; /* CID_TIMEOUT; 0 means disabled */
  int timeout_image_io; /* CID_TIMEOUT_IMAGE_IO; next opener will
                         * read/write to timeout_image */
  u8 *timeout_image; /* copy of it will be captured when timeout passes */
  struct v4l2l_buffer timeout_image_buffer;
  struct timer_list timeout_timer;
  int timeout_happened;

  /* sync stuff */
  atomic_t open_count;
  int ready_for_capture;/* set to true when at least one writer opened
                         * device and negotiated format */
  wait_queue_head_t read_event;
  spinlock_t lock;
};

/* types of opener shows what opener wants to do with loopback */
enum opener_type {
  UNNEGOTIATED = 0,
  READER = 1,
  WRITER = 2,
};

/* struct keeping state and type of opener */
struct v4l2_loopback_opener {
  enum opener_type type;
  int vidioc_enum_frameintervals_calls;
  int read_position; /* number of last processed frame + 1 or
                      * write_position - 1 if reader went out of sync */
  unsigned int reread_count;
  struct v4l2_buffer *buffers;
  int buffers_number;  /* should not be big, 4 is a good choice */
  int timeout_image_io;
};

/* this is heavily inspired by the bttv driver found in the linux kernel */
struct v4l2l_format {
  char *name;
  int  fourcc;          /* video4linux 2      */
  int  depth;           /* bit/pixel          */
  int  flags;
};
/* set the v4l2l_format.flags to PLANAR for non-packed formats */
#define FORMAT_FLAGS_PLANAR       0x01

static const struct v4l2l_format formats[] = {
  /* here come the packed formats */
  {
    .name     = "32 bpp RGB, le",
    .fourcc   = V4L2_PIX_FMT_BGR32,
    .depth    = 32,
    .flags    = 0,
  },{
    .name     = "32 bpp RGB, be",
    .fourcc   = V4L2_PIX_FMT_RGB32,
    .depth    = 32,
    .flags    = 0,
  },{
    .name     = "24 bpp RGB, le",
    .fourcc   = V4L2_PIX_FMT_BGR24,
    .depth    = 24,
    .flags    = 0,
  },{
    .name     = "24 bpp RGB, be",
    .fourcc   = V4L2_PIX_FMT_RGB24,
    .depth    = 24,
    .flags    = 0,
  },{
    .name     = "4:2:2, packed, YUYV",
    .fourcc   = V4L2_PIX_FMT_YUYV,
    .depth    = 16,
    .flags    = 0,
  },{
    .name     = "4:2:2, packed, YUYV",
    .fourcc   = V4L2_PIX_FMT_YUYV,
    .depth    = 16,
    .flags    = 0,
  },{
    .name     = "4:2:2, packed, UYVY",
    .fourcc   = V4L2_PIX_FMT_UYVY,
    .depth    = 16,
    .flags    = 0,
  },{
#ifdef V4L2_PIX_FMT_YVYU
    .name     = "4:2:2, packed YVYU",
    .fourcc   = V4L2_PIX_FMT_YVYU,
    .depth    = 16,
    .flags=0,
  },{
#endif
#ifdef V4L2_PIX_FMT_VYUY
    .name     = "4:2:2, packed VYUY",
    .fourcc   = V4L2_PIX_FMT_VYUY,
    .depth    = 16,
    .flags=0,
  },{
#endif
    .name     = "4:2:2, packed YYUV",
    .fourcc   = V4L2_PIX_FMT_YYUV,
    .depth    = 16,
    .flags=0,
  },{
    .name     = "YUV-8-8-8-8",
    .fourcc   = V4L2_PIX_FMT_YUV32,
    .depth    = 32,
    .flags    = 0,
  },{
    .name     = "8 bpp, gray",
    .fourcc   = V4L2_PIX_FMT_GREY,
    .depth    = 8,
    .flags    = 0,
  },{
    .name     = "16 Greyscale",
    .fourcc   = V4L2_PIX_FMT_Y16,
    .depth    = 16,
    .flags    = 0,
  },

  /* here come the planar formats */
  {
    .name     = "4:1:0, planar, Y-Cr-Cb",
    .fourcc   = V4L2_PIX_FMT_YVU410,
    .depth    = 9,
    .flags    = FORMAT_FLAGS_PLANAR,
  },{
    .name     = "4:2:0, planar, Y-Cr-Cb",
    .fourcc   = V4L2_PIX_FMT_YVU420,
    .depth    = 12,
    .flags    = FORMAT_FLAGS_PLANAR,
  },{
    .name     = "4:1:0, planar, Y-Cb-Cr",
    .fourcc   = V4L2_PIX_FMT_YUV410,
    .depth    = 9,
    .flags    = FORMAT_FLAGS_PLANAR,
  },{
    .name     = "4:2:0, planar, Y-Cb-Cr",
    .fourcc   = V4L2_PIX_FMT_YUV420,
    .depth    = 12,
    .flags    = FORMAT_FLAGS_PLANAR,
  }
};
static const unsigned int FORMATS = ARRAY_SIZE(formats);


static char*
fourcc2str          (unsigned int fourcc,
                     char buf[4])
{
  buf[0]=(fourcc>> 0) & 0xFF;
  buf[1]=(fourcc>> 8) & 0xFF;
  buf[2]=(fourcc>>16) & 0xFF;
  buf[3]=(fourcc>>24) & 0xFF;

  return buf;
}

static const struct v4l2l_format*
format_by_fourcc    (int fourcc)
{
  unsigned int i;

  for (i = 0; i < FORMATS; i++) {
    if (formats[i].fourcc == fourcc)
      return formats+i;  }

  dprintk("unsupported format '%c%c%c%c'",
          (fourcc>> 0) & 0xFF,
          (fourcc>> 8) & 0xFF,
          (fourcc>>16) & 0xFF,
          (fourcc>>24) & 0xFF);
  return NULL;
}

static void
pix_format_set_size     (struct v4l2_pix_format *       f,
                         const struct v4l2l_format *    fmt,
                         unsigned int                   width,
                         unsigned int                   height)
{
  f->width = width;
  f->height = height;

  if (fmt->flags & FORMAT_FLAGS_PLANAR) {
    f->bytesperline = width; /* Y plane */
    f->sizeimage = (width * height * fmt->depth) >> 3;
  } else {
    f->bytesperline = (width * fmt->depth) >> 3;
    f->sizeimage = height * f->bytesperline;
  }
}

static void
set_timeperframe(struct v4l2_loopback_device *dev, struct v4l2_fract *tpf)
{
  dev->capture_param.timeperframe = *tpf;
  dev->frame_jiffies = max(1UL, msecs_to_jiffies(1000) * tpf->numerator / tpf->denominator);
}

static struct v4l2_loopback_device*v4l2loopback_cd2dev  (struct device*cd);

/* device attributes */
/* available via sysfs: /sys/devices/virtual/video4linux/video* */

static ssize_t attr_show_format(struct device *cd,
                                struct device_attribute *attr,
                                char *buf)
{
  /* gets the current format as "FOURCC:WxH@f/s", e.g. "YUYV:320x240@1000/30" */
  struct v4l2_loopback_device *dev = v4l2loopback_cd2dev(cd);
  const struct v4l2_fract *tpf;
  char buf4cc[5], buf_fps[32];

  if (!dev || !dev->ready_for_capture)
    return 0;
  tpf = &dev->capture_param.timeperframe;

  fourcc2str(dev->pix_format.pixelformat, buf4cc);
  if (tpf->numerator == 1)
    snprintf(buf_fps, sizeof(buf_fps), "%d", tpf->denominator);
  else
    snprintf(buf_fps, sizeof(buf_fps), "%d/%d", tpf->denominator, tpf->numerator);

  return sprintf(buf, "%4s:%dx%d@%s\n",
                   buf4cc, dev->pix_format.width, dev->pix_format.height, buf_fps);
}
static ssize_t attr_store_format(struct device* cd,
                                 struct device_attribute *attr,
                                 const char* buf, size_t len)
{
  struct v4l2_loopback_device *dev = v4l2loopback_cd2dev(cd);
  int fps_num = 0, fps_den = 1;

  /* only fps changing is supported */
  if (sscanf(buf, "@%d/%d", &fps_num, &fps_den) > 0) {
    if (fps_num < 1 || fps_den < 1)
      return -EINVAL;
    set_timeperframe(dev, &(struct v4l2_fract){.numerator   = fps_den,
                                               .denominator = fps_num});
    return len;
  } else {
    return -EINVAL;
  }
}
static DEVICE_ATTR(format, S_IRUGO | S_IWUSR, attr_show_format, attr_store_format);

static ssize_t attr_show_buffers(struct device *cd,
                                 struct device_attribute *attr,
                                 char *buf)
{
  struct v4l2_loopback_device *dev = v4l2loopback_cd2dev(cd);
  return sprintf(buf, "%d\n", dev->used_buffers);
}
static DEVICE_ATTR(buffers, S_IRUGO, attr_show_buffers, NULL);

static ssize_t attr_show_maxopeners(struct device *cd,
                                    struct device_attribute *attr,
                                    char *buf)
{
  struct v4l2_loopback_device *dev = v4l2loopback_cd2dev(cd);
  return sprintf(buf, "%d\n", dev->max_openers);
}
static ssize_t attr_store_maxopeners(struct device* cd,
                                     struct device_attribute *attr,
                                     const char* buf, size_t len)
{
  struct v4l2_loopback_device *dev = NULL;
  unsigned long curr=0;

  if (kstrtoul(buf, 0, &curr))
    return -EINVAL;

  dev = v4l2loopback_cd2dev(cd);

  if (dev->max_openers == curr)
    return len;

  if (dev->open_count.counter > curr) {
    /* request to limit to less openers as are currently attached to us */
    return -EINVAL;
  }

  dev->max_openers = (int)curr;

  return len;
}


static DEVICE_ATTR(max_openers, S_IRUGO | S_IWUSR, attr_show_maxopeners, attr_store_maxopeners);





static void v4l2loopback_remove_sysfs(struct video_device *vdev)
{
#define V4L2_SYSFS_DESTROY(x) device_remove_file(&vdev->dev, &dev_attr_##x)

  if (vdev) {
    V4L2_SYSFS_DESTROY(format);
    V4L2_SYSFS_DESTROY(buffers);
    V4L2_SYSFS_DESTROY(max_openers);
    /* ... */
  }
}
static void v4l2loopback_create_sysfs(struct video_device *vdev)
{
  int res=0;
#define V4L2_SYSFS_CREATE(x)     res = device_create_file(&vdev->dev, &dev_attr_##x); if (res < 0) break
  if (!vdev) return;
  do {
    V4L2_SYSFS_CREATE(format);
    V4L2_SYSFS_CREATE(buffers);
    V4L2_SYSFS_CREATE(max_openers);
    /* ... */
  } while(0);

  if (res >= 0)return;
  dev_err(&vdev->dev, "%s error: %d\n", __func__, res);
}






/* global module data */
struct v4l2_loopback_device *devs[MAX_DEVICES];

static struct v4l2_loopback_device*
v4l2loopback_cd2dev  (struct device*cd)
{
  struct video_device *loopdev = to_video_device(cd);
  priv_ptr ptr = (priv_ptr)video_get_drvdata(loopdev);
  int nr = ptr->devicenr;
  if(nr<0 || nr>=DEVICES){printk(KERN_ERR "v4l2-loopback: illegal device %d\n",nr);return NULL;}
  return devs[nr];
}

static struct v4l2_loopback_device*
v4l2loopback_getdevice        (struct file*f)
{
  struct video_device *loopdev = video_devdata(f);
  priv_ptr ptr = (priv_ptr)video_get_drvdata(loopdev);
  int nr = ptr->devicenr;
  if(nr<0 || nr>=DEVICES){printk(KERN_ERR "v4l2-loopback: illegal device %d\n",nr);return NULL;}
  return devs[nr];
}

static struct v4l2_loopback_device*
v4l2loopback_getdevice_internal (int nr)
{
  if(nr<0 || nr>=DEVICES){printk(KERN_ERR "v4l2-loopback: illegal device %d\n",nr);return NULL;}
  return devs[nr];
}

/* forward declarations */
static void init_buffers(struct v4l2_loopback_device *dev);
static int allocate_buffers(struct v4l2_loopback_device *dev);
static int free_buffers(struct v4l2_loopback_device *dev);
static void try_free_buffers(struct v4l2_loopback_device *dev);
static int allocate_timeout_image(struct v4l2_loopback_device *dev);
static void check_timers(struct v4l2_loopback_device *dev);
static const struct v4l2_file_operations v4l2_loopback_fops;
static const struct v4l2_ioctl_ops v4l2_loopback_ioctl_ops;

/* Queue helpers */
/* next functions sets buffer flags and adjusts counters accordingly */
static inline void
set_done            (struct v4l2l_buffer *buffer)
{
  buffer->buffer.flags &= ~V4L2_BUF_FLAG_QUEUED;
  buffer->buffer.flags |= V4L2_BUF_FLAG_DONE;
}

static inline void
set_queued          (struct v4l2l_buffer *buffer)
{
  buffer->buffer.flags &= ~V4L2_BUF_FLAG_DONE;
  buffer->buffer.flags |= V4L2_BUF_FLAG_QUEUED;
}

static inline void
unset_flags         (struct v4l2l_buffer *buffer)
{
  buffer->buffer.flags &= ~V4L2_BUF_FLAG_QUEUED;
  buffer->buffer.flags &= ~V4L2_BUF_FLAG_DONE;
}
/* V4L2 ioctl caps and params calls */
/* returns device capabilities
 * called on VIDIOC_QUERYCAP
 */
static int
vidioc_querycap     (struct file *file,
                     void *priv,
                     struct v4l2_capability *cap)
{
  struct v4l2_loopback_device *dev = v4l2loopback_getdevice(file);
  int devnr = ((struct v4l2loopback_private *)video_get_drvdata(dev->vdev))->devicenr;

  strscpy(cap->driver, "Droidcam", sizeof(cap->driver));
  strscpy(cap->card  , "Droidcam", sizeof(cap->card));
  snprintf(cap->bus_info, sizeof(cap->bus_info), "platform:v4l2loopback_dc-%03d", devnr);

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 1, 0)
  /* since 3.1.0, the v4l2-core system is supposed to set the version */
  cap->version = V4L2LOOPBACK_VERSION_CODE;
#endif

  cap->capabilities =
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
    V4L2_CAP_DEVICE_CAPS |
#endif
    V4L2_CAP_VIDEO_CAPTURE |
  /*V4L2_CAP_VIDEO_OUTPUT |*/
    V4L2_CAP_STREAMING |
    V4L2_CAP_READWRITE;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0)
	cap->device_caps = (cap->capabilities & ~V4L2_CAP_DEVICE_CAPS);
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
	cap->device_caps = cap->capabilities;
	cap->capabilities |= V4L2_CAP_DEVICE_CAPS;
#endif

  memset(cap->reserved, 0, sizeof(cap->reserved));

  return 0;
}

static int
vidioc_enum_framesizes        (struct file *file, void *fh,
                               struct v4l2_frmsizeenum *argp)
{
  struct v4l2_loopback_device *dev;

  /* LATER: what does the index really  mean?
   * if it's about enumerating formats, we can safely ignore it
   * (CHECK)
   */

  /* there can be only one... */
  if (argp->index)
    return -EINVAL;


  dev=v4l2loopback_getdevice(file);
  if (dev->ready_for_capture) {
    /* format has already been negotiated
     * cannot change during runtime
     */
    argp->type=V4L2_FRMSIZE_TYPE_DISCRETE;

    argp->discrete.width=dev->pix_format.width;
    argp->discrete.height=dev->pix_format.height;
  } else {
    /* if the format has not been negotiated yet, we accept anything
     */
    argp->type=V4L2_FRMSIZE_TYPE_CONTINUOUS;

    argp->stepwise.min_width=V4L2LOOPBACK_SIZE_MIN_WIDTH;
    argp->stepwise.min_height=V4L2LOOPBACK_SIZE_MIN_HEIGHT;

    argp->stepwise.max_width=V4L2LOOPBACK_SIZE_MAX_WIDTH;
    argp->stepwise.max_height=V4L2LOOPBACK_SIZE_MAX_HEIGHT;

    argp->stepwise.step_width=1;
    argp->stepwise.step_height=1;
  }
  return 0;
}

/* returns frameinterval (fps) for the set resolution
 * called on VIDIOC_ENUM_FRAMEINTERVALS
 */
static int
vidioc_enum_frameintervals(struct file *file,
			   void *fh,
			   struct v4l2_frmivalenum *argp)
{
  struct v4l2_loopback_device *dev = v4l2loopback_getdevice(file);
  struct v4l2_loopback_opener *opener = file->private_data;

  if (dev->ready_for_capture) {
    if (opener->vidioc_enum_frameintervals_calls > 0)
      return -EINVAL;
    if (argp->width == dev->pix_format.width &&
        argp->height== dev->pix_format.height)
      {
        argp->type = V4L2_FRMIVAL_TYPE_DISCRETE;
        argp->discrete = dev->capture_param.timeperframe;
        opener->vidioc_enum_frameintervals_calls++;
        return 0;
      } else {
      return -EINVAL;
    }
  }
  return 0;
}

/* ------------------ CAPTURE ----------------------- */

/* returns device formats
 * called on VIDIOC_ENUM_FMT, with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_CAPTURE
 */
static int
vidioc_enum_fmt_cap (struct file *file,
                     void *fh,
                     struct v4l2_fmtdesc *f)
{
  struct v4l2_loopback_device *dev;
  MARK();

  dev=v4l2loopback_getdevice(file);

  if (f->index)
    return -EINVAL;
  if (dev->ready_for_capture) {
    const __u32 format = dev->pix_format.pixelformat;
    //  strscpy(f->description, "current format", sizeof(f->description));

    snprintf(f->description, sizeof(f->description),
             "[%c%c%c%c]",
             (format>> 0) & 0xFF,
             (format>> 8) & 0xFF,
             (format>>16) & 0xFF,
             (format>>24) & 0xFF);

    f->pixelformat = dev->pix_format.pixelformat;
  } else {
    return -EINVAL;
  }
  f->flags=0;
  MARK();
  return 0;
}

/* returns current video format format fmt
 * called on VIDIOC_G_FMT, with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_CAPTURE
 */
static int
vidioc_g_fmt_cap    (struct file *file,
                     void *priv,
                     struct v4l2_format *fmt)
{
  struct v4l2_loopback_device *dev;
  MARK();

  dev=v4l2loopback_getdevice(file);

  if (!dev->ready_for_capture) {
    return -EINVAL;
  }

  fmt->fmt.pix = dev->pix_format;
  MARK();
  return 0;
}

/* checks if it is OK to change to format fmt;
 * actual check is done by inner_try_fmt_cap
 * just checking that pixelformat is OK and set other parameters, app should
 * obey this decision
 * called on VIDIOC_TRY_FMT, with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_CAPTURE
 */
static int
vidioc_try_fmt_cap  (struct file *file,
                     void *priv,
                     struct v4l2_format *fmt)
{
  struct v4l2_loopback_device *dev;

  dev=v4l2loopback_getdevice(file);

  if (0 == dev->ready_for_capture) {
    dprintk("setting fmt_cap not possible yet\n");
    return -EBUSY;
  }

  if (fmt->fmt.pix.pixelformat != dev->pix_format.pixelformat)
    return -EINVAL;

  fmt->fmt.pix = dev->pix_format;

  do { char buf[5]; buf[4]=0; dprintk("capFOURCC=%s\n", fourcc2str(dev->pix_format.pixelformat, buf)); } while(0);
  return 0;
}

/* sets new output format, if possible
 * actually format is set  by input and we even do not check it, just return
 * current one, but it is possible to set subregions of input TODO(vasaka)
 * called on VIDIOC_S_FMT, with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_CAPTURE
 */
static int
vidioc_s_fmt_cap    (struct file *file,
                     void *priv,
                     struct v4l2_format *fmt)
{
  return vidioc_try_fmt_cap(file, priv, fmt);
}


/* ------------------ OUTPUT ----------------------- */

/* returns device formats;
 * LATER: allow all formats
 * called on VIDIOC_ENUM_FMT, with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_OUTPUT
 */
static int
vidioc_enum_fmt_out (struct file *file,
                     void *fh,
                     struct v4l2_fmtdesc *f)
{
  struct v4l2_loopback_device *dev;
  const struct v4l2l_format *  fmt;

  dev=v4l2loopback_getdevice(file);

  if (dev->ready_for_capture) {
    const __u32 format = dev->pix_format.pixelformat;

    /* format has been fixed by the writer, so only one single format is supported */
    if (f->index)
      return -EINVAL;

    fmt=format_by_fourcc(format);
    if(NULL == fmt)
      return -EINVAL;

    f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    //    f->flags = ??;
    snprintf(f->description, sizeof(f->description),
             fmt->name);

    f->pixelformat = dev->pix_format.pixelformat;
  } else {
    __u32 format;
    /* fill in a dummy format */
    if(f->index < 0 ||
       f->index >= FORMATS)
      return -EINVAL;

    fmt=&formats[f->index];

    f->pixelformat=fmt->fourcc;
    format = f->pixelformat;

    //    strscpy(f->description, "dummy OUT format", sizeof(f->description));
    snprintf(f->description, sizeof(f->description),
             fmt->name);

  }
  f->flags=0;

  return 0;
}

/* returns current video format format fmt */
/* NOTE: this is called from the producer
 * so if format has not been negotiated yet,
 * it should return ALL of available formats,
 * called on VIDIOC_G_FMT, with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_OUTPUT
 */
static int
vidioc_g_fmt_out    (struct file *file,
                     void *priv,
                     struct v4l2_format *fmt)
{
  struct v4l2_loopback_device *dev;

  MARK();

 if (file != NULL) {
  dev=v4l2loopback_getdevice(file);
 }
 else {
        dev = v4l2loopback_getdevice_internal(0);
 }

  /*
   * LATER: this should return the currently valid format
   * gstreamer doesn't like it, if this returns -EINVAL, as it
   * then concludes that there is _no_ valid format
   * CHECK whether this assumption is wrong,
   * or whether we have to always provide a valid format
   */

  fmt->fmt.pix = dev->pix_format;
  return 0;
}

/* checks if it is OK to change to format fmt;
 * if format is negotiated do not change it
 * called on VIDIOC_TRY_FMT with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_OUTPUT
 */
static int
vidioc_try_fmt_out  (struct file *file,
                     void *priv,
                     struct v4l2_format *fmt)
{
  struct v4l2_loopback_device *dev;
  MARK();

    if (file != NULL) {
        dev=v4l2loopback_getdevice(file);
    }
    else {
        dev = v4l2loopback_getdevice_internal(0);
    }

  /* TODO(vasaka) loopback does not care about formats writer want to set,
   * maybe it is a good idea to restrict format somehow */
  if (dev->ready_for_capture) {
    fmt->fmt.pix = dev->pix_format;
  } else {
    __u32 w=fmt->fmt.pix.width;
    __u32 h=fmt->fmt.pix.height;
    __u32 pixfmt=fmt->fmt.pix.pixelformat;
    const struct v4l2l_format*format=format_by_fourcc(pixfmt);

    if(w > V4L2LOOPBACK_SIZE_MAX_WIDTH)   w=V4L2LOOPBACK_SIZE_MAX_WIDTH;
    if(h > V4L2LOOPBACK_SIZE_MAX_HEIGHT)  h=V4L2LOOPBACK_SIZE_MAX_HEIGHT;

    dprintk("trying image %dx%d", w, h);

    if(w<1) w=V4L2LOOPBACK_SIZE_DEFAULT_WIDTH;
    if(h<1) h=V4L2LOOPBACK_SIZE_DEFAULT_HEIGHT;

    if(NULL==format) {
      format=&formats[0];
    }

    pix_format_set_size(&fmt->fmt.pix, format, w, h);

    fmt->fmt.pix.pixelformat = format->fourcc;
    fmt->fmt.pix.colorspace=V4L2_COLORSPACE_SRGB;

    if(V4L2_FIELD_ANY == fmt->fmt.pix.field)
      fmt->fmt.pix.field=V4L2_FIELD_NONE;

    /* FIXXME: try_fmt should never modify the device-state */
    dev->pix_format = fmt->fmt.pix;
  }
  return 0;
}

/* sets new output format, if possible;
 * allocate data here because we do not know if it will be streaming or
 * read/write IO
 * called on VIDIOC_S_FMT with v4l2_buf_type set to V4L2_BUF_TYPE_VIDEO_OUTPUT
 */
static int
vidioc_s_fmt_out    (struct file *file,
                     void *priv, struct v4l2_format *fmt)
{
  struct v4l2_loopback_device *dev;
  int ret;
  MARK();

    if (file != NULL)
        dev=v4l2loopback_getdevice(file);
    else
        dev=v4l2loopback_getdevice_internal(0);

  ret = vidioc_try_fmt_out(file, priv, fmt);

  dprintk("s_fmt_out(%d) %d...%d", ret, dev->ready_for_capture, dev->pix_format.sizeimage);

  do {
    char buf[5];
    buf[4]=0;
    dprintk("outFOURCC=%s\n", fourcc2str(dev->pix_format.pixelformat, buf));
  } while(0);

  if (ret < 0)
    return ret;

  if (!dev->ready_for_capture) {
    dev->buffer_size = PAGE_ALIGN(dev->pix_format.sizeimage);
    fmt->fmt.pix.sizeimage = dev->buffer_size;
    allocate_buffers(dev);
  }
  return ret;
}

//#define V4L2L_OVERLAY
#ifdef V4L2L_OVERLAY
/* ------------------ OVERLAY ----------------------- */
/* currently unsupported */
/* GSTreamer's v4l2sink is buggy, as it requires the overlay to work
 * while it should only require it, if overlay is requested
 * once the gstreamer element is fixed, remove the overlay dummies
 */
#warning OVERLAY dummies
static int
vidioc_g_fmt_overlay(struct file *file,
                     void *priv,
                     struct v4l2_format *fmt)
{
  return 0;
}
static int
vidioc_s_fmt_overlay(struct file *file,
                     void *priv,
                     struct v4l2_format *fmt)
{
  return 0;
}
#endif /* V4L2L_OVERLAY */


/* ------------------ PARAMs ----------------------- */

/* get some data flow parameters, only capability, fps and readbuffers has
 * effect on this driver
 * called on VIDIOC_G_PARM
 */
static int
vidioc_g_parm       (struct file *file,
                     void *priv,
                     struct v4l2_streamparm *parm)
{
  /* do not care about type of opener, hope this enums would always be
   * compatible */
  struct v4l2_loopback_device *dev;
  MARK();

  dev=v4l2loopback_getdevice(file);
  parm->parm.capture = dev->capture_param;
  return 0;
}

/* get some data flow parameters, only capability, fps and readbuffers has
 * effect on this driver
 * called on VIDIOC_S_PARM
 */
static int
vidioc_s_parm       (struct file *file,
                     void *priv,
                     struct v4l2_streamparm *parm)
{
  struct v4l2_loopback_device *dev;
  MARK();

  dev=v4l2loopback_getdevice(file);
  dprintk("vidioc_s_parm called frate=%d/%d\n",
          parm->parm.capture.timeperframe.numerator,
          parm->parm.capture.timeperframe.denominator);

  switch (parm->type) {
  case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    set_timeperframe(dev, &parm->parm.capture.timeperframe);
    break;
  case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    set_timeperframe(dev, &parm->parm.capture.timeperframe);
    break;
  default:
    return -1;
  }

  parm->parm.capture = dev->capture_param;
  return 0;
}

#ifdef V4L2LOOPBACK_WITH_STD
/* sets a tv standard, actually we do not need to handle this any special way
 * added to support effecttv
 * called on VIDIOC_S_STD
 */
static int
vidioc_s_std        (struct file *file,
                     void *private_data,
                     v4l2_std_id *_std)
{
  v4l2_std_id req_std=0, supported_std=0;
  const v4l2_std_id all_std=V4L2_STD_ALL, no_std=0;

  if(_std) {
    req_std=*_std;
    *_std=all_std;
  }

  /* we support everything in V4L2_STD_ALL, but not more... */
  supported_std=(all_std & req_std);
  if(no_std == supported_std) {
    return -EINVAL;
  }

  return 0;
}


/* gets a fake video standard
 * called on VIDIOC_G_STD
 */
static int
vidioc_g_std        (struct file *file,
                     void *private_data,
                     v4l2_std_id *norm)
{
  if(norm)
    *norm=V4L2_STD_ALL;
  return 0;
}
/* gets a fake video standard
 * called on VIDIOC_QUERYSTD
 */
static int
vidioc_querystd     (struct file *file,
                     void *private_data,
                     v4l2_std_id *norm)
{
  if(norm)
    *norm=V4L2_STD_ALL;
  return 0;
}
#endif /* V4L2LOOPBACK_WITH_STD */


/* get ctrls info
 * called on VIDIOC_QUERY_EXT_CTRL
 */
static int
vidioc_query_ext_ctrl(struct file *file, void *fh,
                 struct v4l2_query_ext_ctrl *q)
{
  switch (q->id) {
  case CID_KEEP_FORMAT:
  case CID_SUSTAIN_FRAMERATE:
  case CID_TIMEOUT_IMAGE_IO:
    q->type = V4L2_CTRL_TYPE_BOOLEAN;
    q->minimum = 0;
    q->maximum = 1;
    q->step = 1;
    break;
  case CID_TIMEOUT:
    q->type = V4L2_CTRL_TYPE_INTEGER;
    q->minimum = 0;
    q->maximum = MAX_TIMEOUT;
    q->step = 1;
    break;
  default:
    return -EINVAL;
  }

  q->default_value = 0;
  q->nr_of_dims = 0;
  q->elems = 1;
  q->elem_size = 4;

  switch (q->id) {
  case CID_KEEP_FORMAT:
    strcpy(q->name, "keep_format");
    break;
  case CID_SUSTAIN_FRAMERATE:
    strcpy(q->name, "sustain_framerate");
    break;
  case CID_TIMEOUT:
    strcpy(q->name, "timeout");
    break;
  case CID_TIMEOUT_IMAGE_IO:
    strcpy(q->name, "timeout_image_io");
    break;
  default:
    BUG();
  }

  memset(q->reserved, 0, sizeof(q->reserved));
  return 0;
}


static int
vidioc_g_ctrl(struct file *file, void *fh,
              struct v4l2_control *c)
{
  struct v4l2_loopback_device *dev = v4l2loopback_getdevice(file);

  switch (c->id) {
  case CID_KEEP_FORMAT:
    c->value = dev->keep_format;
    break;
  case CID_SUSTAIN_FRAMERATE:
    c->value = dev->sustain_framerate;
    break;
  case CID_TIMEOUT:
    c->value = jiffies_to_msecs(dev->timeout_jiffies);
    break;
  case CID_TIMEOUT_IMAGE_IO:
    c->value = dev->timeout_image_io;
    break;
  default:
    return -EINVAL;
  }

  return 0;
}


static int
vidioc_s_ctrl(struct file *file, void *fh,
              struct v4l2_control *c)
{
  struct v4l2_loopback_device *dev = v4l2loopback_getdevice(file);

  switch (c->id) {
  case CID_KEEP_FORMAT:
    if (c->value < 0 || c->value > 1)
      return -EINVAL;
    dev->keep_format = c->value;
    try_free_buffers(dev);
    break;
  case CID_SUSTAIN_FRAMERATE:
    if (c->value < 0 || c->value > 1)
      return -EINVAL;
    spin_lock_bh(&dev->lock);
    dev->sustain_framerate = c->value;
    check_timers(dev);
    spin_unlock_bh(&dev->lock);
    break;
  case CID_TIMEOUT:
    if (c->value < 0 || c->value > MAX_TIMEOUT)
      return -EINVAL;
    spin_lock_bh(&dev->lock);
    dev->timeout_jiffies = msecs_to_jiffies(c->value);
    check_timers(dev);
    spin_unlock_bh(&dev->lock);
    allocate_timeout_image(dev);
    break;
  case CID_TIMEOUT_IMAGE_IO:
    if (c->value < 0 || c->value > 1)
      return -EINVAL;
    dev->timeout_image_io = c->value;
    break;
  default:
    return -EINVAL;
  }

  return 0;
}

static int
vidioc_g_ext_ctrls(struct file *file, void *fh,
                   struct v4l2_ext_controls *ctls)
{
  struct v4l2_control ctrl;
  int i, ret = 0;

  for (i = 0; i < ctls->count; i++) {
    ctrl.id = ctls->controls[i].id;
    ctrl.value = ctls->controls[i].value;
    ret = vidioc_g_ctrl(file, fh, &ctrl);
    ctls->controls[i].value = ctrl.value;
    if (ret) {
      ctls->error_idx = i;
      break;
    }
  }
  return ret;
}

static int
vidioc_s_ext_ctrls(struct file *file, void *fh,
                   struct v4l2_ext_controls *ctls)
{
  struct v4l2_control ctrl;
  int i, ret = 0;

  for (i = 0; i < ctls->count; i++) {
    ctrl.id = ctls->controls[i].id;
    ctrl.value = ctls->controls[i].value;
    ret = vidioc_s_ctrl(file, fh, &ctrl);
    ctls->controls[i].value = ctrl.value;
    if (ret) {
      ctls->error_idx = i;
      break;
    }
  }
  return ret;
}

/* returns set of device outputs, in our case there is only one
 * called on VIDIOC_ENUMOUTPUT
 */
static int
vidioc_enum_output  (struct file *file,
                     void *fh,
                     struct v4l2_output *outp)
{
  __u32 index=outp->index;
  MARK();

  if (0!=index) {
    return -EINVAL;
  }

  /* clear all data (including the reserved fields) */
  memset(outp, 0, sizeof(*outp));

  outp->index = index;
  strscpy(outp->name, "loopback in", sizeof(outp->name));
  outp->type = V4L2_OUTPUT_TYPE_ANALOG;
  outp->audioset = 0;
  outp->modulator = 0;
  outp->std = V4L2_STD_ALL;

#ifdef V4L2_OUT_CAP_STD
  outp->capabilities |= V4L2_OUT_CAP_STD;
#endif

  return 0;
}

/* which output is currently active,
 * called on VIDIOC_G_OUTPUT
 */
static int
vidioc_g_output     (struct file *file,
                    void *fh,
                    unsigned int *i)
{
  if(i)
    *i = 0;
  return 0;
}

/* set output, can make sense if we have more than one video src,
 * called on VIDIOC_S_OUTPUT
 */
static int
vidioc_s_output      (struct file *file,
                     void *fh,
                     unsigned int i)
{
  if(i)
    return -EINVAL;
  i=0;

  if (v4l2loopback_getdevice(file)->ready_for_capture) {
    return -EBUSY;
  }

  return 0;
}


/* returns set of device inputs, in our case there is only one,
 * but later I may add more
 * called on VIDIOC_ENUMINPUT
 */
static int
vidioc_enum_input   (struct file *file,
                     void *fh,
                     struct v4l2_input *inp)
{
  __u32 index=inp->index;
  MARK();

  if (0!=index) {
    return -EINVAL;
  }

  if (!v4l2loopback_getdevice(file)->ready_for_capture)
    return -EINVAL;

  /* clear all data (including the reserved fields) */
  memset(inp, 0, sizeof(*inp));

  inp->index = index;
  strscpy(inp->name, "loopback", sizeof(inp->name));
  inp->type = V4L2_INPUT_TYPE_CAMERA;
  inp->audioset = 0;
  inp->tuner = 0;
  inp->std = V4L2_STD_ALL;
  inp->status = 0;


#ifdef V4L2_IN_CAP_STD
//inp->capabilities |= V4L2_IN_CAP_STD;
#endif
  return 0;
}

/* which input is currently active,
 * called on VIDIOC_G_INPUT
 */
static int
vidioc_g_input     (struct file *file,
                    void *fh,
                    unsigned int *i)
{
 if (!v4l2loopback_getdevice(file)->ready_for_capture)
   return -EINVAL;
  if(i)
    *i = 0;
  return 0;
}

/* set input, can make sense if we have more than one video src,
 * called on VIDIOC_S_INPUT
 */
static int
vidioc_s_input      (struct file *file,
                     void *fh,
                     unsigned int i)
{
  if ((i==0) && (v4l2loopback_getdevice(file)->ready_for_capture))
    return 0;

  return -EINVAL;
}

/* --------------- V4L2 ioctl buffer related calls ----------------- */

/* negotiate buffer type
 * only mmap streaming supported
 * called on VIDIOC_REQBUFS
 */
static int
vidioc_reqbufs      (struct file *file,
                     void *fh,
                     struct v4l2_requestbuffers *b)
{
  struct v4l2_loopback_device *dev;
  struct v4l2_loopback_opener *opener;
  int i;
  MARK();

  dev=v4l2loopback_getdevice(file);
  opener = file->private_data;

  dprintk("reqbufs: %d\t%d=%d", b->memory, b->count, dev->buffers_number);
  if (opener->timeout_image_io) {
    if (b->memory != V4L2_MEMORY_MMAP)
      return -EINVAL;
    b->count = 1;
    return 0;
  }

  init_buffers(dev);
  switch (b->memory) {
  case V4L2_MEMORY_MMAP:
    /* do nothing here, buffers are always allocated*/
    if (0 == b->count)
      return 0;

    if (b->count > dev->buffers_number)
      b->count = dev->buffers_number;

    /* make sure that outbufs_list contains buffers from 0 to used_buffers-1
     * actually, it will have been already populated via v4l2_loopback_init()
     * at this point */
    if (list_empty(&dev->outbufs_list)) {
      for (i = 0; i < dev->used_buffers; ++i)
        list_add_tail(&dev->buffers[i].list_head, &dev->outbufs_list);
    }

    /* also, if dev->used_buffers is going to be decreased, we should remove
     * out-of-range buffers from outbufs_list, and fix bufpos2index mapping */
    if (b->count < dev->used_buffers) {
      struct v4l2l_buffer *pos, *n;
      list_for_each_entry_safe(pos, n, &dev->outbufs_list, list_head) {
        if (pos->buffer.index >= b->count)
          list_del(&pos->list_head);
      }

      /* after we update dev->used_buffers, buffers in outbufs_list will
       * correspond to dev->write_position + [0;b->count-1] range */
      i = dev->write_position;
      list_for_each_entry(pos, &dev->outbufs_list, list_head) {
        dev->bufpos2index[i % b->count] = pos->buffer.index;
        ++i;
      }
    }

    opener->buffers_number = b->count;
    if (opener->buffers_number < dev->used_buffers)
      dev->used_buffers = opener->buffers_number;
    return 0;
  default:
    return -EINVAL;
  }
}

/* returns buffer asked for;
 * give app as many buffers as it wants, if it less than MAX,
 * but map them in our inner buffers
 * called on VIDIOC_QUERYBUF
 */
static int
vidioc_querybuf     (struct file *file,
                     void *fh,
                     struct v4l2_buffer *b)
{
  enum v4l2_buf_type type ;
  int index;
  struct v4l2_loopback_device *dev;
  struct v4l2_loopback_opener *opener;
  MARK();

  type = b->type;
  index = b->index;
  dev=v4l2loopback_getdevice(file);
  opener = file->private_data;

  if ((b->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) &&
      (b->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)) {
    return -EINVAL;
  }
  if (b->index > MAX_BUFFERS)
    return -EINVAL;

  if (opener->timeout_image_io)
    *b = dev->timeout_image_buffer.buffer;
  else
    *b = dev->buffers[b->index % dev->used_buffers].buffer;

  b->type = type;
  b->index = index;
  dprintkrw("buffer type: %d (of %d with size=%ld)", b->memory, dev->buffers_number, dev->buffer_size);
  return 0;
}

static void
buffer_written(struct v4l2_loopback_device *dev, struct v4l2l_buffer *buf)
{
  timer_delete_sync(&dev->sustain_timer);
  timer_delete_sync(&dev->timeout_timer);
  spin_lock_bh(&dev->lock);

  dev->bufpos2index[dev->write_position % dev->used_buffers] = buf->buffer.index;
  list_move_tail(&buf->list_head, &dev->outbufs_list);
  ++dev->write_position;
  dev->reread_count = 0;

  check_timers(dev);
  spin_unlock_bh(&dev->lock);
}

/* put buffer to queue
 * called on VIDIOC_QBUF
 */
static int
vidioc_qbuf         (struct file *file,
                     void *private_data,
                     struct v4l2_buffer *buf)
{
  struct v4l2_loopback_device *dev;
  struct v4l2_loopback_opener *opener;
  struct v4l2l_buffer *b;
  int index;

  dev=v4l2loopback_getdevice(file);
  opener = file->private_data;

  if (buf->index > MAX_BUFFERS)
    return -EINVAL;
  if (opener->timeout_image_io)
    return 0;

  index = buf->index % dev->used_buffers;
  b=&dev->buffers[index];

  switch (buf->type) {
  case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    dprintkrw("capture QBUF index: %d\n", index);
    set_queued(b);
    return 0;
  case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    dprintkrw("output QBUF pos: %d index: %d\n", dev->write_position, index);
    get_timestamp(&b->buffer);
    set_done(b);
    buffer_written(dev, b);
    wake_up_all(&dev->read_event);
    return 0;
  default:
    return -EINVAL;
  }
}

static int
can_read(struct v4l2_loopback_device *dev, struct v4l2_loopback_opener *opener)
{
  int ret;
  spin_lock_bh(&dev->lock);
  check_timers(dev);
  ret = dev->write_position > opener->read_position
        || dev->reread_count > opener->reread_count
        || dev->timeout_happened;
  spin_unlock_bh(&dev->lock);
  return ret;
}

static int
get_capture_buffer(struct file *file)
{
  struct v4l2_loopback_device *dev = v4l2loopback_getdevice(file);
  struct v4l2_loopback_opener *opener = file->private_data;
  int pos, ret;
  int timeout_happened;

  if ((file->f_flags&O_NONBLOCK) && (dev->write_position <= opener->read_position &&
                                      dev->reread_count <= opener->reread_count &&
                                      !dev->timeout_happened))
    return -EAGAIN;
  wait_event_interruptible(dev->read_event, can_read(dev, opener));

  spin_lock_bh(&dev->lock);
  if (dev->write_position == opener->read_position) {
    if (dev->reread_count > opener->reread_count+2)
      opener->reread_count = dev->reread_count - 1;
    ++opener->reread_count;
    pos = (opener->read_position + dev->used_buffers - 1) % dev->used_buffers;
  } else {
    opener->reread_count = 0;
    if (dev->write_position > opener->read_position+2)
      opener->read_position = dev->write_position - 1;
    pos = opener->read_position % dev->used_buffers;
    ++opener->read_position;
  }
  timeout_happened = dev->timeout_happened;
  dev->timeout_happened = 0;
  spin_unlock_bh(&dev->lock);

  ret = dev->bufpos2index[pos];
  if (timeout_happened) {
    /* although allocated on-demand, timeout_image is freed only in free_buffers(),
     * so we don't need to worry about it being deallocated suddenly */
    memcpy(dev->image + dev->buffers[ret].buffer.m.offset, dev->timeout_image, dev->buffer_size);
  }
  return ret;
}

/* put buffer to dequeue
 * called on VIDIOC_DQBUF
 */
static int
vidioc_dqbuf        (struct file *file,
                     void *private_data,
                     struct v4l2_buffer *buf)
{
  struct v4l2_loopback_device *dev;
  struct v4l2_loopback_opener *opener;
  int index;
  struct v4l2l_buffer *b;

  dev=v4l2loopback_getdevice(file);
  opener = file->private_data;
  if (opener->timeout_image_io) {
    *buf = dev->timeout_image_buffer.buffer;
    return 0;
  }

  switch (buf->type) {
  case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    index = get_capture_buffer(file);
    if (index < 0)
      return index;
    dprintkrw("capture DQBUF pos: %d index: %d\n", opener->read_position - 1, index);
    if (!(dev->buffers[index].buffer.flags&V4L2_BUF_FLAG_MAPPED)) {
      dprintk("trying to return not mapped buf\n");
      return -EINVAL;
    }
    unset_flags(&dev->buffers[index]);
    *buf = dev->buffers[index].buffer;
    return 0;
  case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    b = list_entry(dev->outbufs_list.next, struct v4l2l_buffer, list_head);
    list_move_tail(&b->list_head, &dev->outbufs_list);
    dprintkrw("output DQBUF index: %d\n", b->buffer.index);
    unset_flags(b);
    *buf = b->buffer;
    buf->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    return 0;
  default:
    return -EINVAL;
  }
}

/* ------------- STREAMING ------------------- */

/* start streaming
 * called on VIDIOC_STREAMON
 */
static int
vidioc_streamon     (struct file *file,
                     void *private_data,
                     enum v4l2_buf_type type)
{
  struct v4l2_loopback_device *dev;
  struct v4l2_loopback_opener *opener;
  int ret;
  MARK();

  dev=v4l2loopback_getdevice(file);
  opener = file->private_data;

  switch (type) {
  case V4L2_BUF_TYPE_VIDEO_OUTPUT:
    opener->type = WRITER;
    if (!dev->ready_for_capture) {
      ret = allocate_buffers(dev);
      if (ret < 0)
        return ret;
      dev->ready_for_capture = 1;
    }
    return 0;
  case V4L2_BUF_TYPE_VIDEO_CAPTURE:
    opener->type = READER;
    if (!dev->ready_for_capture)
      return -EIO;
    return 0;
  default:
    return -EINVAL;
  }
}

/* stop streaming
 * called on VIDIOC_STREAMOFF
 */
static int
vidioc_streamoff    (struct file *file,
                     void *private_data,
                     enum v4l2_buf_type type)
{
  MARK();
  dprintk("%d", type);
  return 0;
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int
vidiocgmbuf         (struct file *file,
                     void *fh,
                     struct video_mbuf *p)
{
  struct v4l2_loopback_device *dev;
  MARK();

  dev=v4l2loopback_getdevice(file);
  p->frames = dev->buffers_number;
  p->offsets[0] = 0;
  p->offsets[1] = 0;
  p->size = dev->buffer_size;
  return 0;
}
#endif

/* file operations */
static void
vm_open             (struct vm_area_struct *vma)
{
  struct v4l2l_buffer *buf;
  MARK();

  buf=vma->vm_private_data;
  buf->use_count++;
}

static void
vm_close            (struct vm_area_struct *vma)
{
  struct v4l2l_buffer *buf;
  MARK();

  buf=vma->vm_private_data;
  buf->use_count--;
}

static struct vm_operations_struct vm_ops = {
  .open = vm_open,
  .close = vm_close,
};

static int
v4l2_loopback_mmap  (struct file *file,
                     struct vm_area_struct *vma)
{
  int i;
  unsigned long addr;
  unsigned long start;
  unsigned long size;
  struct v4l2_loopback_device *dev;
  struct v4l2_loopback_opener *opener;
  struct v4l2l_buffer *buffer = NULL;
  MARK();

  start = (unsigned long) vma->vm_start;
  size = (unsigned long) (vma->vm_end - vma->vm_start);

  dev=v4l2loopback_getdevice(file);
  opener=file->private_data;

  if (size > dev->buffer_size) {
    dprintk("userspace tries to mmap too much, fail\n");
    return -EINVAL;
  }
  if (opener->timeout_image_io) {
    /* we are going to map the timeout_image_buffer */
    if ((vma->vm_pgoff << PAGE_SHIFT) != dev->buffer_size * MAX_BUFFERS) {
      dprintk("invalid mmap offset for timeout_image_io mode\n");
      return -EINVAL;
    }
  } else if ((vma->vm_pgoff << PAGE_SHIFT) >
      dev->buffer_size * (dev->buffers_number - 1)) {
    dprintk("userspace tries to mmap too far, fail\n");
    return -EINVAL;
  }

  /* FIXXXXXME: allocation should not happen here! */
  if(NULL==dev->image) {
    if(allocate_buffers(dev)<0) {
      return -EINVAL;
    }
  }

  if (opener->timeout_image_io) {
    buffer = &dev->timeout_image_buffer;
    addr = (unsigned long) dev->timeout_image;
  } else {
    for (i = 0; i < dev->buffers_number; ++i) {
      buffer = &dev->buffers[i];
      if ((buffer->buffer.m.offset >> PAGE_SHIFT) == vma->vm_pgoff)
        break;
    }

    if(NULL == buffer) {
      return -EINVAL;
    }

    addr = (unsigned long) dev->image + (vma->vm_pgoff << PAGE_SHIFT);
  }

  while (size > 0) {
    struct page *page;

    page = (void *) vmalloc_to_page((void *) addr);

    if (vm_insert_page(vma, start, page) < 0)
      return -EAGAIN;

    start += PAGE_SIZE;
    addr += PAGE_SIZE;
    size -= PAGE_SIZE;
  }

  vma->vm_ops = &vm_ops;
  vma->vm_private_data = buffer;
  buffer->buffer.flags |= V4L2_BUF_FLAG_MAPPED;

  vm_open(vma);

  MARK();
  return 0;
}

static unsigned int
v4l2_loopback_poll  (struct file *file,
                     struct poll_table_struct *pts)
{
  struct v4l2_loopback_opener *opener;
  struct v4l2_loopback_device *dev;
  int ret_mask = 0;
  MARK();

  opener = file->private_data;
  dev    = v4l2loopback_getdevice(file);

  switch (opener->type) {
  case WRITER:
    ret_mask = POLLOUT | POLLWRNORM;
    break;
  case READER:
    poll_wait(file, &dev->read_event, pts);
    if (can_read(dev, opener))
      ret_mask =  POLLIN | POLLRDNORM;
    break;
  default:
    ret_mask = -POLLERR;
  }
  MARK();

  return ret_mask;
}

/* do not want to limit device opens, it can be as many readers as user want,
 * writers are limited by means of setting writer field */
static int
v4l2_loopback_open   (struct file *file)
{
  struct v4l2_loopback_device *dev;
  struct v4l2_loopback_opener *opener;
  MARK();

  dev=v4l2loopback_getdevice(file);

  if (dev->open_count.counter >= dev->max_openers)
    return -EBUSY;
  /* kfree on close */
  opener = kzalloc(sizeof(*opener), GFP_KERNEL);
  if (opener == NULL)
    return -ENOMEM;
  file->private_data = opener;
  atomic_inc(&dev->open_count);

  opener->timeout_image_io = dev->timeout_image_io;
  dev->timeout_image_io = 0;

  if (opener->timeout_image_io) {
    int r = allocate_timeout_image(dev);
    if (r < 0) {
      dprintk("timeout image allocation failed\n");
      return r;
    }
  }
  dprintk("opened dev:%p with image:%p", dev, dev?dev->image:NULL);
  // droidcam:
  {
       struct v4l2_format vid_format;
       vidioc_g_fmt_out(NULL, NULL, &vid_format);
       vid_format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
       vid_format.fmt.pix.width = width;
       vid_format.fmt.pix.height = height;
       vid_format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV420;
       vid_format.fmt.pix.field = V4L2_FIELD_NONE;
       vid_format.fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
       if (0 != vidioc_s_fmt_out(NULL, NULL, &vid_format))
        printk("Setting DroidCam default format FAILED!");
       else
        dev->ready_for_capture = 1;
  }

  MARK();
  return 0;
}

static int
v4l2_loopback_close  (struct file *file)
{
  struct v4l2_loopback_opener *opener;
  struct v4l2_loopback_device *dev;
  MARK();

  opener = file->private_data;
  dev    = v4l2loopback_getdevice(file);

  atomic_dec(&dev->open_count);
  if (dev->open_count.counter == 0) {
    timer_delete_sync(&dev->sustain_timer);
    timer_delete_sync(&dev->timeout_timer);
  }
  try_free_buffers(dev);
  kfree(opener);
  MARK();
  return 0;
}

static ssize_t
v4l2_loopback_read   (struct file *file,
                      char __user *buf,
                      size_t count,
                      loff_t *ppos)
{
  int read_index;
  struct v4l2_loopback_opener *opener;
  struct v4l2_loopback_device *dev;
  MARK();

  opener = file->private_data;
  dev    = v4l2loopback_getdevice(file);

  read_index = get_capture_buffer(file);
  if (count > dev->buffer_size)
    count = dev->buffer_size;
  if (copy_to_user((void *) buf, (void *) (dev->image +
                                           dev->buffers[read_index].buffer.m.offset), count)) {
    printk(KERN_ERR "v4l2-loopback: "
           "failed copy_from_user() in write buf\n");
    return -EFAULT;
  }
  dprintkrw("leave v4l2_loopback_read()\n");
  return count;
}

static ssize_t
v4l2_loopback_write  (struct file *file,
                      const char __user *buf,
                      size_t count,
                      loff_t *ppos)
{
  struct v4l2_loopback_device *dev;
  int write_index;
  struct v4l2_buffer*b;
  int ret;
  MARK();

  dev=v4l2loopback_getdevice(file);

  if (!dev->ready_for_capture) {
    ret = allocate_buffers(dev);
    if (ret < 0)
      return ret;
    dev->ready_for_capture = 1;
  }
  dprintkrw("v4l2_loopback_write() trying to write %zu bytes\n", count);
  if (count > dev->buffer_size)
    count = dev->buffer_size;

  write_index = dev->write_position % dev->used_buffers;
  b=&dev->buffers[write_index].buffer;

  if (copy_from_user((void *) (dev->image + b->m.offset),
                     (void *) buf, count)) {
    printk(KERN_ERR "v4l2-loopback: "
           "failed copy_from_user() in write buf, could not write %zu\n",
           count);
    return -EFAULT;
  }
  get_timestamp(b);
  b->sequence = dev->write_position;
  buffer_written(dev, &dev->buffers[write_index]);
  wake_up_all(&dev->read_event);
  dprintkrw("leave v4l2_loopback_write()\n");
  return count;
}

/* init functions */
/* frees buffers, if already allocated */
static int free_buffers(struct v4l2_loopback_device *dev)
{
  MARK();
  dprintk("freeing image@%p for dev:%p", dev?(dev->image):NULL, dev);
  if(dev->image) {
    vfree(dev->image);
    dev->image=NULL;
  }
  if(dev->timeout_image) {
    vfree(dev->timeout_image);
    dev->timeout_image=NULL;
  }
  dev->imagesize=0;

  return 0;
}
/* frees buffers, if they are no longer needed */
static void
try_free_buffers(struct v4l2_loopback_device *dev)
{
  MARK();
  if (0 == dev->open_count.counter && !dev->keep_format) {
    free_buffers(dev);
    dev->ready_for_capture = 0;
    dev->buffer_size = 0;
    dev->write_position = 0;
  }
}
/* allocates buffers, if buffer_size is set */
static int
allocate_buffers    (struct v4l2_loopback_device *dev)
{
  MARK();
  /* vfree on close file operation in case no open handles left */
  if (0 == dev->buffer_size)
    return -EINVAL;

  if (dev->image) {
    dprintk("allocating buffers again: %ld %ld", dev->buffer_size * dev->buffers_number, dev->imagesize);
    /* FIXME: prevent double allocation more intelligently! */
    if(dev->buffer_size * dev->buffers_number == dev->imagesize)
      return 0;

    /* if there is only one writer, no problem should occur */
    if (dev->open_count.counter==1)
      free_buffers(dev);
    else
      return -EINVAL;
  }

  dev->imagesize=dev->buffer_size * dev->buffers_number;

  dprintk("allocating %ld = %ldx%d", dev->imagesize, dev->buffer_size, dev->buffers_number);

  dev->image = vmalloc(dev->imagesize);
  if (dev->timeout_jiffies > 0)
    allocate_timeout_image(dev);

  if (dev->image == NULL)
    return -ENOMEM;
  dprintk("vmallocated %ld bytes\n",
          dev->imagesize);
  MARK();
  init_buffers(dev);
  return 0;
}
/* init inner buffers, they are capture mode and flags are set as
 * for capture mod buffers */
static void
init_buffers        (struct v4l2_loopback_device *dev)
{
  int i;
  int buffer_size;
  int bytesused;
  MARK();

  buffer_size=dev->buffer_size;
  bytesused = dev->pix_format.sizeimage;

  for (i = 0; i < dev->buffers_number; ++i) {
    struct v4l2_buffer*b=&dev->buffers[i].buffer;
    b->index             = i;
    b->bytesused         = bytesused;
    b->length            = buffer_size;
    b->field             = V4L2_FIELD_NONE;
    b->flags             = 0;
//    b->input             = 0;
    b->m.offset          = i * buffer_size;
    b->memory            = V4L2_MEMORY_MMAP;
    b->sequence          = 0;
    b->timestamp.tv_sec  = 0;
    b->timestamp.tv_usec = 0;
    b->type              = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    get_timestamp(b);
  }
  dev->timeout_image_buffer = dev->buffers[0];
  dev->timeout_image_buffer.buffer.m.offset = MAX_BUFFERS * buffer_size;
  MARK();
}

static int
allocate_timeout_image(struct v4l2_loopback_device *dev)
{
  MARK();
  if (dev->buffer_size <= 0)
    return -EINVAL;

  if (dev->timeout_image == NULL) {
    dev->timeout_image = vzalloc(dev->buffer_size);
    if (dev->timeout_image == NULL)
      return -ENOMEM;
  }
  return 0;
}

/* fills and register video device */
static void
init_vdev           (struct video_device *vdev)
{
  MARK();
  strscpy(vdev->name, "Loopback video device", sizeof(vdev->name));

#if 0
  //todo: remove V4L2_STD stuff
  vdev->tvnorms      = V4L2_STD_ALL;
  vdev->current_norm = V4L2_STD_ALL;
#endif

  vdev->vfl_type     = VFL_TYPE_VIDEO;
  vdev->fops         = &v4l2_loopback_fops;
  vdev->ioctl_ops    = &v4l2_loopback_ioctl_ops;
  vdev->release      = &video_device_release;
  vdev->minor        = -1;
  #if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0)
  vdev->device_caps  =
    V4L2_CAP_DEVICE_CAPS |
    V4L2_CAP_VIDEO_CAPTURE |
    V4L2_CAP_STREAMING | V4L2_CAP_READWRITE;
  #endif

  /* since kernel-3.7, there is a new field 'vfl_dir' that has to be
   * set to VFL_DIR_M2M for bidrectional devices.
   * For DroidCam, this allows other programs like ffmpeg to write to
   * the device.  */
  #ifdef VFL_DIR_M2M
    vdev->vfl_dir = VFL_DIR_M2M;
  #endif
#if DEBUG
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 20, 0)
   vdev->debug = V4L2_DEBUG_IOCTL | V4L2_DEBUG_IOCTL_ARG;
#else
   vdev->dev_debug = V4L2_DEV_DEBUG_IOCTL | V4L2_DEV_DEBUG_IOCTL_ARG;
#endif
#endif
  MARK();
}

/* init default capture parameters, only fps may be changed in future */
static void
init_capture_param  (struct v4l2_captureparm *capture_param)
{
  MARK();
  capture_param->capability               = 0;
  capture_param->capturemode              = 0;
  capture_param->extendedmode             = 0;
  capture_param->readbuffers              = MAX_BUFFERS;
  capture_param->timeperframe.numerator   = 1;
  capture_param->timeperframe.denominator = 30;
}

static void
check_timers(struct v4l2_loopback_device *dev)
{
  if (!dev->ready_for_capture)
    return;

  if (dev->timeout_jiffies > 0 && !timer_pending(&dev->timeout_timer))
    mod_timer(&dev->timeout_timer, jiffies + dev->timeout_jiffies);
  if (dev->sustain_framerate && !timer_pending(&dev->sustain_timer))
    mod_timer(&dev->sustain_timer, jiffies + dev->frame_jiffies * 3 / 2);
}

#ifdef HAVE_TIMER_SETUP

static void sustain_timer_clb(struct timer_list *t)
{
   struct v4l2_loopback_device *dev = container_of(t, struct v4l2_loopback_device, sustain_timer);
#else

static void sustain_timer_clb(unsigned long nr)
{
  struct v4l2_loopback_device *dev = devs[nr];

#endif

  spin_lock(&dev->lock);
  if (dev->sustain_framerate) {
    dev->reread_count++;
    dprintkrw("reread: %d %d", dev->write_position, dev->reread_count);
    if (dev->reread_count == 1)
      mod_timer(&dev->sustain_timer, jiffies + max(1UL, dev->frame_jiffies / 2));
    else
      mod_timer(&dev->sustain_timer, jiffies + dev->frame_jiffies);
    wake_up_all(&dev->read_event);
  }
  spin_unlock(&dev->lock);
}

#ifdef HAVE_TIMER_SETUP
static void timeout_timer_clb(struct timer_list *t)
{
 struct v4l2_loopback_device *dev = container_of(t, struct v4l2_loopback_device, timeout_timer);
#else

static void timeout_timer_clb(unsigned long nr)
{
  struct v4l2_loopback_device *dev = devs[nr];

#endif
  spin_lock(&dev->lock);
  if (dev->timeout_jiffies > 0) {
    dev->timeout_happened = 1;
    mod_timer(&dev->timeout_timer, jiffies + dev->timeout_jiffies);
    wake_up_all(&dev->read_event);
  }
  spin_unlock(&dev->lock);
}

/* init loopback main structure */
static int
v4l2_loopback_init  (struct v4l2_loopback_device *dev,
                     int nr)
{
  MARK();

  {
    int ret;
    snprintf(dev->v4l2_dev.name, sizeof(dev->v4l2_dev.name), "Droidcam (v4l2loopback-%03d)", nr);
    if ((ret = v4l2_device_register(NULL, &dev->v4l2_dev))) return ret;
  }

  dev->vdev = video_device_alloc();
  if (dev->vdev == NULL) {
    v4l2_device_unregister(&dev->v4l2_dev);
    return -ENOMEM;
  }

  video_set_drvdata(dev->vdev, kzalloc(sizeof(struct v4l2loopback_private), GFP_KERNEL));
  if (video_get_drvdata(dev->vdev) == NULL) {
    v4l2_device_unregister(&dev->v4l2_dev);
    kfree(dev->vdev);
    return -ENOMEM;
  }
  ((priv_ptr)video_get_drvdata(dev->vdev))->devicenr = nr;

  init_vdev(dev->vdev);
  dev->vdev->v4l2_dev = &dev->v4l2_dev;
  init_capture_param(&dev->capture_param);
  set_timeperframe(dev, &dev->capture_param.timeperframe);
  dev->keep_format = 0;
  dev->sustain_framerate = 0;
  dev->buffers_number = MAX_BUFFERS;
  dev->used_buffers = MAX_BUFFERS;
  dev->max_openers = MAX_OPENERS;
  dev->write_position = 0;
  INIT_LIST_HEAD(&dev->outbufs_list);
  if (list_empty(&dev->outbufs_list)) {
    int i;
    for (i = 0; i < dev->used_buffers; ++i) {
      list_add_tail(&dev->buffers[i].list_head, &dev->outbufs_list);
    }
  }
  memset(dev->bufpos2index, 0, sizeof(dev->bufpos2index));
  atomic_set(&dev->open_count, 0);
  dev->ready_for_capture = 0;
  dev->buffer_size = 0;
  dev->image = NULL;
  dev->imagesize = 0;

#ifdef HAVE_TIMER_SETUP
  timer_setup(&dev->sustain_timer, sustain_timer_clb, 0);
  timer_setup(&dev->timeout_timer, timeout_timer_clb, 0);
#else
  setup_timer(&dev->sustain_timer, sustain_timer_clb, nr);
  setup_timer(&dev->timeout_timer, timeout_timer_clb, nr);
#endif
  dev->reread_count = 0;
  dev->timeout_jiffies = 0;
  dev->timeout_image = NULL;
  dev->timeout_happened = 0;

  /* FIXME set buffers to 0 */

  /* Set initial format */

  dev->pix_format.width = 0; /* V4L2LOOPBACK_SIZE_DEFAULT_WIDTH; */
  dev->pix_format.height = 0; /* V4L2LOOPBACK_SIZE_DEFAULT_HEIGHT; */
  dev->pix_format.pixelformat = formats[0].fourcc;
  dev->pix_format.colorspace = V4L2_COLORSPACE_SRGB; /* do we need to set this ? */
  dev->pix_format.field = V4L2_FIELD_NONE;
  dev->buffer_size = PAGE_ALIGN(dev->pix_format.sizeimage);

  dprintk("buffer_size = %ld (=%d)\n", dev->buffer_size, dev->pix_format.sizeimage);
  allocate_buffers(dev);

  init_waitqueue_head(&dev->read_event);

  MARK();
  return 0;
};

/* LINUX KERNEL */
static const struct v4l2_file_operations v4l2_loopback_fops = {
  .owner   = THIS_MODULE,
  .open    = v4l2_loopback_open,
  .release = v4l2_loopback_close,
  .read    = v4l2_loopback_read,
  .write   = v4l2_loopback_write,
  .poll    = v4l2_loopback_poll,
  .mmap    = v4l2_loopback_mmap,
  .unlocked_ioctl   = video_ioctl2,
};

static const struct v4l2_ioctl_ops v4l2_loopback_ioctl_ops = {
  .vidioc_querycap         = &vidioc_querycap,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29)
  .vidioc_enum_framesizes  = &vidioc_enum_framesizes,
  .vidioc_enum_frameintervals = &vidioc_enum_frameintervals,
#endif

  .vidioc_query_ext_ctrl    = &vidioc_query_ext_ctrl,
  .vidioc_s_ext_ctrls       = &vidioc_s_ext_ctrls,
  .vidioc_g_ext_ctrls       = &vidioc_g_ext_ctrls,

  .vidioc_enum_output       = &vidioc_enum_output,
  .vidioc_g_output          = &vidioc_g_output,
  .vidioc_s_output          = &vidioc_s_output,

  .vidioc_enum_input       = &vidioc_enum_input,
  .vidioc_g_input          = &vidioc_g_input,
  .vidioc_s_input          = &vidioc_s_input,

  .vidioc_enum_fmt_vid_cap = &vidioc_enum_fmt_cap,
  .vidioc_g_fmt_vid_cap    = &vidioc_g_fmt_cap,
  .vidioc_s_fmt_vid_cap    = &vidioc_s_fmt_cap,
  .vidioc_try_fmt_vid_cap  = &vidioc_try_fmt_cap,

  .vidioc_enum_fmt_vid_out = &vidioc_enum_fmt_out,
  .vidioc_s_fmt_vid_out    = &vidioc_s_fmt_out,
  .vidioc_g_fmt_vid_out    = &vidioc_g_fmt_out,
  .vidioc_try_fmt_vid_out  = &vidioc_try_fmt_out,

#ifdef V4L2L_OVERLAY
  .vidioc_s_fmt_vid_overlay= &vidioc_s_fmt_overlay,
  .vidioc_g_fmt_vid_overlay= &vidioc_g_fmt_overlay,
#endif

#ifdef V4L2LOOPBACK_WITH_STD
  .vidioc_s_std            = &vidioc_s_std,
  .vidioc_g_std            = &vidioc_g_std,
  .vidioc_querystd         = &vidioc_querystd,
#endif

  .vidioc_g_parm           = &vidioc_g_parm,
  .vidioc_s_parm           = &vidioc_s_parm,

  .vidioc_reqbufs          = &vidioc_reqbufs,
  .vidioc_querybuf         = &vidioc_querybuf,
  .vidioc_qbuf             = &vidioc_qbuf,
  .vidioc_dqbuf            = &vidioc_dqbuf,

  .vidioc_streamon         = &vidioc_streamon,
  .vidioc_streamoff        = &vidioc_streamoff,

#ifdef CONFIG_VIDEO_V4L1_COMPAT
  .vidiocgmbuf             = &vidiocgmbuf,
#endif
};

static void
zero_devices        (void)
{
  int i;
  MARK();
  for(i=0; i<MAX_DEVICES; i++) {
    devs[i]=NULL;
  }
}

static void
free_devices        (void)
{
  int i;
  MARK();
  for(i=0; i<DEVICES; i++) {
    if(NULL!=devs[i]) {
      free_buffers(devs[i]);
      v4l2loopback_remove_sysfs(devs[i]->vdev);
      kfree(video_get_drvdata(devs[i]->vdev));
      video_unregister_device(devs[i]->vdev);
      v4l2_device_unregister(&devs[i]->v4l2_dev);
      kfree(devs[i]);
      devs[i]=NULL;
    }
  }
}

static int __init v4l2loopback_init_module(void)
{
  int ret;
  int i;
  MARK();

  zero_devices();

  /* kfree on module release */
  for(i=0; i<DEVICES; i++) {
    dprintk("creating v4l2loopback-device #%d on device %d\n", i, video_nr);
    devs[i] = kzalloc(sizeof(*devs[i]), GFP_KERNEL);
    if (devs[i] == NULL) {
      free_devices();
      return -ENOMEM;
    }
    ret = v4l2_loopback_init(devs[i], i);
    if (ret < 0) {
      free_devices();
      return ret;
    }
    /* register the device -> it creates /dev/video* */
    if (video_register_device(devs[i]->vdev, VFL_TYPE_VIDEO, video_nr) < 0) {
      video_device_release(devs[i]->vdev);
      printk(KERN_ERR "v4l2loopback: failed video_register_device()\n");
      free_devices();
      return -EFAULT;
    }
    v4l2loopback_create_sysfs(devs[i]->vdev);
  }

  dprintk("module installed\n");

  printk(KERN_INFO "v4l2loopback driver version %d.%d.%d (droidcam) loaded\n",
         (V4L2LOOPBACK_VERSION_CODE >> 16) & 0xff,
         (V4L2LOOPBACK_VERSION_CODE >>  8) & 0xff,
         (V4L2LOOPBACK_VERSION_CODE      ) & 0xff);

  return 0;
}

static void v4l2loopback_cleanup_module(void)
{
  MARK();
  /* unregister the device -> it deletes /dev/video* */
  free_devices();
  dprintk("module removed\n");
}

module_init(v4l2loopback_init_module);
module_exit(v4l2loopback_cleanup_module);
