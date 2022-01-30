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

#include <malloc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "libmjpeg2http.h"

size_t allocated = 0, freed = 0;

void *my_malloc_hook(size_t, const void *);
void my_free_hook(void *, const void *);
void *my_realloc_hook(void *ptr, size_t size, const void *caller);

void *(*old_malloc_hook)(size_t, const void *);
void (*old_free_hook)(void *, const void *);
void (*old_realloc_hook)(void *ptr, size_t size, const void *caller);

void my_init(void) {
  old_malloc_hook = __malloc_hook;
  old_free_hook = __free_hook;
  old_realloc_hook = __realloc_hook;
  __malloc_hook = my_malloc_hook;
  __free_hook = my_free_hook;
  __realloc_hook = my_realloc_hook;
}

void *my_realloc_hook(void *ptr, size_t size, const void *caller) {
  ++allocated;
  void *result;
  /* Restore all old hooks */
  __malloc_hook = old_malloc_hook;
  __free_hook = old_free_hook;
  __realloc_hook = old_realloc_hook;
  /* Call recursively */
  result = realloc(ptr, size);
  /* Save underlying hooks */
  old_malloc_hook = __malloc_hook;
  old_free_hook = __free_hook;
  old_realloc_hook = __realloc_hook;
  /* Restore our own hooks */
  __malloc_hook = my_malloc_hook;
  __free_hook = my_free_hook;
  __realloc_hook = my_realloc_hook;
  return result;
}

void *my_malloc_hook(size_t size, const void *caller) {
  ++allocated;
  void *result;
  /* Restore all old hooks */
  __malloc_hook = old_malloc_hook;
  __free_hook = old_free_hook;
  __realloc_hook = old_realloc_hook;
  /* Call recursively */
  result = malloc(size);
  /* Save underlying hooks */
  old_malloc_hook = __malloc_hook;
  old_free_hook = __free_hook;
  old_realloc_hook = __realloc_hook;
  /* Restore our own hooks */
  __malloc_hook = my_malloc_hook;
  __free_hook = my_free_hook;
  __realloc_hook = my_realloc_hook;
  return result;
}

void my_free_hook(void *ptr, const void *caller) {
  ++freed;
  /* Restore all old hooks */
  __malloc_hook = old_malloc_hook;
  __free_hook = old_free_hook;
  __realloc_hook = old_realloc_hook;
  /* Call recursively */
  free(ptr);
  /* Save underlying hooks */
  old_malloc_hook = __malloc_hook;
  old_free_hook = __free_hook;
  old_realloc_hook = __realloc_hook;
  /* Restore our own hooks */
  __malloc_hook = my_malloc_hook;
  __free_hook = my_free_hook;
  __realloc_hook = my_realloc_hook;
}

void stop(void *p) {
  int seconds = *((int *)p);
  printf("call endloop sleep for %d seconds\n", seconds);
  sleep(seconds);
  fflush(stdout);
  libmjpeg2http_endLoop();
}

int main(int argc, char **argv) {
  if (argc < 5) {
    printf("usage example: ./mjpeg2http 192.168.2.1 8080 /dev/video0 "
           "this_is_token [/tmp/mjpeg2http_onetimetoken]\n");
    return 1;
  }
  my_init();

  char *tokenpipe = NULL;

  if (argc == 6) {
    tokenpipe = argv[5];
  }

  for (;;) {
    int t1 = 30;
    pthread_t stopper;
    pthread_create(&stopper, NULL, stop, &t1);
    pthread_detach(stopper);
    libmjpeg2http_loop(argv[1], atoi(argv[2]), argv[3], argv[4], tokenpipe);
    printf("mem allocated=%u freed=%u\n", allocated, freed);
  }

  return 0;
}
