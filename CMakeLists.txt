cmake_minimum_required (VERSION 2.8.11)
project (mjpeg2http C)

add_executable(mjpeg2http
  server.c
  main.c
  client.c
  video.c
)

