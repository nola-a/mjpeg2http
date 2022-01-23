#
#  mjpeg2http
#
#  Copyright (c) 2022 Antonino Nolano. Licensed under the MIT license, as
#  follows:
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to
#  deal in the Software without restriction, including without limitation the
#  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
#  sell copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in
#  all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
#  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
#  IN THE SOFTWARE.

CC=gcc
CFLAGS=-Wall -O3
TIMESTAMP=$(shell date +'%Y%m%d%H%M%S')

.PHONY: all clean debug run dump valgrind format

all: mjpeg2http

mjpeg2http: main.o video.o client.o server.o
	$(CC) -o mjpeg2http video.o client.o server.o main.o

clean:
	rm -f mjpeg2http client.o server.o video.o main.o dump2file dump2file.o

debug: mjpeg2http
	gdb --args ./mjpeg2http 192.168.1.2 8080 /dev/video0 mytoken

run: mjpeg2http
	./mjpeg2http 192.168.1.2 8080 /dev/video0 mytoken /tmp/mjpeg2http_oneshottoken

dump: dump2file
	mkdir -p /tmp/mjpeg2http_dump/$(TIMESTAMP)
	./dump2file /dev/video0 /tmp/mjpeg2http_dump/$(TIMESTAMP)/frame_

dump2file: dump2file.o video.o
	$(CC) -o dump2file video.o dump2file.o

valgrind: mjpeg2http
	valgrind --leak-check=yes ./mjpeg2http 192.168.1.2 8080 /dev/video0 mytoken /tmp/mjpeg2http_oneshottoken

format:
	clang-format -i -style=LLVM *.c *.h

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

