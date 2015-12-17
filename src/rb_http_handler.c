/**
 * @file rb_http_handler.c
 * @author Diego Fernández Barrera
 * @brief Main library.
 */
#include "rb_http_handler.h"
#include "rb_http_normal.h"
#include "rb_http_chunked.h"

struct rb_http_handler_s *rb_http_handler_create (const char *urls_str,
        char *err,
        size_t errsize) {

	(void) err;
	(void) errsize;

	if (urls_str == NULL)
		return NULL;

	struct rb_http_handler_s *rb_http_handler =
	    calloc (1, sizeof (struct rb_http_handler_s));

	rb_http_handler->options = calloc (1, sizeof (struct rb_http_options_s));

	rd_fifoq_init (&rb_http_handler->rfq_reports);

	rb_http_handler->still_running = 0;
	rb_http_handler->msgs_left = 0;
	rb_http_handler->thread_running = 1;

	rb_http_handler->options->max_messages = DEFAULT_MAX_MESSAGES;
	rb_http_handler->options->conntimeout = DEFAULT_CONTTIMEOUT;
	rb_http_handler->options->connections = DEFAULT_CONNECTIONS;
	rb_http_handler->options->timeout = DEFAULT_TIMEOUT;
	rb_http_handler->options->url = strdup(urls_str);
	rb_http_handler->options->mode = -1;

	curl_global_init(CURL_GLOBAL_ALL);

	return rb_http_handler;
}

int rb_http_handler_set_opt (struct rb_http_handler_s *rb_http_handler,
                             const char *key,
                             const char *val,
                             char *err,
                             size_t errsize) {
	assert(rb_http_handler != NULL);
	assert(rb_http_handler->options != NULL);

	if (key == NULL || val == NULL) {
		snprintf (err, errsize, "Invalid option");
		return -1;
	}

	if (!strcmp(key, "RB_HTTP_CONNECTIONS")) {
		rb_http_handler->options->connections =  atoi(val);
	} else if (!strcmp(key, "HTTP_VERBOSE")) {
		rb_http_handler->options->verbose =  atol(val);
	} else if (!strcmp(key, "RB_HTTP_MODE")) {
		rb_http_handler->options->mode =  atoi(val);
	} else if (!strcmp(key, "HTTP_TIMEOUT")) {
		rb_http_handler->options->timeout =  atol(val);
	} else if (!strcmp(key, "HTTP_CONNTTIMEOUT")) {
		rb_http_handler->options->conntimeout = atol(val);
	} else if (!strcmp(key, "RB_HTTP_MAX_MESSAGES")) {
		rb_http_handler->options->max_messages = atoi(val);
	} else {
		snprintf (err, errsize, "Error decoding option: \"%s: %s\"", key, val);
		return -1;
	}

	return 0;
}

void rb_http_handler_run (struct rb_http_handler_s *rb_http_handler) {
	assert(rb_http_handler != NULL);
	assert(rb_http_handler->options != NULL);

	int i = 0;
	struct rb_http_threaddata_s *rb_http_threaddata = NULL;

	switch (rb_http_handler->options->mode) {
	case NORMAL_MODE:
	// rb_http_threaddata = (struct rb_http_threaddata_s *)
	//                      calloc(1, sizeof(struct rb_http_threaddata_s));

	// rb_http_handler->multi_handle = curl_multi_init();

	// curl_multi_setopt (rb_http_handler->multi_handle,
	//                    CURLMOPT_MAX_TOTAL_CONNECTIONS,
	//                    rb_http_handler->options->connections);

	// pthread_create (&rb_http_threaddata->p_thread,
	//                 NULL,
	//                 &rb_http_process_normal,
	//                 rb_http_handler);
	// break;
	case CHUNKED_MODE:
		for (i = 0;  i < rb_http_handler->options->connections; i++) {
			rb_http_threaddata = calloc(1, sizeof(struct rb_http_threaddata_s));
			rb_http_handler->threads[i] = rb_http_threaddata;
			rb_http_handler->options->max_batch_messages =
			    rb_http_handler->options->max_messages / 10;
			rb_http_handler->options->post_timeout =
			    rb_http_handler->options->timeout / 10;

			rd_fifoq_init(&rb_http_threaddata->rfq);
			rb_http_threaddata->post_timestamp = time(NULL);
			rb_http_threaddata->rfq_pending = NULL;
			rb_http_threaddata->rb_http_handler = rb_http_handler;
			rb_http_threaddata->easy_handle = curl_easy_init();
			rb_http_threaddata->chunks = 0;
			rb_http_threaddata->opaque = NULL;

			pthread_create (&rb_http_threaddata->p_thread,
			                NULL,
			                &rb_http_process_chunked,
			                rb_http_threaddata);
		}
		break;
	default:
		exit(1);
	}
}

void rb_http_handler_destroy (struct rb_http_handler_s *rb_http_handler,
                              char *err,
                              size_t errsize) {
	assert(rb_http_handler != NULL);

	(void) err;
	(void) errsize;

	int i = 0;
	ATOMIC_OP(sub, fetch, &rb_http_handler->thread_running, 1);

	for (i = 0; i < rb_http_handler->options->connections ; i++) {
		pthread_join(rb_http_handler->threads[i]->p_thread, NULL);
		curl_easy_cleanup(rb_http_handler->threads[i]->easy_handle);
		free(rb_http_handler->threads[i]);
	}

	rd_fifoq_destroy(&rb_http_handler->rfq_reports);
	if (rb_http_handler->options->url != NULL) {
		free (rb_http_handler->options->url);
	}

	free(rb_http_handler->options);
	free(rb_http_handler);

	curl_global_cleanup();
}

int rb_http_produce (struct rb_http_handler_s *handler,
                     char *buff,
                     size_t len,
                     int flags,
                     char *err,
                     size_t errsize,
                     void *opaque) {

	int error = 0;
	if (ATOMIC_OP(add, fetch, &handler->left, 1) < handler->options->max_messages) {
		struct rb_http_message_s *message = calloc(1,
		                                    sizeof(struct rb_http_message_s) +
		                                    ((flags & RB_HTTP_MESSAGE_F_COPY) ? len : 0));

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
			if (handler->next_thread == handler->options->connections) {
				handler->next_thread = 0;
			}

			rd_fifoq_add (&handler->threads[handler->next_thread++]->rfq, message);
		}
	} else {
		ATOMIC_OP(sub, fetch, &handler->left, 1);
		error++;
	}

	(void) err;
	(void) errsize;

	return error;
}

int rb_http_batch_produce (struct rb_http_handler_s *handler,
                           char *buff,
                           size_t len,
                           int flags,
                           char *err,
                           size_t errsize,
                           void *opaque) {

	(void) handler;
	(void) buff;
	(void) len;
	(void) flags;
	(void) err;
	(void) errsize;
	(void) opaque;

	// TODO
	return 0;
}

int rb_http_get_reports (struct rb_http_handler_s *rb_http_handler,
                         cb_report report_fn, int timeout_ms) {

	switch (rb_http_handler->options->mode) {
	case NORMAL_MODE:
		// return rb_http_get_reports_normal(rb_http_handler, report_fn, timeout_ms);
		return 0;
		break;
	case CHUNKED_MODE:
		return rb_http_get_reports_chunked(rb_http_handler, report_fn, timeout_ms);
		break;
	default:
		exit(1);
	}

	return rb_http_handler->left;
}

size_t write_null_callback (void *buffer,
                            size_t size,
                            size_t nmemb,
                            void *opaque) {
	(void) buffer;
	(void) opaque;
	return nmemb * size;
}
