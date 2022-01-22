.PHONY: all clean debug run dump valgrind format

CC=gcc
CFLAGS=-Wall -O3
TIMESTAMP=$(shell date +'%Y%m%d%H%M%S')

all: mjpeg2http

video.o: video.c video.h
	$(CC) $(CFLAGS) -c video.c

client.o: client.c client.h constants.h
	$(CC) $(CFLAGS) -c client.c

server.o: server.c server.h
	$(CC) $(CFLAGS) -c server.c

dump2file.o: dump2file.c protocol.h constants.h
	$(CC) $(CFLAGS) -c dump2file.c

mjpeg2http: main.o video.o client.o server.o
	$(CC) -o mjpeg2http video.o client.o server.o main.o

main.o: main.c protocol.h constants.h
	$(CC) $(CFLAGS) -c main.c

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

