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
#include <sys/socket.h>
#include <unistd.h>

#include "client.h"
#include "constants.h"
#include "libmjpeg2http.h"
#include "list.h"
#include "protocol.h"
#include "server.h"
#include "video.h"

enum type { SERVER, CLIENT, VIDEO, TOKEN };

union observed_data {
  client_t *client;
  int fd;
};

struct observed {
  struct dlist node;
  enum type t;
  union observed_data data;
};

static char g_frame_complete[MAX_FRAME_SIZE];
static int g_numClients;
static const int g_maxClients = MAX_FILE_DESCRIPTORS - 2;
static char g_token[NUMBER_OF_TOKEN * (TOKEN_SIZE + 1)];
static int g_token_pos = -1;
static int g_videoOn = 0;
static int g_frame_complete_len;
static int g_token_len;
static int g_epfd;
static int g_pipe_fd = -1;
static struct observed g_pipe;
static int g_runs = 0;

static void cleanAll() {
  g_numClients = 0;
  g_token_pos = -1;
  g_videoOn = 0;
  g_pipe_fd = -1;
}

static int enable_video(struct observed *video) {
  struct epoll_event ev;
  ev.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
  ev.data.ptr = video;
  if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, video->data.fd, &ev) == -1) {
    perror("epoll_ctl: enable video");
    return -1;
  }
  return 1;
}

static int disable_video(struct observed *video) {
  if (epoll_ctl(g_epfd, EPOLL_CTL_DEL, video->data.fd, NULL) == -1) {
    perror("epoll_ctl: disable video");
    return -1;
  }
  return 1;
}

static int add_clients(int server_fd, struct dlist *clients,
                       struct observed *video) {
  int ret;
  struct remotepeer peer;
  struct epoll_event ev;
  do {
    ret = server_new_peer(server_fd, &peer);
    if (ret == 0)
      break; // no more connections to accept
    else if (ret == -1) {
      perror("server_new_peer error");
      return -1;
    }

    if (g_numClients <= g_maxClients - 1) {
      struct observed *oc = malloc(sizeof(struct observed));
      oc->data.client = client_init(peer.hostname, peer.port, peer.fd);
      oc->t = CLIENT;
      list_add_right(&oc->node, clients);
      ev.events =
          EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
      ev.data.ptr = oc;
      if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, peer.fd, &ev) == -1) {
        perror("epoll_ctl: add clients");
        return -1;
      }
      ++g_numClients;
    } else {
      printf("reject new connection => increase MAX_FILE_DESCRIPTORS\n");
      fflush(stdout);
      close(peer.fd);
    }
  } while (ret > 0);
  if (g_numClients > 0 && g_videoOn == 0) {
    printf("turn on video because clients=%d\n", g_numClients);
    fflush(stdout);
    g_videoOn = 1;
    return enable_video(video);
  }
  return 1;
}

static int remove_client(struct observed *oc, struct observed *video) {
  printf("remove client %s %d fd=%d\n", oc->data.client->hostname,
         oc->data.client->port, oc->data.client->fd);
  fflush(stdout);
  if (epoll_ctl(g_epfd, EPOLL_CTL_DEL, oc->data.client->fd, NULL) == -1) {
    perror("epoll_ctl: remove clients");
    return -1;
  }
  client_free(oc->data.client);
  list_del(&oc->node);
  free(oc);
  if (--g_numClients == 0 && g_videoOn == 1) {
    printf("turn off video because clients=%d\n", g_numClients);
    fflush(stdout);
    g_videoOn = 0;
    return disable_video(video);
  }
  return 1;
}

static void prepare_frame(uint8_t *jpeg_image, uint32_t len) {
  struct timeval timestamp;
  gettimeofday(&timestamp, NULL);
  int total = snprintf(g_frame_complete, MAX_FRAME_SIZE, frame_header, len,
                       (int)timestamp.tv_sec, (int)timestamp.tv_usec);
  memcpy(g_frame_complete + total, jpeg_image, len);
  memcpy(g_frame_complete + total + len, end_frame, end_frame_len);
  g_frame_complete_len = total + len + end_frame_len;
}

static int handle_new_frame(struct dlist *clients) {
  int n = video_read_jpeg(prepare_frame, MAX_FRAME_SIZE);
  if (n > 0) {
    uint8_t *allocated = NULL;
    struct dlist *itr;
    list_iterate(itr, clients) {
      struct observed *oc = list_get_entry(itr, struct observed, node);
      if (oc->data.client->is_auth)
        client_enqueue_frame(oc->data.client, (uint8_t *)g_frame_complete,
                             g_frame_complete_len, &allocated);
    }
    if (allocated != NULL) {
      if (--*(allocated + g_frame_complete_len) == 0)
        free(allocated);
    }
  } else if (n < 0) {
    perror("error on handle new frame");
    return -1;
  }
  return 1;
}

static int create_pipe(char *name) {
  mkfifo(name, S_IRUSR | S_IWUSR);
  g_pipe_fd = open(name, O_RDWR | O_TRUNC);
  if (g_pipe_fd < 0)
    return -1;
  g_pipe.data.fd = g_pipe_fd;
  g_pipe.t = TOKEN;
  struct epoll_event ev;
  ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
  ev.data.ptr = &g_pipe;
  fcntl(g_pipe_fd, F_SETFL, O_NONBLOCK);
  if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, g_pipe_fd, &ev) == -1) {
    close(g_pipe_fd);
    return -1;
  }
  return 1;
}

static int handle_token(int fd) {
  int r;
  g_token_pos %= NUMBER_OF_TOKEN * (TOKEN_SIZE + 1);
  while ((r = read(fd, g_token + g_token_pos,
                   NUMBER_OF_TOKEN * (TOKEN_SIZE + 1) - g_token_pos)) > 0) {
    g_token_pos %= NUMBER_OF_TOKEN * (TOKEN_SIZE + 1);
    g_token_pos += r;
  }

  //	printf("token=%.*s\n", NUMBER_OF_TOKEN * (TOKEN_SIZE + 1), g_token);

  if (r < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return 0;
    }
    return -1;
  }

  return 0;
}

static int check_token(const char *auth, uint8_t *start, int count) {
  if (g_token_len == count && memcmp(auth, start, count) == 0)
    return 1;

  if (g_token_pos == -1 || TOKEN_SIZE != count)
    return 0;

  for (int i = 0; i < NUMBER_OF_TOKEN * (TOKEN_SIZE + 1); i += TOKEN_SIZE + 1) {
    if (memcmp(g_token + i, start, TOKEN_SIZE) == 0) {
      memset(g_token + i, 0, TOKEN_SIZE);
      return 1;
    }
  }
  return 0;
}

void libmjpeg2http_endLoop() {
  if (g_runs > 0) {
    --g_runs;
    close(g_epfd);
  }
}

int libmjpeg2http_loop(char *ipaddress, int port, char *device, char *token,
                       char *tokenpipe) {
  if (g_runs > 0) {
    printf("libmjpeg2http_loop: already running\n");
    fflush(stdout);
    return -1;
  }
  ++g_runs;

  cleanAll();

  // reference counter uses 1 byte so
  // to increase this limit more bytes
  // must be used
  if (g_maxClients >= 255) {
    printf("g_maxClients=%d exceeds 255\n", g_maxClients);
    fflush(stdout);
    return -1;
  }

  signal(SIGPIPE, SIG_IGN);

  g_epfd = epoll_create1(0);
  if (g_epfd == -1) {
    perror("epoll_create1");
    return -1;
  }

  int server_fd = server_create(ipaddress, port);
  if (server_fd < 0)
    goto errorOnServerCreate;

  int video_fd = video_init(device, WIDTH, HEIGHT, FRAME_PER_SECOND);
  if (video_fd < 0)
    goto errorOnVideoInit;

  struct epoll_event ev, events[MAX_FILE_DESCRIPTORS];
  struct observed video, server, *oev;
  struct dlist *itr, *save;

  video.data.fd = video_fd;
  video.t = VIDEO;

  g_token_len = strlen(token);

  // register webserver
  server.data.fd = server_fd;
  server.t = SERVER;
  ev.events = EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
  ev.data.ptr = &server;
  if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
    perror("epoll_ctl: server socket");
    goto errorOnRegisterServer;
  }

  if (tokenpipe != NULL) {
    g_token_pos = 0;
    if (create_pipe(tokenpipe) < 0) {
      goto errorOnCreatePipe;
    }
  }

  client_t *c;
  int nfds, n;
  declare_list(clients);

  printf("libmjpeg2http mainloop\n");
  fflush(stdout);

  for (;;) {

    nfds = epoll_wait(g_epfd, events, MAX_FILE_DESCRIPTORS, -1);
    if (nfds == -1) {
      if (errno != EBADF)
        perror("epoll_wait");
      goto errorOnEpollWait;
    }

    for (n = 0; n < nfds; ++n) {
      oev = (struct observed *)events[n].data.ptr;
      switch (oev->t) {

      case TOKEN:
        if (handle_token(oev->data.fd) < 0)
          goto errorOnHandleToken;
        break;

      case VIDEO:
        if (events[n].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
          perror("error on video");
          goto errorOnVideo;
        }
        if (handle_new_frame(&clients) < 0)
          goto errorOnHandleNewFrame;
        break;

      case SERVER:
        if (events[n].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
          perror("error on server");
          goto errorOnServer;
        }
        if (events[n].events & EPOLLIN) {
          if (add_clients(server_fd, &clients, &video) < 0)
            goto errorOnAddClients;
        }
        break;

      case CLIENT:
        c = oev->data.client;
        if (events[n].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
          printf("generic error fd=%d\n", c->fd);
          fflush(stdout);
          if (remove_client(oev, &video) < 0)
            goto errorOnRemoveClient;
          break;
        }

        if (events[n].events & EPOLLOUT) {
          if (client_tx(c) < 0) {
            if (remove_client(oev, &video) < 0)
              goto errorOnRemoveClient;
            break;
          }
        }

        if (events[n].events & EPOLLIN) {
          if (!c->is_auth) {
            int done = client_parse_request(c);
            if (done > 0) {
              if (check_token(token, c->rxbuf + c->start_token,
                              c->end_token - c->start_token)) {
                printf("client auth OK %s %d\n", c->hostname, c->port);
                fflush(stdout);
                c->is_auth = 1;
                client_enqueue_frame(c, (uint8_t *)welcome, welcome_len, NULL);
              } else {
                printf("client auth KO %s %d\n", c->hostname, c->port);
                fflush(stdout);
                client_enqueue_frame(c, (uint8_t *)welcome_ko, welcome_ko_len,
                                     NULL);
                if (remove_client(oev, &video) < 0)
                  goto errorOnRemoveClient;
              }
            } else if (done < 0) {
              if (remove_client(oev, &video) < 0)
                goto errorOnRemoveClient;
            }
          }
        }
        break;
      }
    }
  }

errorOnRemoveClient:
errorOnAddClients:
errorOnServer:
errorOnVideo:
errorOnHandleNewFrame:
errorOnHandleToken:
errorOnEpollWait:
  list_iterate_safe(itr, save, &clients) {
    list_del(&list_get_entry(itr, struct observed, node)->node);
    oev = list_get_entry(itr, struct observed, node);
    client_free(oev->data.client);
    free(oev);
  }

errorOnCreatePipe:
  if (g_pipe_fd != -1)
    close(g_pipe_fd);

errorOnRegisterServer:
  video_deinit();

errorOnVideoInit:
  shutdown(server_fd, SHUT_RDWR);
  close(server_fd);

errorOnServerCreate:
  close(g_epfd);

  printf("libmjpeg2http exit from loop\n");
  fflush(stdout);

  return 0;
}
