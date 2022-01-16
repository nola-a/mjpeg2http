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

#ifndef CLIENT_H
#define CLIENT_H

#include <stdint.h>

#include "list.h"


#define MAXSIZE 200000

typedef struct {

	/* client data */
	char* hostname;
	int port;
	int fd;

	/* tx buffer */
	uint8_t txbuf[MAXSIZE];
	uint32_t txbuf_pos;
 	uint32_t total_to_sent;

	/* rx buffer */
	uint8_t rxbuf[MAXSIZE];
	int rxbuf_pos;

	/* tx queue */
	struct dlist tx_queue;

	int is_auth;
	int start_token, end_token;
} client_t;

typedef struct {
	struct dlist node;
	uint8_t* payload;
	int size;
} message_t;

client_t* client_init(char* hostname, int port, int fd);
void client_free(client_t *client);
int client_parse_request(client_t *client);
int client_tx(client_t *client);
int client_enqueue_frame(client_t *client, uint8_t *payload, int size);

#endif
