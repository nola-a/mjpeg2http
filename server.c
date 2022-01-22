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

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "constants.h"
#include "server.h"

int server_new_peer(int sfd, struct remotepeer *rpeer) {
  // accept
  struct sockaddr_storage remote;
  uint32_t addrlen = sizeof(struct sockaddr_storage);
  rpeer->fd = accept(sfd, (struct sockaddr *)&remote, &addrlen);
  if (rpeer->fd < 0) {
    return -1;
  }

  // set non block and tcp_nodelay
  fcntl(rpeer->fd, F_SETFL, O_NONBLOCK);
  int on = 1;
  if (0 != setsockopt(rpeer->fd, IPPROTO_TCP, TCP_NODELAY, (char *)&on,
                      sizeof(int))) {
    perror("error address setsock");
    return -1;
  }

  // get hostname and port
  rpeer->port = ntohs(((struct sockaddr_in *)&remote)->sin_port);
  if (NULL == inet_ntop(AF_INET, &((struct sockaddr_in *)&remote)->sin_addr,
                        rpeer->hostname, SERVER_MAX)) {
    perror("error address");
    return -1;
  }
  return 1;
}

int server_create(char *hostname, int port) {
  // create fd
  int error = 0;
  int socket_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (socket_fd < 0) {
    perror("error");
    return -1;
  }

  // build address
  struct sockaddr_storage address;
  address.ss_family = AF_INET;
  error = inet_pton(AF_INET, hostname,
                    &(((struct sockaddr_in *)&address)->sin_addr));
  if (error != 1) {
    perror("error address");
    return -1;
  }
  ((struct sockaddr_in *)&address)->sin_port = htons(port);

  // set reuse
  int yes = 1;
  if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) != 0) {
    perror("setsockopt error");
    return -1;
  }

  // bind address
  if (bind(socket_fd, (struct sockaddr *)&address, sizeof(struct sockaddr_in)) <
      0) {
    perror("bind error");
    return -1;
  }

  // listening and backlog
  if (listen(socket_fd, SERVER_LISTEN_BACKLOG) < 0) {
    perror("listen error");
    return -1;
  }

  // set nonblock
  fcntl(socket_fd, F_SETFL, O_NONBLOCK);

  return socket_fd;
}
