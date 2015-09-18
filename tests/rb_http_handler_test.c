#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "../src/librb-http.h"

static void test_rb_http_handler_url (void ** state) {
	(void) state;

	struct rb_http_handler_t * handler = NULL;
	char * url = strdup ("http://localhost:8080/librb-http");

	handler = rb_http_handler (url);

	assert_non_null (handler);
	free (url);
}

static void test_rb_http_handler_url_null (void ** state) {
	(void) state;

	struct rb_http_handler_t * handler = NULL;

	handler = rb_http_handler (NULL);

	assert_null (handler);
}

int main (void) {

	const struct CMUnitTest tests[] = {
		cmocka_unit_test (test_rb_http_handler_url),
		cmocka_unit_test (test_rb_http_handler_url_null)
	};

	return cmocka_run_group_tests (tests, NULL, NULL);
}