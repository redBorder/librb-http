/**
 * @file rb_http_handler.c
 * @author Diego Fernández Barrera
 * @brief Main library.
 */
#include "../config.h"
#include "librb-http.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <librd/rdlog.h>
#include <pthread.h>

/**
 * @brief Creates a handler to produce messages.
 * @param  urls_str List of comma sepparated URLs. If the first URL doesn't work
 * it will try the next one.
 * @return          Handler for send messages to the provided URL.
 */
struct rb_http_handler_s *rb_http_handler_create (
    const char *urls_str,
    char *err,
    size_t errsize) {

	struct rb_http_handler_s *rb_http_handler = NULL;
	(void) err;
	(void) errsize;

	if (urls_str != NULL) {
		rb_http_handler = calloc (1, sizeof (struct rb_http_handler_s));

		rd_fifoq_init (&rb_http_handler->rfq);
		rd_fifoq_init (&rb_http_handler->rfq_reports);

		rb_http_handler->max_messages = DEFAULT_MAX_MESSAGES;
		rb_http_handler->timeout = DEFAULT_TIMEOUT;
		rb_http_handler->timeout = DEFAULT_CONTTIMEOUT;
		rb_http_handler->timeout = 0;

		rb_http_handler->url = strdup(urls_str);
		rb_http_handler->still_running = 0;
		rb_http_handler->msgs_left = 0;

		curl_global_init(CURL_GLOBAL_ALL);

		if ((rb_http_handler->multi_handle = curl_multi_init()) == NULL ) {
			return NULL;
		}
		rb_http_handler->thread_running = 1;
		const int set_max_connection_rc = curl_multi_setopt (
		                                      rb_http_handler->multi_handle,
		                                      CURLMOPT_MAX_TOTAL_CONNECTIONS,
		                                      DEFAULT_MAX_TOTAL_CONNECTIONS);
		if (CURLM_OK != set_max_connection_rc) {
			snprintf (err, errsize, "Error setting MAX_TOTAL_CONNECTIONS: %s",
			          curl_multi_strerror(set_max_connection_rc));
			return NULL;
		}

		pthread_create (&rb_http_handler->p_thread_send, NULL,
		                &rb_http_process_message_plain,
		                rb_http_handler);

		return rb_http_handler;
	} else {
		return NULL;
	}
}

int rb_http_handler_set_opt (struct rb_http_handler_s *rb_http_handler,
                             const char *key,
                             const char *val, char *err,
                             size_t errsize) {

	if (key == NULL || val == NULL) {
		snprintf (err, errsize, "Invalid option");
		return -1;
	}

	if (!strcmp(key, "HTTP_MAX_TOTAL_CONNECTIONS")) {
		if (CURLM_OK != (curl_multi_setopt (rb_http_handler->multi_handle,
		                                    CURLMOPT_MAX_TOTAL_CONNECTIONS,
		                                    atol(val)))) {
			snprintf (err, errsize, "Error setting MAX_TOTAL_CONNECTIONS");
			return -1;
		}
	} else if (!strcmp(key, "HTTP_VERBOSE")) {
		rb_http_handler->verbose =  atol(val);
	} else if (!strcmp(key, "HTTP_TIMEOUT")) {
		rb_http_handler->timeout =  atol(val);
	} else if (!strcmp(key, "HTTP_CONNTTIMEOUT")) {
		rb_http_handler->connttimeout = atol(val);
	} else if (!strcmp(key, "RB_HTTP_MAX_MESSAGES")) {
		rb_http_handler->max_messages = atoi(val);
	} else {
		snprintf (err, errsize, "Error decoding option: \"%s: %s\"", key, val);
		return -1;
	}

	return 0;
}

/**
 * @brief Free memory from a handler
 * @param rb_http_handler Handler that will freed
 */
int rb_http_handler_destroy (struct rb_http_handler_s *rb_http_handler,
                             char *err,
                             size_t errsize) {
	rb_http_handler->thread_running = 0;
	pthread_join(rb_http_handler->p_thread_send, NULL);

	if (CURLM_OK != curl_multi_cleanup (rb_http_handler->multi_handle)) {
		snprintf (err, errsize, "Error cleaning up curl multi");
		return 1;
	}

	rd_fifoq_destroy (&rb_http_handler->rfq);

	if (rb_http_handler->url != NULL) {
		free (rb_http_handler->url);
	}

	free (rb_http_handler);

	return 0;
}

/**
 * @brief Enqueues a message (non-blocking)
 * @param handler The handler that will be used to send the message.
 * @param message Message to be enqueued.
 * @param options Options
 */
int rb_http_produce (struct rb_http_handler_s *handler,
                     char *buff,
                     size_t len,
                     int flags,
                     char *err,
                     size_t errsize,
                     void *opaque) {

	int error = 0;
	if (ATOMIC_OP(add, fetch, &handler->left, 1) < handler->max_messages) {
		struct rb_http_message_s *message = calloc (1,
		                                    sizeof (struct rb_http_message_s)
		                                    + ((flags & RB_HTTP_MESSAGE_F_COPY) ? len : 0));

		message->len = len;
		message->client_opaque = opaque;

		if (flags & RB_HTTP_MESSAGE_F_COPY) {
			message->payload = (char *) &message[1];
			memcpy (message->payload, buff, len);
		} else {
			message->payload = buff;
		}

		if (flags & RB_HTTP_MESSAGE_F_FREE) {
			message->free_message = 1;
		} else {
			message->free_message = 0;
		}

		if (message != NULL && message->len > 0 && message->payload != NULL) {
			rd_fifoq_add (&handler->rfq, message);
		}
	} else {
		ATOMIC_OP(sub, fetch, &handler->left, 1);
		error++;
	}

	(void) err;
	(void) errsize;

	return error;
}

/**
 *
 */
int rb_http_get_reports (struct rb_http_handler_s *rb_http_handler,
                         cb_report report_fn, int timeout_ms) {
	rd_fifoq_elm_t *rfqe;
	struct rb_http_report_s *report = NULL;
	struct rb_http_message_s *message = NULL;
	int nowait = 0;
	long http_code = 0;

	if (timeout_ms == 0) {
		nowait = 1;
	}

	while ((rfqe = rd_fifoq_pop0 (&rb_http_handler->rfq_reports,
	                              nowait,
	                              timeout_ms)) != NULL) {
		if (rfqe->rfqe_ptr != NULL) {
			report = rfqe->rfqe_ptr;
			curl_easy_getinfo (report->handler,
			                   CURLINFO_PRIVATE,
			                   (char **)&message);
			http_code = report->http_code;

			report_fn (rb_http_handler,
			           report->err_code,
			           http_code, NULL,
			           message->payload,
			           message->len,
			           message->client_opaque);

			curl_slist_free_all (message->headers);
			curl_easy_cleanup (report->handler);
			ATOMIC_OP(sub, fetch, &rb_http_handler->left, 1);
			if (message->free_message && message->payload != NULL) {
				free (message->payload); message->payload = NULL;
			}
			free (message); message = NULL;
			free (report); report = NULL;
			rd_fifoq_elm_release (&rb_http_handler->rfq_reports, rfqe);
		}
	}

	return rb_http_handler->left;
}