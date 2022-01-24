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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "client.h"

client_t *client_init(char *hostname, int port, int fd) {
  printf("new client %s %d fd=%d\n", hostname, port, fd);
  client_t *c = malloc(sizeof(client_t));
  c->hostname = strdup(hostname);
  c->port = port;
  c->fd = fd;
  init_list_entry(&c->tx_queue);
  c->total_to_sent = c->start_token = c->end_token = c->rxbuf_pos = c->is_auth =
      c->txbuf_pos = 0;
  return c;
}

int client_parse_request(client_t *client) {
  if (client->start_token != 0) {
    // token already decoded
    return 1;
  }

  int r;
  while ((r = read(client->fd, client->rxbuf + client->rxbuf_pos,
                   1000 - client->rxbuf_pos)) > 0)
    client->rxbuf_pos += r;

  if (r < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      const char *end = strchr((const char *)client->rxbuf, '\n');
      if (end != NULL) {
        // GET /whatever?myauthtoken HTTP/1.1
        // read enough bytes to get myauthtoken
        const char *sp = strchr((const char *)client->rxbuf, ' ');
        if (sp == NULL || end - sp < 2)
          return -1;
        const char *sq = strchr(sp + 1, '?');
        if (sq == NULL || end - sq < 2)
          return -1;
        const char *eq = strchr(sq + 1, ' ');
        if (eq == NULL)
          return -1;
        client->start_token = sq + 1 - (const char *)client->rxbuf;
        client->end_token = eq - (const char *)client->rxbuf;
        return 1;
      }

      return 0;
    }
    printf("rxbuf %d error %s\n", client->fd, strerror(errno));
    return -1;
  }

  return 0;
}

void client_free(client_t *client) {
  printf("destroy client %s %d fd=%d\n", client->hostname, client->port,
         client->fd);
  struct dlist *itr, *save;
  message_t *msg;
  list_iterate_safe(itr, save, &client->tx_queue) {
    list_del(&list_get_entry(itr, message_t, node)->node);
    msg = list_get_entry(itr, message_t, node);
    if (--*(msg->payload + msg->size) == 0)
      free(msg->payload);
    free(msg);
  }

  close(client->fd);
  free(client->hostname);
  free(client);
}

int client_write_txbuf(client_t *client) {
  int txbuf_pos;
  while ((txbuf_pos = write(client->fd, client->txbuf + client->txbuf_pos,
                            client->total_to_sent - client->txbuf_pos)) > 0) {
    // printf("raw: %.*s\n", txbuf_pos, client->txbuf + client->txbuf_pos);
    client->txbuf_pos += txbuf_pos;
  }

  if (client->txbuf_pos == client->total_to_sent) {
    //	printf("frame size=%d sent %s:%d\n", client->total_to_sent,
    // client->hostname, client->port);
    client->total_to_sent = client->txbuf_pos = 0;
  }

  if (txbuf_pos >= 0)
    return 1;

  if (errno == EAGAIN || errno == EWOULDBLOCK) {
    return 0;
  }

  printf("txbuf fd=%d error %s\n", client->fd, strerror(errno));

  return -1;
}

int client_tx(client_t *client) {

  int r;

  do {
    r = 0;
    if (client->total_to_sent > 0) {
      r = client_write_txbuf(client);
    } else if (!list_empty(&client->tx_queue)) {
      // printf("move message from tx queue to tx buffer\n");
      message_t *msg =
          list_get_entry(list_get_first(&client->tx_queue), message_t, node);
      list_del(&msg->node);
      memcpy(client->txbuf, msg->payload, msg->size);
      client->total_to_sent = msg->size;
      client->txbuf_pos = 0;
      if (--*(msg->payload + msg->size) == 0)
        free(msg->payload);
      free(msg);
      r = client_write_txbuf(client);
    }
  } while (r > 0);

  return r;
}

void client_enqueue_frame(client_t *client, uint8_t *payload, int size,
                          uint8_t **allocated) {
  int tx_queue_size = 0;
  list_size(tx_queue_size, &client->tx_queue);
  // printf("enqueue message size=%d, still to sent=%d tx_queue_size=%d\n",
  // size, client->total_to_sent - client->txbuf_pos, tx_queue_size);

  if (tx_queue_size || client->total_to_sent) {
    if (tx_queue_size > TX_QUEUE_MAX || allocated == NULL) {
      printf("tx queue %s %d-> drop message because current size %d\n",
             client->hostname, client->port, tx_queue_size);
      return;
    }
    // printf("place message into queue\n");
    message_t *msg = malloc(sizeof(message_t));
    if (*allocated == NULL) {
      *allocated = malloc(size + 1);
      *(*allocated + size) = 0x02;
      memcpy(*allocated, payload, size);
    } else {
      ++*(*allocated + size);
    }
    msg->payload = *allocated;
    msg->size = size;
    init_list_entry(&msg->node);
    list_add_right(&msg->node, &client->tx_queue);
    client_tx(client);
  } else {
    int pos, sent = 0;
    while ((pos = write(client->fd, payload + sent, size - sent)) > 0)
      sent += pos;

    if (sent < size) {
      //	printf("place remaining bytes %d of %d into buffer tx\n", size -
      // sent, size);
      memcpy(client->txbuf + sent, payload + sent, size - sent);
      client->total_to_sent = size;
      client->txbuf_pos = sent;
    }
  }
}
