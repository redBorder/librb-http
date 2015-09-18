#include <stdio.h>
#include <curl/curl.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <librd/rdqueue.h>
#include <librd/rdlog.h>
#include <pthread.h>

#include "librb-http.h"

////////////////////
// Structures
////////////////////
struct rb_http_handler_t {
	char ** urls;
	rd_fifoq_t msgs;
};

struct message_t {
	uint8_t * payload;
	size_t len;
};

struct thread_arguments_t {
	rd_fifoq_t * msgs;
	char * url;
};

////////////////////
// Private functions
////////////////////
static char** str_split (char* a_str, const char a_delim);
static void * curl_send_message0 (void * arg);
static void curl_send_message();


/**
 * Initialize a handler for send messages to a server
 * @param  urls Comma separated URLs
 * @return NULL
 */
struct rb_http_handler_t * rb_http_handler (char * urls_str) {
	pthread_t worker;

	struct rb_http_handler_t * rb_http_handler = NULL;
	struct thread_arguments_t * thread_arguments = NULL;

	if (urls_str != NULL) {
		rb_http_handler = calloc (1, sizeof (struct rb_http_handler_t));
		thread_arguments = calloc (1, sizeof (struct thread_arguments_t));

		curl_global_init (CURL_GLOBAL_ALL);
		rd_fifoq_init (&rb_http_handler->msgs);

		// rb_http_handler->urls = str_split (urls_str, ',');
		rb_http_handler->urls = calloc (1, sizeof (char*));
		rb_http_handler->urls[0] = urls_str;

		thread_arguments->msgs = &rb_http_handler->msgs;
		thread_arguments->url = rb_http_handler->urls[0];

		pthread_create (&worker, NULL, &curl_send_message0, thread_arguments);

		return rb_http_handler;
	} else {
		return NULL;
	}
}

/**
 * Global initialize CURL
 */
void rb_http_init() {
	curl_global_init (CURL_GLOBAL_ALL);
}

/**
 * Global cleanup CURL
 */
void rb_http_clean() {
	curl_global_cleanup ();
}

/**
 * Enqueues a message
 * @param message Message to be sent
 * @param options Options
 */
void rb_http_produce (struct rb_http_handler_t * handler,
                      char * message) {
	rd_fifoq_add (&handler->msgs, message);
}

/**
 * [curl_send_message0  description]
 * @param  arg [description]
 * @return     [description]
 */
void * curl_send_message0 (void * arg) {
	rd_fifoq_elm_t * rfqe = NULL;

	while (1) {
		if (arg != NULL) {
			rfqe = rd_fifoq_pop_wait (((struct thread_arguments_t *) arg)->msgs);
			if (rfqe != NULL) {
				if (rfqe->rfqe_ptr != NULL) {
					printf ("Leído de la cola mensaje: [%s]\n", (char*) rfqe->rfqe_ptr);
					curl_send_message (rfqe->rfqe_ptr, ((struct thread_arguments_t *) arg)->url);
				}
			}
		}
	}

	return NULL;
}

/**
 * [rb_http_send  description]
 * @param  handler [description]
 * @return         [description]
 */
void curl_send_message (char * message, char * url) {
	CURL * curl;
	CURLcode res_code = 0;

	curl = curl_easy_init();

	if (curl) {
		// Set remote host and write_callback
		curl_easy_setopt (curl, CURLOPT_URL, url);
		curl_easy_setopt (curl, CURLOPT_TIMEOUT_MS, 3000L);
		curl_easy_setopt (curl, CURLOPT_FAILONERROR, 1L);

		// Set request headers
		struct curl_slist * headers = NULL;
		headers = curl_slist_append (headers, "Accept: application/json");
		headers = curl_slist_append (headers,
		                             "Content-Type: application/json");
		headers = curl_slist_append (headers, "charsets: utf-8");
		curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headers);

		// Set request payload
		curl_easy_setopt (curl, CURLOPT_POSTFIELDS, message);

		//  Perform the request, res will get the return code
		res_code = curl_easy_perform (curl);

		// Check for errors
		if (res_code != CURLE_OK) {
			rdlog (LOG_ERR, "FAIL: %s", curl_easy_strerror (res_code));
		}

		// Always cleanup
		curl_easy_cleanup (curl);
	}
}

/**
 * Split a string (and specify the delimiter to use)
 * @param  a_str   Single sting separated by delimiter
 * @param  a_delim Delimiter
 * @return         Array of strings separated
 */
char** str_split (char* a_str, const char a_delim) {
	char** result    = 0;
	size_t count     = 0;
	char* tmp        = a_str;
	char* last_comma = 0;
	char delim[2];
	delim[0] = a_delim;
	delim[1] = 0;

	/* Count how many elements will be extracted. */
	while (*tmp) {
		if (a_delim == *tmp) {
			count++;
			last_comma = tmp;
		}
		tmp++;
	}

	/* Add space for trailing token. */
	count += last_comma < (a_str + strlen (a_str) - 1);

	/* Add space for terminating null string so caller
	   knows where the list of returned strings ends. */
	count++;

	result = malloc (sizeof (char*) * count);

	if (result) {
		size_t idx  = 0;
		char* token = strtok (a_str, delim);

		while (token) {
			assert (idx < count);
			* (result + idx++) = strdup (token);
			token = strtok (0, delim);
		}
		assert (idx == count - 1);
		* (result + idx) = 0;
	}

	return result;
}