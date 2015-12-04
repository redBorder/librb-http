#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <signal.h>
#include <librd/rdthread.h>
#include <librbhttp/librb-http.h>

static int running = 1;
struct rb_http_handler_s *handler = NULL;

static void my_callback (struct rb_http_handler_s *rb_http_handler,
                         int status_code,
                         long http_status,
                         const char *status_code_str,
                         char *buff,
                         size_t bufsiz,
                         void *opaque) {

	if (status_code != 0) {
		printf ("CURL CODE: %d\n", status_code);
	}

	if (status_code == 0) {
		printf ("HTTP STATUS: %ld\n", http_status);
	}

	if (buff != NULL)
		printf ("MESSAGE: %s\n\n", buff);

	if (opaque != NULL)
		printf ("OPAQUE: %p\n", opaque);

	(void) rb_http_handler;
	(void) bufsiz;
	(void) status_code_str;
}

int main() {
	char url[] = "http://localhost:8080/librb-http/";
	char string[128];

	handler = rb_http_handler_create (url, NULL, 0);
	// rb_http_handler_set_opt(handler, "HTTP_TIMEOUT", "10000", NULL, 0);
	// rb_http_handler_set_opt(handler, "HTTP_CONNTTIMEOUT", "5000", NULL, 0);
	// rb_http_handler_set_opt(handler, "HTTP_VERBOSE", "0", NULL, 0);
	// rb_http_handler_set_opt(handler, "RB_HTTP_MAX_MESSAGES", "512", NULL, 0);
	rb_http_handler_set_opt(handler, "RB_HTTP_CONNECTIONS", "1", NULL, 0);
	rb_http_handler_set_opt(handler, "RB_HTTP_MODE", "1", NULL, 0);

	rb_http_handler_run(handler);

	printf ("Sending 1024 messages\n");
	int i = 0;

	// getchar();
	char *message = NULL;

	for (i = 0 ; i < 100000 && running; i++) {
		sprintf (string, "{\"message\": \"%d\"}", i);
		while (rb_http_produce (handler,
		                        message = strdup (string),
		                        strlen (string),
		                        RB_HTTP_MESSAGE_F_FREE,
		                        NULL,
		                        0,
		                        NULL) > 0 && running) {
			rb_http_get_reports (handler, my_callback, 100);
			free(message);
		}
	}

	while (rb_http_get_reports (handler, my_callback, 100) && running);

	rb_http_handler_destroy (handler, NULL, 0);

	return 0;
}