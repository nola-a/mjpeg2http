/**
 *  mjpeg2http
 *
 *  based on:
 * https://www.kernel.org/doc/html/v4.10/media/uapi/v4l/capture.c.html
 *
 *  Copyright (c) 2022 Antonino Nolano. Licensed under the MIT license, as
 * follows:
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))

struct buffer {
  void *start;
  size_t length;
};

static char *dev_name;
static int fd = -1;
struct buffer *buffers;
static unsigned int n_buffers;
static int req_num = 30;
static int req_den = 1;
static int g_width = 640;
static int g_height = 480;

static int xioctl(int fh, int request, void *arg) {
  int r;

  do {
    r = ioctl(fh, request, arg);
  } while (-1 == r && EINTR == errno);

  return r;
}

int video_read_jpeg(void (*cb)(uint8_t *, uint32_t len), int maxsize) {
  struct v4l2_buffer buf;
  unsigned int i;

  CLEAR(buf);

  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_USERPTR;

  if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) {
    switch (errno) {
    case EAGAIN:
      return 0;

    case EIO:
      /* Could ignore EIO, see spec. */

      /* fall through */

    default:
      return -1;
    }
  }

  for (i = 0; i < n_buffers; ++i)
    if (buf.m.userptr == (unsigned long)buffers[i].start &&
        buf.length == buffers[i].length)
      break;

  if (i >= n_buffers) {
    fprintf(stderr, "invalid buffer index\\n");
    return -1;
  }
  if (buf.bytesused >= maxsize) {
    fprintf(stderr, "image too large\\n");
    return -1;
  }

  cb((uint8_t *)buf.m.userptr, buf.bytesused);

  if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
    return -1;

  return 1;
}

static int stop_capturing(void) {
  enum v4l2_buf_type type;

  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
    return -1;
  return 1;
}

static int start_capturing(void) {
  unsigned int i;
  enum v4l2_buf_type type;

  for (i = 0; i < n_buffers; ++i) {
    struct v4l2_buffer buf;

    CLEAR(buf);
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_USERPTR;
    buf.index = i;
    buf.m.userptr = (unsigned long)buffers[i].start;
    buf.length = buffers[i].length;

    if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
      return -1;
  }
  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
    return -1;
  return 1;
}

static void uninit_device(void) {
  unsigned int i;

  for (i = 0; i < n_buffers; ++i)
    free(buffers[i].start);

  free(buffers);
}

static int init_userp(unsigned int buffer_size) {
  struct v4l2_requestbuffers req;

  CLEAR(req);

  req.count = 4;
  req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  req.memory = V4L2_MEMORY_USERPTR;

  if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
    if (EINVAL == errno) {
      fprintf(stderr,
              "%s does not support "
              "user pointer i/on",
              dev_name);
      return -1;
    } else {
      return -1;
    }
  }

  buffers = calloc(4, sizeof(*buffers));

  if (!buffers) {
    fprintf(stderr, "Out of memory\\n");
    return -1;
  }

  for (n_buffers = 0; n_buffers < 4; ++n_buffers) {
    buffers[n_buffers].length = buffer_size;
    buffers[n_buffers].start = malloc(buffer_size);

    if (!buffers[n_buffers].start) {
      fprintf(stderr, "Out of memory\\n");
      for (int j = n_buffers - 1; j >= 0; --j)
        free(buffers[j].start);
      free(buffers);
      return -1;
    }
  }
  return 1;
}

static int setup_framerate() {

  struct v4l2_streamparm streamparm;
  streamparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (-1 == xioctl(fd, VIDIOC_G_PARM, &streamparm))
    return -1;

  if (streamparm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) {
    streamparm.parm.capture.timeperframe.numerator = req_den;
    streamparm.parm.capture.timeperframe.denominator = req_num;

    if (-1 == xioctl(fd, VIDIOC_S_PARM, &streamparm))
      return -1;

    if (streamparm.parm.capture.timeperframe.numerator != req_den ||
        streamparm.parm.capture.timeperframe.denominator != req_num) {
      fprintf(stderr,
              "the driver changed the time per frame from "
              "%d/%d to %d/%d\n",
              req_den, req_num, streamparm.parm.capture.timeperframe.numerator,
              streamparm.parm.capture.timeperframe.denominator);
    }
  } else {
    fprintf(stderr, "the driver does not allow to change time per frame\n");
  }

  return 1;
}

static int init_device(void) {
  struct v4l2_capability cap;
  struct v4l2_cropcap cropcap;
  struct v4l2_crop crop;
  struct v4l2_format fmt;
  unsigned int min;

  if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) {
    if (EINVAL == errno) {
      fprintf(stderr, "%s is no V4L2 device\\n", dev_name);
      return -1;
    } else {
      return -1;
    }
  }

  if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
    fprintf(stderr, "%s is no video capture device\\n", dev_name);
    return -1;
  }

  if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
    fprintf(stderr, "%s does not support streaming i/o\\n", dev_name);
    return -1;
  }

  CLEAR(cropcap);

  cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) {
    crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    crop.c = cropcap.defrect; /* reset to default */

    if (-1 == xioctl(fd, VIDIOC_S_CROP, &crop)) {
      switch (errno) {
      case EINVAL:
        /* Cropping not supported. */
        break;
      default:
        /* Errors ignored. */
        break;
      }
    }
  }

  if (setup_framerate() < 0)
    return -1;

  CLEAR(fmt);

  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = g_width;
  fmt.fmt.pix.height = g_height;
  fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG;
  fmt.fmt.pix.field = V4L2_FIELD_ANY;

  if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
    return -1;

  /* Buggy driver paranoia. */
  min = fmt.fmt.pix.width * 2;
  if (fmt.fmt.pix.bytesperline < min)
    fmt.fmt.pix.bytesperline = min;
  min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
  if (fmt.fmt.pix.sizeimage < min)
    fmt.fmt.pix.sizeimage = min;

  return init_userp(fmt.fmt.pix.sizeimage);
}

static void close_device(void) {
  close(fd);
  fd = -1;
}

static int open_device(void) {
  struct stat st;

  if (-1 == stat(dev_name, &st)) {
    fprintf(stderr, "Cannot identify '%s': %d, %s\\n", dev_name, errno,
            strerror(errno));
    return -1;
  }

  if (!S_ISCHR(st.st_mode)) {
    fprintf(stderr, "%s is no devicen", dev_name);
    return -1;
  }

  fd = open(dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);

  if (-1 == fd) {
    fprintf(stderr, "Cannot open '%s': %d, %s\\n", dev_name, errno,
            strerror(errno));
    return -1;
  }
  return fd;
}

int video_init(const char *dev, int width, int height, int rate) {
  dev_name = strdup(dev);
  g_width = width;
  g_height = height;
  req_num = rate;
  if (open_device() < 0)
    goto errorOnOpen;
  if (init_device() < 0)
    goto errorOnInit;
  if (start_capturing() < 0)
    goto errorOnStart;
  return fd;

errorOnStart:
  uninit_device();
errorOnInit:
  close(fd);
errorOnOpen:
  free(dev_name);
  fprintf(stderr, "error %d, %s\\n", errno, strerror(errno));
  return -1;
}

void video_deinit() {
  stop_capturing();
  uninit_device();
  close_device();
  free(dev_name);
}
