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


#ifndef SERVER_H
#define SERVER_H

#define SERVER_MAX 1024

struct remotepeer {
	char hostname[SERVER_MAX];
	int port;
	int fd;
};

int  server_create(char* hostname, int port);
int  server_new_peer(int fd, struct remotepeer* rpeer);

// web signatures
#define BOUNDARY "hdfahelfaelfalfvcjcfjfjfj"

#define STD_HEADER "Connection: close\r\n" \
    "Server: mjpeg2http/1.0\r\n" \
    "Cache-Control: no-store, no-cache, must-revalidate, pre-check=0, post-check=0, max-age=0\r\n" \
    "Pragma: no-cache\r\n" \
    "Expires: Mon, 3 Jan 2000 12:34:56 GMT\r\n"

#define FIRST_MESSAGE "HTTP/1.0 200 OK\r\n" \
		      "Access-Control-Allow-Origin: *\r\n" \
		      STD_HEADER \
		      "Content-Type: multipart/x-mixed-replace;boundary=" BOUNDARY "\r\n" \
		      "\r\n" \
		      "--" BOUNDARY "\r\n"

#define FRAME_HEADER "Content-Type: image/jpeg\r\n" \
                     "Content-Length: %d\r\n" \
                     "X-Timestamp: %d.%06d\r\n" \
                     "\r\n"

#define END_FRAME "\r\n--" BOUNDARY "\r\n"

#define UNAUTHORIZED_MESSAGE "HTTP/1.1 401 Unauthorized\r\n" \
		      "Access-Control-Allow-Origin: *\r\n" \
	              "Connection: close\r\n" \
		      "\r\n"

#endif
