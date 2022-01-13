CC=gcc
CFLAGS=-Wall -O3

all: mjpeg2http

video.o: video.c video.h
	$(CC) $(CFLAGS) -c video.c

client.o: client.c client.h
	$(CC) $(CFLAGS) -c client.c

server.o: server.c server.h
	$(CC) $(CFLAGS) -c server.c

mjpeg2http: main.o video.o client.o server.o
	$(CC) -o mjpeg2http video.o client.o server.o main.o

main.o: main.c protocol.h
	$(CC) $(CFLAGS) -c main.c

clean:
	rm -f mjpeg2http client.o server.o video.o main.o

debug: mjpeg2http
	gdb --args ./mjpeg2http 192.168.1.2 8080 /dev/video0 mytoken

run: mjpeg2http
	./mjpeg2http 192.168.1.2 8080 /dev/video0 mytoken

