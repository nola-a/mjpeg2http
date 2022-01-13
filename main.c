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
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>

#include "video.h"
#include "server.h"
#include "client.h"
#include "list.h"
#include "protocol.h"

#define MAX_FRAME_SIZE 200000
#define MAX_FILE_DESCRIPTORS 32
#define WEBCAM_IDX 1
#define WEBSERVER_IDX 0

extern const char* welcome;
extern const int welcome_len;
extern const char* welcome_ko;
extern const int welcome_ko_len;
extern const char* frame_header;
extern const char* end_frame;
extern const int end_frame_len;

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc != 5) {
		printf("usage example: ./mjpeg2http 192.168.2.1 8080 /dev/video0 this_is_token\n");
		return 1;
	}

	char jpeg_image[MAX_FRAME_SIZE];
	char frame_complete[MAX_FRAME_SIZE];

	int server_fd = server_create(argv[1], atoi(argv[2]));
	int video_fd = video_init(argv[3], 640, 480, 30);
	declare_list(clients);

	struct pollfd fds[MAX_FILE_DESCRIPTORS];
	fds[WEBSERVER_IDX].fd = server_fd;
	fds[WEBSERVER_IDX].events = POLLIN;
	fds[WEBCAM_IDX].events = POLLIN;

	const int maxClients = MAX_FILE_DESCRIPTORS - 2;

	int ready, nclients;
	struct timeval timestamp;

	while (1) {

		for (int i = 0; i < MAX_FILE_DESCRIPTORS; ++i)
			fds[i].revents = 0;

		nclients = client_register_fds(&clients, fds + 2);
		if (nclients > 0)
			fds[WEBCAM_IDX].fd = video_fd;
		else
			fds[WEBCAM_IDX].fd = -1; // not clients -> no interest in frames

		ready = poll(fds, nclients + 2, -1);
		if (ready < 0) {
			if (errno == EINTR)
				continue;
			else {
				perror("poll");
				exit(EXIT_FAILURE);
			}
		}

		if (fds[WEBSERVER_IDX].revents & POLLIN) {
			// got a new connection
			struct remotepeer rpeer;
			if (server_new_peer(server_fd, &rpeer) > 0) {
				if (nclients <= maxClients - 1) {
					client_t* c = client_init(rpeer.hostname, rpeer.port, rpeer.fd);
					list_add_right(&c->node, &clients);
				} else {
					printf("reject new connection => increase MAX_FILE_DESCRIPTORS\n");
					close(rpeer.fd);
				}
			}
		}

		if (fds[WEBCAM_IDX].revents & POLLIN) {
			// new frame is ready
			uint32_t n = video_read_jpeg(jpeg_image, MAX_FRAME_SIZE);
			if (n > 0) {
				gettimeofday(&timestamp, NULL);
				int total = snprintf(frame_complete, MAX_FRAME_SIZE, frame_header, n, (int)timestamp.tv_sec, (int)timestamp.tv_usec);
				memcpy(frame_complete + total, jpeg_image, n);
				memcpy(frame_complete + total + n, end_frame, end_frame_len);
				struct dlist *itr;
        			list_iterate(itr, &clients) {
        		       		client_t *c = list_get_entry(itr, client_t, node);
					if (c->is_auth) {
						client_enqueue_frame(c, (uint8_t*)frame_complete, total + n + end_frame_len);
					}
        			}
			}
		}
		
		// clients idle: looking for scheduled trasmission, dead clients, authentications......
		for (int i = 2; i < nclients + 2; ++i) {
			if (fds[i].revents & POLLOUT) {
				// there are outstanding data ready to transmit
				client_t* c = client_get_by_fd(&clients, fds[i].fd);
				if (c != NULL) {
					if (client_tx(c) < 0) {
						client_free(c);
						continue;
					}
					if (!c->is_auth && !client_are_pending_bytes(c))
						client_free(c);
				}
			}
			if (fds[i].revents & POLLIN) {
				client_t* c = client_get_by_fd(&clients, fds[i].fd);
				if (c != NULL) {
					if (!c->is_auth) {
						int done = client_parse_request(c);
						if (done > 0) {
							if (memcmp(argv[4], c->rxbuf + c->start_token, c->end_token - c->start_token) == 0) {
								c->is_auth = 1;
								client_enqueue_frame(c, (uint8_t*)welcome, welcome_len);
							} else {
								client_enqueue_frame(c, (uint8_t*)welcome_ko, welcome_ko_len);
							}
						} else if (done < 0) {
							client_free(c);
						}
						// done == 0 means not enough bytes for token
					} else {
						if (read(c->fd, c->rxbuf, MAXSIZE) < 0)
							client_free(c);
					}
				}
			}
			if (fds[i].revents & (POLLHUP | POLLERR)) {
				client_t* c = client_get_by_fd(&clients, fds[i].fd);
				if (c != NULL) {
					client_free(c);
				}
			}
		}
		
	}
	return 0;
}
