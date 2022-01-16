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
#include "constants.h"

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

char g_jpeg_image[MAX_FRAME_SIZE];
char g_frame_complete[MAX_FRAME_SIZE];
int g_numClients;
const int g_maxClients = MAX_FILE_DESCRIPTORS - 2;

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
		if (g_numClients <= g_maxClients - 1) {
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
			++g_numClients;
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
	--g_numClients;
}

void handle_new_frame(struct dlist* clients) 
{
	uint32_t n = video_read_jpeg(g_jpeg_image, MAX_FRAME_SIZE);
	if (n > 0) {
		struct timeval timestamp;
		gettimeofday(&timestamp, NULL);
		int total = snprintf(g_frame_complete, MAX_FRAME_SIZE, frame_header, n, (int)timestamp.tv_sec, (int)timestamp.tv_usec);
		memcpy(g_frame_complete + total, g_jpeg_image, n);
		memcpy(g_frame_complete + total + n, end_frame, end_frame_len);
		struct dlist *itr;
		list_iterate(itr, clients) {
			struct observed* oc = list_get_entry(itr, struct observed, node);
			if (oc->data.client->is_auth)
				client_enqueue_frame(oc->data.client, (uint8_t*)g_frame_complete, total + n + end_frame_len);
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
	if (argc != 5) {
		printf("usage example: ./mjpeg2http 192.168.2.1 8080 /dev/video0 this_is_token\n");
		return 1;
	}

	signal(SIGPIPE, SIG_IGN);
	setvbuf(stdout, NULL, _IONBF, 0);

	int epfd = epoll_create1(0);
	if (epfd == -1) {
		perror("epoll_create1");
		exit(EXIT_FAILURE);
	}

	int server_fd = server_create(argv[1], atoi(argv[2]));
	int video_fd = video_init(argv[3], WIDTH, HEIGHT, FRAME_PER_SECOND);

	struct epoll_event ev, events[MAX_FILE_DESCRIPTORS];
	struct observed video, server, *oev;

	video.data.fd = video_fd;
	video.t = VIDEO;

	// register webserver 
	server.data.fd = server_fd;
	server.t = SERVER;
	ev.events =  EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
	ev.data.ptr = &server;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
		perror("epoll_ctl: server socket");
		exit(EXIT_FAILURE);
	}

	client_t *c;
	int nfds, n, videoOn = 0;
	declare_list(clients);

	for (;;) {

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
						if (g_numClients > 0 && videoOn == 0) {
							printf("turn on video because clients=%d\n", g_numClients);
							videoOn = 1;
							enable_video(epfd, &video);
						}
					}
					break;

				case CLIENT:
					if (events[n].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP))
							goto removeClient;

					c = oev->data.client;
					if (events[n].events & EPOLLOUT) { 
						if (client_tx(c) < 0)
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
									goto removeClient;
								}
							} else if (done < 0) {
								goto removeClient;
							}
						}
					}
					break;
			}
			continue;
	removeClient:
			remove_client(epfd, oev);
			if (g_numClients == 0 && videoOn == 1) {
				printf("turn off video because clients=%d\n", g_numClients);
				videoOn = 0;
				disable_video(epfd, &video);
			}
		}
	}

	return 0;
}
