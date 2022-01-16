# mjpeg2http

**A simple linux MJPEG over HTTP server**
<p>
<a href="https://raw.githubusercontent.com/uraimo/Bitter/master/LICENSE"><img src="http://img.shields.io/badge/license-MIT-blue.svg?style=flat" alt="License: MIT" /></a>
</p>


## Summary

mjpeg2http is a minimalistic server primarily targeted to run on embedded computers, like routers, raspberry pi, with a Linux OS.

It can be used to stream JPEG files over an IP-based network from a webcam to various types of viewers such as Google Chrome, Mozilla Firefox, VLC, mplayer, and other software capable of receiving MJPG streams.

The implementation uses epoll on non-blocking file descriptors and is not thread-based.

## Build

Install dependencies:
```bash
$ sudo apt install build-essential libv4l-dev
```

Compile with:

```bash
$ make
```

## Usage

Run:

```bash
$ ./mjpeg2http 192.168.2.1 8080 /dev/video0 my_secret_token
 
```

Open browser on http://192.168.2.1:8080/path?my_secret_token

## Warning
* mjpeg2http should be used in private network because it does not use TLS connections 
 
