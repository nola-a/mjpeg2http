/**
 *  mjpeg2http
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
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "constants.h"
#include "video.h"

enum type { VIDEO };

union observed_data {
  int fd;
};

struct observed {
  enum type t;
  union observed_data data;
};

void enable_video(int epfd, struct observed *video) {
  struct epoll_event ev;
  ev.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
  ev.data.ptr = video;
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, video->data.fd, &ev) == -1) {
    perror("epoll_ctl: enable video");
    exit(EXIT_FAILURE);
  }
}

int g_idx = 0;
char g_path[1000];

void dump_frame(uint8_t *jpeg_image, uint32_t len) {
  char output[8192];
  snprintf(output, 8192, "%s%d.jpeg", g_path, g_idx++);
  int fd = open(output, O_RDWR | O_CREAT, S_IRWXU | S_IRUSR);
  if (fd < 0) {
    perror("error on opening!!");
    exit(EXIT_FAILURE);
  }
  int w = write(fd, jpeg_image, len);
  if (w != len) {
    perror("error on writing!!");
    exit(EXIT_FAILURE);
  }
  close(fd);
}

int main(int argc, char **argv) {
  if (argc != 3) {
    printf("usage example: ./dump2frame /dev/video0 /tmp/frames/test_\n");
    return 1;
  }

  setvbuf(stdout, NULL, _IONBF, 0);

  int epfd = epoll_create1(0);
  if (epfd == -1) {
    perror("epoll_create1");
    exit(EXIT_FAILURE);
  }

  strcpy(g_path, argv[2]);

  struct epoll_event events[MAX_FILE_DESCRIPTORS];
  int video_fd = video_init(argv[1], WIDTH, HEIGHT, FRAME_PER_SECOND);
  struct observed video, *ov;
  video.data.fd = video_fd;
  video.t = VIDEO;

  int nfds, n;
  enable_video(epfd, &video);

  for (;;) {

    nfds = epoll_wait(epfd, events, MAX_FILE_DESCRIPTORS, -1);
    if (nfds == -1) {
      perror("epoll_wait");
      exit(EXIT_FAILURE);
    }

    for (n = 0; n < nfds; ++n) {
      ov = (struct observed *)events[n].data.ptr;
      switch (ov->t) {
      case VIDEO:
        if (events[n].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
          perror("error on video");
          exit(EXIT_FAILURE);
        }
        video_read_jpeg(dump_frame, MAX_FRAME_SIZE);
        break;
      }
    }
  }

  return 0;
}
