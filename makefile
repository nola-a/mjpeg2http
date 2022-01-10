CC=gcc
CFLAGS=-Wall -O3

all: mjpeg2http

video.o: video.c
	$(CC) $(CFLAGS) -c video.c

client.o: client.c
	$(CC) $(CFLAGS) -c client.c

server.o: server.c
	$(CC) $(CFLAGS) -c server.c

mjpeg2http: main.o video.o client.o server.o
	$(CC) -o mjpeg2http video.o client.o server.o main.o

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

clean:
	rm -f mjpeg2http client.o server.o video.o main.o

debug: mjpeg2http
	gdb --args ./mjpeg2http 192.168.1.2 8080 /dev/video0 mytoken

run: mjpeg2http
	./mjpeg2http 192.168.1.2 8080 /dev/video0 mytoken

