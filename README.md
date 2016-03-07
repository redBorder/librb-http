# librb-http

Non-blocking high-level wrapper for libcurl.

## Example

~~~c
#include "rb_http_handler.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define RB_HTTP_NORMAL_MODE "0"
#define RB_HTTP_CHUNKED_MODE "1"

#define MESSAGE                                                                \
  "{\"client_mac\": \"54:26:96:db:88:01\", \"application_name\": \"wwww\", "   \
  "\"sensor_uuid\":\"abc\", \"a\":5}"
#define N_MESSAGE 100 * 1
#define URL "http://localhost:8080"

struct rb_http_handler_s *handler = NULL;

static void my_callback(struct rb_http_handler_s *rb_http_handler,
                        int status_code, long http_status,
                        const char *status_code_str, char *buff, size_t bufsiz,
                        void *opaque) {

  (void)rb_http_handler;
  (void)bufsiz;
  (void)status_code_str;

  if (status_code != 0) {
    printf("CURL CODE: %d\n", status_code);
  }

  if (status_code == 0) {
    printf("HTTP STATUS: %ld\n", http_status);
  }

  if (buff != NULL) {
    printf("MESSAGE: %s\n\n", buff);
  }

  if (opaque != NULL) {
    printf("OPAQUE: %p\n", opaque);
  }
}

void *get_reports(void *ptr) {
  (void)ptr;

  while (1) {
    rb_http_get_reports(handler, my_callback, 500);
  }

  return NULL;
}

int main() {

  handler = rb_http_handler_create(URL, NULL, 0);
  rb_http_handler_set_opt(handler, "HTTP_VERBOSE", "0", NULL, 0);
  rb_http_handler_set_opt(handler, "HTTP_CONNTTIMEOUT", "5000", NULL, 0);
  rb_http_handler_set_opt(handler, "HTTP_TIMEOUT", "15000", NULL, 0);
  rb_http_handler_set_opt(handler, "RB_HTTP_BATCH_TIMEOUT", "1000", NULL, 0);
  rb_http_handler_set_opt(handler, "RB_HTTP_MAX_MESSAGES", "10000", NULL, 0);
  rb_http_handler_set_opt(handler, "RB_HTTP_CONNECTIONS", "1", NULL, 0);
  rb_http_handler_set_opt(handler, "RB_HTTP_MODE", "2", NULL, 0);
  rb_http_handler_set_opt(handler, "RB_HTTP_DEFLATE", "1", NULL, 0);

  rb_http_handler_run(handler);

  printf("Sending %d messages\n", N_MESSAGE);
  int i = 0;

  pthread_t p_thread;

  pthread_create(&p_thread, NULL, &get_reports, NULL);

  for (i = 0; i < N_MESSAGE; i++) {
    char *message = calloc(20, sizeof(char));
    sprintf(message, "MESSAGE %d", i);
    while (rb_http_produce(handler, message, strlen(message),
                           RB_HTTP_MESSAGE_F_FREE, NULL, 0, NULL) > 0) {
      free(message);
    }
    usleep(500000);
  }

  pthread_join(p_thread, NULL);

  rb_http_handler_destroy(handler, NULL, 0);

  return 0;
}
~~~
