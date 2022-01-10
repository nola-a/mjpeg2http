# mjpeg2http

**A simple linux webcam http broadcaster**
<p>
<a href="https://raw.githubusercontent.com/uraimo/Bitter/master/LICENSE"><img src="http://img.shields.io/badge/license-MIT-blue.svg?style=flat" alt="License: MIT" /></a>
</p>


## Summary

mjpeg2http is a minimalistic server primarily targeted to run on embedded computers, like routers, raspberry pi, with a Linux operating system.

mjpeg2http converts jpeg image taken from /dev/video0 into http mjpeg stream. It can be used to stream JPEG files over an IP-based network from a webcam to various types of viewers such as Google Chrome, Mozilla Firefox, VLC, mplayer, and other software capable of receiving MJPG streams.

The implementation uses non blocking I/O and doesn't use threads.

if you need a more complete solution you can consider [mjpeg_streamer](https://github.com/jacksonliam/mjpg-streamer).


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
* mjpeg2http requires enough band to transmitt jpeg frames 
 
