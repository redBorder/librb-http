#include "../src/rb_http_handler.h"

#define N_MESSAGE 10000

int main() {
  const char url[] = "http://localhost:8080";

  struct rb_http_handler_s *handler = rb_http_handler_create(url, NULL, 0);
  assert(handler);

  // Starts a thread to process requests
  rb_http_handler_run(handler);

  // Send messages multiple messages
  for (int i = 0; i < N_MESSAGE; i++) {
    char *message = strdup("Sample message");
    // rb_http_produce() does not block
    if (!rb_http_produce(handler, message, strlen(message), 0, NULL, 0, NULL)) {
      free(message);
    }
  }

  // Terminates the handle waiting for all messages to get a response
  rb_http_handler_destroy(handler, NULL, 0);

  return 0;
}
