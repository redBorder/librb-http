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
		printf ("MESSAGE: %s\n", buff);

	if (opaque != NULL)
		printf ("OPAQUE: %p\n", opaque);

	free (buff);

	(void) rb_http_handler;
	(void) bufsiz;
	(void) status_code_str;
}

static void sigint_handler () {
	rb_http_get_reports (handler, my_callback, 100);
	running = 0;
};

int main() {
	char *url = strdup ("http://localhost:8080/librb-http/");
	long max_connections = 4L;
	char string[128];

	struct sigaction action;
	memset (&action, 0, sizeof (action));
	action.sa_handler = &sigint_handler;
	sigaction (SIGINT, &action, NULL);

	handler = rb_http_handler (url, max_connections, 512, NULL, 0);
	printf ("This will send 1024 messages\n");
	int i = 0;

	// getchar();

	for (i = 0 ; i < 1024 && running; i++) {
		sprintf (string, "{\"message\": \"%d\"}", i);
		while (rb_http_produce (handler, strdup (string), strlen (string), 0,
		                        NULL, 0, NULL) > 0 && running) {
			printf ("Queue is full, sleeping 1s\n");
			sleep (1);
		}
	}

	while (rb_http_get_reports (handler, my_callback, 100) && running);

	rb_http_handler_destroy (handler, NULL, 0);

	return 0;
}