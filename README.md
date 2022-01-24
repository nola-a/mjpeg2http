# mjpeg2http

**A pure C Linux MJPEG over HTTP server**
<p>
<a href="https://raw.githubusercontent.com/nola-a/mjpeg2http/master/LICENSE"><img src="http://img.shields.io/badge/license-MIT-blue.svg?style=flat" alt="License: MIT" /></a>
</p>


## Summary

mjpeg2http is a minimalistic server primarily targeted to run on embedded computers, like routers, raspberry pi, with a Linux OS.

It can be used to stream JPEG files over an IP-based network from a webcam to various types of viewers such as Google Chrome, Mozilla Firefox, VLC, mplayer, and other software capable of receiving MJPG streams.

The implementation uses epoll on non-blocking file descriptors and is not thread-based.

## Build using make

Install dependencies:
```bash
$ sudo apt install build-essential libv4l-dev
```

Compile with:

```bash
$ make
```

## Build using cmake

Compile with:

```bash
$ mkdir build
$ cmake ..
$ make
```

## Usage

Run:

```bash
$ ./mjpeg2http 192.168.2.1 8080 /dev/video0 my_secret_token
 
```

Open browser on http://192.168.2.1:8080/path?my_secret_token

## One time token

Run:

```bash
$ ./mjpeg2http 192.168.2.1 8080 /dev/video0 my_secret_token /tmp/mjpeg2http_token
 
```
Book access:

```bash
$ echo "12345678901234567890" > /tmp/mjpeg2http_token
 
```

Open browser on http://192.168.2.1:8080/path?12345678901234567890

The token will be valid exactly for one access after that it gets invalid

## Warning
+ mjpeg2http should be used in private network because it does not use TLS connections. If you would like to use it while on a public network it is highly recommended to use TLS, some ideas:
    - you can try [stunnel](https://www.stunnel.org/).
    - nginx or apache httpd placed in front of mjpeg2http with a reverse proxy.
    - for encryption between mjpeg2http and server it can be used ssh tunnels or wireguard.
+ token length must be exactly TOKEN_SIZE (see constants.h) 
 
