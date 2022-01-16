/**
 *  mjpeg2http
 *
 *  Copyright (c) 2022 Nolano Antonino. Licensed under the MIT license, as follows:
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#include <stdio.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <signal.h>

#include "video.h"
#include "server.h"
#include "client.h"
#include "list.h"
#include "protocol.h"

#define MAX_FRAME_SIZE 200000
#define MAX_FILE_DESCRIPTORS 32

enum type {
	SERVER,
	CLIENT,
	VIDEO
};

typedef union observed_data {
	client_t* client;
	int fd;
} observed_data_t;

struct observed {
	struct dlist node;
	enum type t;
	observed_data_t data;
};

extern const char* welcome;
extern const int welcome_len;
extern const char* welcome_ko;
extern const int welcome_ko_len;
extern const char* frame_header;
extern const char* end_frame;
extern const int end_frame_len;

char jpeg_image[MAX_FRAME_SIZE];
char frame_complete[MAX_FRAME_SIZE];

int numClients;
int maxClients;

void add_clients(int epfd, int server_fd, struct dlist* clients)
{
	int ret;
	struct remotepeer peer;
	struct epoll_event ev;
	do {
		ret = server_new_peer(server_fd, &peer);
		if (ret == -1) {
			if (EAGAIN == errno || EWOULDBLOCK == errno) break;
			perror("server_new_peer error");
			exit(EXIT_FAILURE);
		} 
		if (numClients <= maxClients - 1) {
			printf("add new client\n");
			struct observed* oc = malloc(sizeof(struct observed));
			oc->data.client = client_init(peer.hostname, peer.port, peer.fd);
			oc->t = CLIENT;
			list_add_right(&oc->node, clients);
			ev.events =  EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
			ev.data.ptr = oc;
			if (epoll_ctl(epfd, EPOLL_CTL_ADD, peer.fd, &ev) == -1) {
				perror("epoll_ctl: add client");
				exit(EXIT_FAILURE);
			}
			++numClients;
		} else {
			printf("reject new connection => increase MAX_FILE_DESCRIPTORS\n");
			close(peer.fd);
		}
	} while (ret > 0);
}

void remove_client(int epfd, struct observed* oc)
{
	printf("remove client %s %d\n", oc->data.client->hostname, oc->data.client->port);
	if (epoll_ctl(epfd, EPOLL_CTL_DEL, oc->data.client->fd, NULL) == -1) {
		perror("epoll_ctl: remove_client");
		exit(EXIT_FAILURE);
	}
	client_free(oc->data.client);
	list_del(&oc->node);
	free(oc);
	--numClients;
}

void handle_new_frame(struct dlist* clients) 
{
	uint32_t n = video_read_jpeg(jpeg_image, MAX_FRAME_SIZE);
	if (n > 0) {
		struct timeval timestamp;
		gettimeofday(&timestamp, NULL);
		int total = snprintf(frame_complete, MAX_FRAME_SIZE, frame_header, n, (int)timestamp.tv_sec, (int)timestamp.tv_usec);
		memcpy(frame_complete + total, jpeg_image, n);
		memcpy(frame_complete + total + n, end_frame, end_frame_len);
		struct dlist *itr;
		list_iterate(itr, clients) {
			struct observed* oc = list_get_entry(itr, struct observed, node);
			if (oc->data.client->is_auth)
				client_enqueue_frame(oc->data.client, (uint8_t*)frame_complete, total + n + end_frame_len);
		}
	}

}

void enable_video(int epfd, struct observed *video) 
{
	struct epoll_event ev;
	ev.events = EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
	ev.data.ptr = video;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, video->data.fd, &ev) == -1) {
		perror("epoll_ctl: enable video");
		exit(EXIT_FAILURE);
	}
}

void disable_video(int epfd, struct observed *video)
{
	if (epoll_ctl(epfd, EPOLL_CTL_DEL, video->data.fd, NULL) == -1) {
		perror("epoll_ctl: disable video");
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char **argv)
{
	signal(SIGPIPE, SIG_IGN);
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc != 5) {
		printf("usage example: ./mjpeg2http 192.168.2.1 8080 /dev/video0 this_is_token\n");
		return 1;
	}

	struct epoll_event ev, events[MAX_FILE_DESCRIPTORS];

	int server_fd = server_create(argv[1], atoi(argv[2]));
	int video_fd = video_init(argv[3], 640, 480, 30);
	declare_list(clients);

	struct observed video, server, *oev;
	video.data.fd = video_fd;
	video.t = VIDEO;
	server.data.fd = server_fd;
	server.t = SERVER;

	int epfd = epoll_create1(0);
	if (epfd == -1) {
		perror("epoll_create1");
		exit(EXIT_FAILURE);
	}

	ev.events =  EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
	ev.data.ptr = &server;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
		perror("epoll_ctl: server socket");
		exit(EXIT_FAILURE);
	}

	maxClients = MAX_FILE_DESCRIPTORS - 2;
	client_t *c;

	int nfds, n;
	int videoOn = 0;

	for (;;) {

		if (numClients > 0 && videoOn == 0) { 
			printf("turn on video because clients=%d\n", numClients);
			videoOn = 1;
			enable_video(epfd, &video);
		} else if (numClients == 0 && videoOn == 1) {
			printf("turn off video because clients=%d\n", numClients);
			videoOn = 0;
			disable_video(epfd, &video);
		}

		nfds = epoll_wait(epfd, events, MAX_FILE_DESCRIPTORS, -1);
		if (nfds == -1) {
			perror("epoll_wait");
			exit(EXIT_FAILURE);
		}

		for (n = 0; n < nfds; ++n) {
			oev = (struct observed*)events[n].data.ptr;
			switch(oev->t) {

				case VIDEO:
					if (events[n].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
						perror("error on video");
						exit(EXIT_FAILURE);
					}
					handle_new_frame(&clients);
					break;

				case SERVER:
					if (events[n].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
						perror("error on server");
						exit(EXIT_FAILURE);
					}
					if (events[n].events & EPOLLIN) {
						add_clients(epfd, server_fd, &clients);
					}
					break;

				case CLIENT:
					c = oev->data.client;
					if (events[n].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP))
							goto removeClient;

					if (events[n].events & EPOLLOUT) { 
						if (c->is_auth && client_tx(c) < 0)
							goto removeClient;
					}

					if (events[n].events & EPOLLIN) {
						if (!c->is_auth) {
							int done = client_parse_request(c);
							if (done > 0) {
								if (memcmp(argv[4], c->rxbuf + c->start_token, c->end_token - c->start_token) == 0) {
									printf("client auth OK %s %d\n", c->hostname, c->port);
									c->is_auth = 1;
									client_enqueue_frame(c, (uint8_t*)welcome, welcome_len);
								} else {
									printf("client auth KO %s %d\n", c->hostname, c->port);
									client_enqueue_frame(c, (uint8_t*)welcome_ko, welcome_ko_len);
								}
							} else if (done < 0) {
								goto removeClient;
							}
						} else {
							// discard bytes
							int r;
							while ((r = read(c->fd, c->rxbuf, MAXSIZE)) > 0);
							if (r < 0) {
								if (errno == EAGAIN || errno == EWOULDBLOCK)
									continue;
							}
							goto removeClient;
						}
					}
					break;
			}
			continue;
	removeClient:
			remove_client(epfd, oev);
		}
		
		
	}

	return 0;
}
