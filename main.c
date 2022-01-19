/**
 *  mjpeg2http
 *
 *  Copyright (c) 2022 Antonino Nolano. Licensed under the MIT license, as follows:
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
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "video.h"
#include "server.h"
#include "client.h"
#include "list.h"
#include "protocol.h"
#include "constants.h"

enum type {
	SERVER,
	CLIENT,
	VIDEO,
	TOKEN
};

union observed_data {
	client_t* client;
	int fd;
};

struct observed {
	struct dlist node;
	enum type t;
	union observed_data data;
};

char g_jpeg_image[MAX_FRAME_SIZE];
char g_frame_complete[MAX_FRAME_SIZE];
int g_numClients;
const int g_maxClients = MAX_FILE_DESCRIPTORS - 2;
char g_token[NUMBER_OF_TOKEN * (TOKEN_SIZE + 1)];
int g_token_pos = -1;
int g_videoOn = 0;
int g_frame_complete_len;
int g_file_fd;

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

void add_clients(int epfd, int server_fd, struct dlist* clients, struct observed* video)
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
	if (g_numClients > 0 && g_videoOn == 0) {
		printf("turn on video because clients=%d\n", g_numClients);
		g_videoOn = 1;
		enable_video(epfd, video);
	}
}

void remove_client(int epfd, struct observed* oc, struct observed* video)
{
	printf("remove client %s %d fd=%d\n", oc->data.client->hostname, oc->data.client->port, oc->data.client->fd);
	if (epoll_ctl(epfd, EPOLL_CTL_DEL, oc->data.client->fd, NULL) == -1) {
		perror("epoll_ctl: remove_client");
		exit(EXIT_FAILURE);
	}
	client_free(oc->data.client);
	list_del(&oc->node);
	free(oc);
	if (--g_numClients == 0 && g_videoOn == 1) {
		printf("turn off video because clients=%d\n", g_numClients);
		g_videoOn = 0;
		disable_video(epfd, video);
	}
}

int g_idx = 0;

void dump_frame(uint8_t* jpeg_image, uint32_t len)
{
	char output[1000];
	snprintf(output, 1000, "/tmp/frames/test_%d.jpeg", g_idx++);
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

void prepare_frame(uint8_t* jpeg_image, uint32_t len)
{
	struct timeval timestamp;
	gettimeofday(&timestamp, NULL);
	int total = snprintf(g_frame_complete, MAX_FRAME_SIZE, frame_header, len, (int)timestamp.tv_sec, (int)timestamp.tv_usec);
	memcpy(g_frame_complete + total, jpeg_image, len);
	memcpy(g_frame_complete + total + len, end_frame, end_frame_len);
	g_frame_complete_len =  total + len + end_frame_len;
}

void handle_new_frame(struct dlist* clients) 
{
	if (video_read_jpeg(prepare_frame, MAX_FRAME_SIZE) > 0) {
		struct dlist *itr;
		list_iterate(itr, clients) {
			struct observed* oc = list_get_entry(itr, struct observed, node);
			if (oc->data.client->is_auth)
				client_enqueue_frame(oc->data.client, (uint8_t*)g_frame_complete, g_frame_complete_len);
		}
	}

}

void create_pipe(char *name, int epfd)
{
	mkfifo(name, 0777);
	int pipe_fd = open(name, O_RDWR| O_TRUNC);
	struct observed* pipe = malloc(sizeof(struct observed));
	pipe->data.fd = pipe_fd;
	pipe->t = TOKEN;
	struct epoll_event ev;
	ev.events =  EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLERR | EPOLLHUP;
	ev.data.ptr = pipe;
	fcntl(pipe_fd, F_SETFL, O_NONBLOCK);
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, pipe_fd, &ev) == -1) {
		perror("epoll_ctl: add pipe");
		exit(EXIT_FAILURE);
	}
} 

int handle_token(int fd)
{
	int r;
	g_token_pos %= NUMBER_OF_TOKEN * (TOKEN_SIZE + 1);
	while ((r = read(fd, g_token + g_token_pos, NUMBER_OF_TOKEN * (TOKEN_SIZE + 1) - g_token_pos)) > 0) {
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

int check_token(const char* auth, uint8_t* start, int count)
{
	if (memcmp(auth, start, count) == 0)
		return 1;

	for(int i = 0; i < NUMBER_OF_TOKEN * (TOKEN_SIZE + 1) && g_token_pos != -1; i += TOKEN_SIZE + 1) {
		if (memcmp(g_token + i, start, count) == 0) {
			memset(g_token + i, 0, count);
			return 1;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 5) {
		printf("usage example: ./mjpeg2http 192.168.2.1 8080 /dev/video0 this_is_token [/tmp/mjpeg2http_onetimetoken]\n");
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

	if (argc == 6) {
		g_token_pos = 0;
		create_pipe(argv[5], epfd);
	}

	client_t *c;
	int nfds, n;
	declare_list(clients);
	enable_video(epfd, &video);

	for (;;) {

		nfds = epoll_wait(epfd, events, MAX_FILE_DESCRIPTORS, -1);
		if (nfds == -1) {
			perror("epoll_wait");
			exit(EXIT_FAILURE);
		}

		for (n = 0; n < nfds; ++n) {
			oev = (struct observed*)events[n].data.ptr;
			switch(oev->t) {
	
				case TOKEN:
					handle_token(oev->data.fd);
					break;

				case VIDEO:
					if (events[n].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
						perror("error on video");
						exit(EXIT_FAILURE);
					}
					video_read_jpeg(dump_frame, MAX_FRAME_SIZE);
					break;

				case SERVER:
					if (events[n].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
						perror("error on server");
						exit(EXIT_FAILURE);
					}
					if (events[n].events & EPOLLIN) {
						add_clients(epfd, server_fd, &clients, &video);
					}
					break;

				case CLIENT:
					c = oev->data.client;
					if (events[n].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
						printf("generic error fd=%d\n", c->fd);
						remove_client(epfd, oev, &video);
						break;
					}

					if (events[n].events & EPOLLOUT) { 
						if (client_tx(c) < 0) {
							remove_client(epfd, oev, &video);
							break;
						}
					}

					if (events[n].events & EPOLLIN) {
						if (!c->is_auth) {
							int done = client_parse_request(c);
							if (done > 0) {
								if (check_token(argv[4], c-> rxbuf + c->start_token, c->end_token - c->start_token)) {
									printf("client auth OK %s %d\n", c->hostname, c->port);
									c->is_auth = 1;
									client_enqueue_frame(c, (uint8_t*)welcome, welcome_len);
								} else {
									printf("client auth KO %s %d\n", c->hostname, c->port);
									client_enqueue_frame(c, (uint8_t*)welcome_ko, welcome_ko_len);
									remove_client(epfd, oev, &video);
								}
							} else if (done < 0) {
								remove_client(epfd, oev, &video);
							}
						}
					}
					break;
			}
		}
	}

	return 0;
}
