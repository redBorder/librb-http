/**
 * @file rb_http_handler.c
 * @author Diego Fernández Barrera
 * @brief Main library.
 */

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

/**
 *  @struct rb_http_handler_s rb_http_handler.c "rb_http_handler.c"
 *  @brief Contains the "handler" information.
 */
struct rb_http_handler_s {
	int still_running;
	int thread_running;
	int msgs_left;
	int left;
	int max_messages;
	long curlmopt_maxconnects;
	char *url;
	pthread_mutex_t multi_handle_mutex;
	CURLM *multi_handle;
	rd_fifoq_t rfq;
	rd_fifoq_t rfq_reports;
	pthread_t rd_thread_send;
	pthread_t rd_thread_recv;
};

/**
 *  @struct rb_http_message_s rb_http_handler.c "rb_http_handler.c"
 *  @brief The message to send.
 */
struct rb_http_message_s {
	char *payload;
	size_t len;
	int free_message;
	int copy;
	struct curl_slist *headers;
	void *client_opaque;
};

struct rb_http_report_s {
	int err_code;
	long http_code;
	CURL *handler;
};

////////////////////
// Private functions
////////////////////
static void *rb_http_send_message (void *arg);
static void *rb_http_recv_message (void *arg);

/**
 * @brief Creates a handler to produce messages.
 * @param  urls_str List of comma sepparated URLs. If the first URL doesn't work
 * it will try the next one.
 * @return          Handler for send messages to the provided URL.
 */
struct rb_http_handler_s *rb_http_create_handler (
    const char *urls_str,
    long curlmopt_maxconnects,
    int max_messages,
    char *err,
    size_t errsize) {

	struct rb_http_handler_s *rb_http_handler = NULL;
	(void) err;
	(void) errsize;

	if (urls_str != NULL) {
		rb_http_handler = calloc (1, sizeof (struct rb_http_handler_s));

		rd_fifoq_init (&rb_http_handler->rfq);
		rd_fifoq_init (&rb_http_handler->rfq_reports);

		rb_http_handler->curlmopt_maxconnects = curlmopt_maxconnects;

		rb_http_handler->url = strdup(urls_str);
		rb_http_handler->still_running = 0;
		rb_http_handler->msgs_left = 0;

		if (pthread_mutex_init (&rb_http_handler->multi_handle_mutex, NULL) != 0) {
			snprintf (err, errsize, "Error setting initializing mutex");
			return NULL;
		}
		if ((rb_http_handler->multi_handle = curl_multi_init()) == NULL ) {
			return NULL;
		}
		rb_http_handler->thread_running = 1;
		rb_http_handler->max_messages = max_messages;
		if (CURLM_OK != (curl_multi_setopt (rb_http_handler->multi_handle,
		                                    CURLMOPT_MAX_TOTAL_CONNECTIONS,
		                                    curlmopt_maxconnects))) {
			snprintf (err, errsize, "Error setting MAX_TOTAL_CONNECTIONS");
			return NULL;
		}

		pthread_create (&rb_http_handler->rd_thread_send, NULL, &rb_http_send_message,
		                rb_http_handler);
		pthread_create (&rb_http_handler->rd_thread_recv, NULL, &rb_http_recv_message,
		                rb_http_handler);

		return rb_http_handler;
	} else {
		return NULL;
	}
}

/**
 * @brief Free memory from a handler
 * @param rb_http_handler Handler that will freed
 */
int rb_http_handler_destroy (struct rb_http_handler_s *rb_http_handler,
                             char *err,
                             size_t errsize) {
	rb_http_handler->thread_running = 0;

	if (CURLM_OK != curl_multi_cleanup (rb_http_handler->multi_handle)) {
		snprintf (err, errsize, "Error cleaning up curl multi");
		return 1;
	}

	if (pthread_mutex_destroy (&rb_http_handler->multi_handle_mutex) > 0) {
		snprintf (err, errsize, "Error destroying mutex");
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

	if (pthread_mutex_lock (&handler->multi_handle_mutex)) {
		snprintf (err, errsize, "Error locking mutex");
	}
	if (handler->left < handler->max_messages) {
		if (pthread_mutex_unlock (&handler->multi_handle_mutex)) {
			snprintf (err, errsize, "Error unlocking mutex");
		}
		handler->left++;
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
		error++;
		if (pthread_mutex_unlock (&handler->multi_handle_mutex)) {
			snprintf (err, errsize, "Error unlocking mutex");
		}
	}

	return error;
}

/**
 * @brief Send a message from the queue
 * @param  arg Opaque that contains a struct thread_arguments_t with the URL
 * and message queue.
 */
void *rb_http_send_message (void *arg) {

	struct rb_http_handler_s *rb_http_handler = (struct rb_http_handler_s *) arg;

	struct rb_http_message_s *message = NULL;
	rd_fifoq_elm_t *rfqe = NULL;
	CURL *handler;

	if (arg != NULL) {
		while (rb_http_handler->thread_running) {
			rfqe = rd_fifoq_pop (&rb_http_handler->rfq);
			if (rfqe != NULL && rfqe->rfqe_ptr != NULL) {
				message = rfqe->rfqe_ptr;
				rd_fifoq_elm_release (&rb_http_handler->rfq, rfqe);
				handler  =  curl_easy_init();

				if (handler == NULL) {
					struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));
					report->err_code = -1;
					report->http_code = 0;
					report->handler = NULL;
					rd_fifoq_add (&rb_http_handler->rfq_reports, report);
					return NULL;
				}

				if (curl_easy_setopt (handler,
				                      CURLOPT_URL,
				                      rb_http_handler->url)
				        != CURLE_OK) {
					struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));
					report->err_code = -1;
					report->http_code = 0;
					report->handler = NULL;
					rd_fifoq_add (&rb_http_handler->rfq_reports, report);
					return NULL;
				}

				message->headers = NULL;
				message->headers = curl_slist_append (message->headers,
				                                      "Accept: application/json");
				message->headers = curl_slist_append (message->headers,
				                                      "Content-Type: application/json");
				message->headers = curl_slist_append (message->headers, "charsets: utf-8");

				if (curl_easy_setopt (handler, CURLOPT_PRIVATE, message) != CURLE_OK) {
					struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));
					report->err_code = -1;
					report->http_code = 0;
					report->handler = NULL;
					rd_fifoq_add (&rb_http_handler->rfq_reports, report);
					return NULL;
				}

				if (curl_easy_setopt (handler, CURLOPT_HTTPHEADER,
				                      message->headers) != CURLE_OK) {
					struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));
					report->err_code = -1;
					report->http_code = 0;
					report->handler = NULL;
					rd_fifoq_add (&rb_http_handler->rfq_reports, report);
					return NULL;
				}

				if (curl_easy_setopt (handler, CURLOPT_TIMEOUT_MS, 1000L) != CURLE_OK) {
					struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));
					report->err_code = -1;
					report->http_code = 0;
					report->handler = NULL;
					rd_fifoq_add (&rb_http_handler->rfq_reports, report);
					return NULL;
				}

				if (curl_easy_setopt (handler, CURLOPT_POSTFIELDS,
				                      message->payload) != CURLE_OK) {
					struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));
					report->err_code = -1;
					report->http_code = 0;
					report->handler = NULL;
					rd_fifoq_add (&rb_http_handler->rfq_reports, report);
					return NULL;
				}

				pthread_mutex_lock (&rb_http_handler->multi_handle_mutex);
				if (curl_multi_add_handle (rb_http_handler->multi_handle,
				                           handler) != CURLM_OK) {
					struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));
					report->err_code = -1;
					report->http_code = 0;
					report->handler = NULL;
					rd_fifoq_add (&rb_http_handler->rfq_reports, report);
					return NULL;
				}
				if (curl_multi_perform (rb_http_handler->multi_handle,
				                        &rb_http_handler->still_running) != CURLM_OK) {
					struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));
					report->err_code = -1;
					report->http_code = 0;
					report->handler = NULL;
					rd_fifoq_add (&rb_http_handler->rfq_reports, report);
					return NULL;
				}
				pthread_mutex_unlock (&rb_http_handler->multi_handle_mutex);
			}
		}
	}

	rd_thread_exit ();
	return NULL;
}

/**
 * [curl_recv_message  description]
 * @param  arg [description]
 * @return     [description]
 */
void *rb_http_recv_message (void *arg) {

	rd_thread_sigmask (SIG_BLOCK, SIGINT, RD_SIG_END);
	struct rb_http_handler_s *rb_http_handler = (struct rb_http_handler_s *) arg;
	struct rb_http_report_s *report = NULL;
	struct rb_http_message_s *message = NULL;
	CURLMsg *msg = NULL;

	while (rb_http_handler->thread_running || rb_http_handler->still_running) {
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

		pthread_mutex_lock (&rb_http_handler->multi_handle_mutex);

		if (curl_multi_timeout (rb_http_handler->multi_handle,
		                        &curl_timeo) != CURLM_OK) {
			struct rb_http_report_s *ireport = calloc(1, sizeof(struct rb_http_report_s));
			ireport->err_code = -1;
			ireport->http_code = 0;
			ireport->handler = NULL;
			rd_fifoq_add (&rb_http_handler->rfq_reports, ireport);
			return NULL;
		}

		if (curl_timeo >= 0) {
			timeout.tv_sec = curl_timeo / 1000;
			if (timeout.tv_sec > 1)
				timeout.tv_sec = 1;
			else
				timeout.tv_usec = (curl_timeo % 1000) * 1000;
		}

		/* get file descriptors from the transfers */
		mc = curl_multi_fdset (rb_http_handler->multi_handle, &fdread, &fdwrite,
		                       &fdexcep, &maxfd);
		pthread_mutex_unlock (&rb_http_handler->multi_handle_mutex);

		if (mc != CURLM_OK) {
			fprintf (stderr, "curl_multi_fdset() failed, code %d.\n", mc);
			struct rb_http_report_s *ireport = calloc(1, sizeof(struct rb_http_report_s));
			ireport->err_code = -1;
			ireport->http_code = 0;
			ireport->handler = NULL;
			rd_fifoq_add (&rb_http_handler->rfq_reports, ireport);
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
			pthread_mutex_lock (&rb_http_handler->multi_handle_mutex);
			if (curl_multi_perform (rb_http_handler->multi_handle,
			                        &rb_http_handler->still_running) != CURLM_OK) {
				pthread_mutex_unlock (&rb_http_handler->multi_handle_mutex);
				struct rb_http_report_s *ireport = calloc(1, sizeof(struct rb_http_report_s));
				ireport->err_code = -1;
				ireport->http_code = 0;
				ireport->handler = NULL;
				rd_fifoq_add (&rb_http_handler->rfq_reports, ireport);
				return NULL;
			}
			pthread_mutex_unlock (&rb_http_handler->multi_handle_mutex);
			break;
		}

		/* See how the transfers went */
		pthread_mutex_lock (&rb_http_handler->multi_handle_mutex);
		while ((msg = curl_multi_info_read (
		                  rb_http_handler->multi_handle,
		                  &rb_http_handler->msgs_left))) {
			if (msg->msg == CURLMSG_DONE) {
				report = calloc (1, sizeof (struct rb_http_report_s));
				rb_http_handler->left--;
				if (curl_multi_remove_handle (rb_http_handler->multi_handle,
				                              msg->easy_handle) != CURLM_OK ) {
					pthread_mutex_unlock (&rb_http_handler->multi_handle_mutex);
					struct rb_http_report_s *ireport = calloc(1, sizeof(struct rb_http_report_s));
					ireport->err_code = -1;
					ireport->http_code = 0;
					ireport->handler = NULL;
					rd_fifoq_add (&rb_http_handler->rfq_reports, ireport);
					return NULL;
				}
				if (curl_easy_getinfo (msg->easy_handle,
				                       CURLINFO_PRIVATE, &message) != CURLE_OK) {
					pthread_mutex_unlock (&rb_http_handler->multi_handle_mutex);
					struct rb_http_report_s *ireport = calloc(1, sizeof(struct rb_http_report_s));
					ireport->err_code = -1;
					ireport->http_code = 0;
					ireport->handler = NULL;
					rd_fifoq_add (&rb_http_handler->rfq_reports, ireport);
					return NULL;
				}

				if (report == NULL) {
					pthread_mutex_unlock (&rb_http_handler->multi_handle_mutex);
					struct rb_http_report_s *ireport = calloc(1, sizeof(struct rb_http_report_s));
					ireport->err_code = -1;
					ireport->http_code = 0;
					ireport->handler = NULL;
					rd_fifoq_add (&rb_http_handler->rfq_reports, ireport);
					return NULL;
				}

				report->err_code = msg->data.result;
				report->handler = msg->easy_handle;
				curl_easy_getinfo (msg->easy_handle,
				                   CURLINFO_RESPONSE_CODE,
				                   &report->http_code);

				rd_fifoq_add (&rb_http_handler->rfq_reports, report);
			}
		}
		pthread_mutex_unlock (&rb_http_handler->multi_handle_mutex);
	}

	rd_thread_exit ();
	return NULL;
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
			                   &message);
			http_code = report->http_code;

			report_fn (rb_http_handler,
			           report->err_code,
			           http_code, NULL,
			           message->payload,
			           message->len,
			           message->client_opaque);

			curl_slist_free_all (message->headers);
			curl_easy_cleanup (report->handler);
			if (message->free_message) {
				free (message->payload);
			}
			free (message);
			free (report);
			rd_fifoq_elm_release (&rb_http_handler->rfq_reports, rfqe);
		}
	}

	return rb_http_handler->still_running;
}
