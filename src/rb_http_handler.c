#include <stdio.h>
#include <curl/curl.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <librd/rdqueue.h>
#include <librd/rdlog.h>
#include <pthread.h>
#include <librd/rdthread.h>

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
static void * curl_send_message (void * arg);

/**
 * Initialize a handler for send messages to a server
 * @param  urls Comma separated URLs
 * @return NULL
 */
struct rb_http_handler_t * rb_http_handler (char * urls_str) {

	struct rb_http_handler_t * rb_http_handler = NULL;
	struct thread_arguments_t * thread_arguments = NULL;

	if (urls_str != NULL) {
		rb_http_handler = calloc (1, sizeof (struct rb_http_handler_t));
		thread_arguments = calloc (1, sizeof (struct thread_arguments_t));

		rd_fifoq_init (&rb_http_handler->msgs);
		rb_http_handler->urls = str_split (urls_str, ',');

		rb_http_handler->urls = calloc (1, sizeof (char*));
		rb_http_handler->urls[0] = urls_str;

		thread_arguments->msgs = &rb_http_handler->msgs;
		thread_arguments->url = rb_http_handler->urls[0];

		rd_threads_create ("curl_send_message", 4, NULL, curl_send_message,
		                   thread_arguments);

		return rb_http_handler;
	} else {
		return NULL;
	}
}

/**
 * Enqueues a message
 * @param message Message to be sent
 * @param options Options
 */
void rb_http_produce (struct rb_http_handler_t * handler,
                      char * message) {
	// printf ("PUSH: %s\n", message);
	rd_fifoq_add (&handler->msgs, message);
}

/**
 * [curl_send_message0  description]
 * @param  arg [description]
 * @return     [description]
 */
void * curl_send_message (void * arg) {
	int still_running = 1;
	int msgs_left;

	CURLM *multi_handle = NULL;
	CURL * handle = NULL;
	CURLMsg *msg = NULL;

	char * url = ((struct thread_arguments_t *) arg)->url;
	rd_fifoq_t	* rfq = ((struct thread_arguments_t *) arg)->msgs;
	rd_fifoq_elm_t * rfqe = NULL;

	multi_handle = curl_multi_init();

	if (arg != NULL) {
		while (1) {
			rfqe = rd_fifoq_pop (rfq);

			if (rfqe != NULL && rfqe->rfqe_ptr != NULL) {
				// printf ("POP: %s\n", rfqe->rfqe_ptr);
				handle =  curl_easy_init();
				curl_easy_setopt (handle, CURLOPT_URL, url);
				curl_easy_setopt (handle, CURLOPT_TIMEOUT_MS, 3000L);
				curl_easy_setopt (handle, CURLOPT_FAILONERROR, 1L);

				struct curl_slist * headers = NULL;
				headers = curl_slist_append (headers, "Accept: application/json");
				headers = curl_slist_append (headers,
				                             "Content-Type: application/json");
				headers = curl_slist_append (headers, "charsets: utf-8");

				curl_easy_setopt (handle, CURLOPT_HTTPHEADER, headers);
				curl_easy_setopt (handle, CURLOPT_POSTFIELDS, rfqe->rfqe_ptr);

				curl_multi_add_handle (multi_handle, handle);
				curl_multi_perform (multi_handle, &still_running);

				do {
					struct timeval timeout;
					int rc; /* select() return code */
					CURLMcode mc; /* curl_multi_fdset() return code */

					fd_set fdread;
					fd_set fdwrite;
					fd_set fdexcep;
					int maxfd = -1;

					long curl_timeo = -1;

					FD_ZERO (&fdread);
					FD_ZERO (&fdwrite);
					FD_ZERO (&fdexcep);

					/* set a suitable timeout to play around with */
					timeout.tv_sec = 1;
					timeout.tv_usec = 0;

					curl_multi_timeout (multi_handle, &curl_timeo);
					if (curl_timeo >= 0) {
						timeout.tv_sec = curl_timeo / 1000;
						if (timeout.tv_sec > 1)
							timeout.tv_sec = 1;
						else
							timeout.tv_usec = (curl_timeo % 1000) * 1000;
					}

					/* get file descriptors from the transfers */
					mc = curl_multi_fdset (multi_handle, &fdread, &fdwrite, &fdexcep, &maxfd);

					if (mc != CURLM_OK) {
						fprintf (stderr, "curl_multi_fdset() failed, code %d.\n", mc);
						break;
					}

					/* On success the value of maxfd is guaranteed to be >= -1. We call
					   select(maxfd + 1, ...); specially in case of (maxfd == -1) there are
					   no fds ready yet so we call select(0, ...) --or Sleep() on Windows--
					   to sleep 100ms, which is the minimum suggested value in the
					   curl_multi_fdset() doc. */

					if (maxfd == -1) {
						/* Portable sleep for platforms other than Windows. */
						struct timeval wait = { 0, 100 * 1000 }; /* 100ms */
						rc = select (0, NULL, NULL, NULL, &wait);
					} else {
						/* Note that on some platforms 'timeout' may be modified by select().
						   If you need access to the original value save a copy beforehand. */
						rc = select (maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout);
					}

					switch (rc) {
					case -1:
						/* select error */
						break;
					case 0: /* timeout */
					default: /* action */
						curl_multi_perform (multi_handle, &still_running);
						break;
					}
				} while (still_running);

				/* See how the transfers went */
				while ((msg = curl_multi_info_read (multi_handle, &msgs_left))) {
					if (msg->msg == CURLMSG_DONE) {
						printf ("HTTP transfer completed with status %d\n", msg->data.result);
					}
				}

				curl_easy_cleanup (handle);
				rd_fifoq_elm_release (rfq, rfqe);
			}
		}
	}
	curl_multi_cleanup (multi_handle);

	return NULL;
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