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
 *  @struct rb_http_handler_t rb_http_handler.h "rb_http_handler.h"
 *  @brief Contains the "handler" information.
 */
struct rb_http_handler_s {
	int still_running;
	int thread_running;
	int msgs_left;
	int left;
	int max_messages;
	long curlmopt_maxconnects;
	char ** urls;
	pthread_mutex_t multi_handle_mutex;
	CURLM * multi_handle;
	rd_fifoq_t rfq;
	rd_fifoq_t rfq_reports;
	rd_thread_t * rd_thread_send;
	rd_thread_t * rd_thread_recv;
};

/**
 *  @struct message_t rb_http_handler.h "rb_http_handler.h"
 *  @brief The message to send.
 */
struct rb_http_message_s {
	char * payload;
	size_t len;
	int free_message;
	int copy;
	struct curl_slist * headers;
	void *client_opaque;
};

////////////////////
// Private functions
////////////////////
static char ** str_split (const char * a_str, const char a_delim);
static void * rb_http_send_message (void * arg);
static void * rb_http_recv_message (void * arg);

/**
 * @brief Creates a handler to produce messages.
 * @param  urls_str List of comma sepparated URLs. If the first URL doesn't work
 * it will try the next one.
 * @return          Handler for send messages to the provided URL.
 */
struct rb_http_handler_s * rb_http_handler (
    const char * urls_str,
    long curlmopt_maxconnects,
    int max_messages,
    char * err,
    size_t errsize) {

	struct rb_http_handler_s * rb_http_handler = NULL;
	(void) err;
	(void) errsize;

	if (urls_str != NULL) {
		rb_http_handler = calloc (1, sizeof (struct rb_http_handler_s));

		rd_fifoq_init (&rb_http_handler->rfq);
		rd_fifoq_init (&rb_http_handler->rfq_reports);

		rb_http_handler->curlmopt_maxconnects = curlmopt_maxconnects;

		rb_http_handler->urls = str_split (urls_str, ',');
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

		if ((rd_thread_create (&rb_http_handler->rd_thread_send,
		                       "curl_send_message",
		                       NULL,
		                       rb_http_send_message,
		                       rb_http_handler)
		    ) < 0) {
			snprintf (err, errsize, "Error creating thread for reading");
			return NULL;
		}

		if ((rd_thread_create (&rb_http_handler->rd_thread_recv, "curl_recv_message",
		                       NULL,
		                       rb_http_recv_message,
		                       rb_http_handler)
		    ) < 0) {
			snprintf (err, errsize, "Error creating thread for writting");
			return NULL;
		}

		return rb_http_handler;
	} else {
		return NULL;
	}
}

/**
 * @brief Free memory from a handler
 * @param rb_http_handler Handler that will freed
 */
void rb_http_handler_destroy (struct rb_http_handler_s * rb_http_handler) {
	rb_http_handler->thread_running = 0;

	// rd_thread_kill_join (rb_http_handler->rd_thread_send, NULL);
	// rd_thread_kill_join (rb_http_handler->rd_thread_recv, NULL);

	curl_multi_cleanup (rb_http_handler->multi_handle);
	pthread_mutex_destroy (&rb_http_handler->multi_handle_mutex);
	rd_fifoq_destroy (&rb_http_handler->rfq);

	if (rb_http_handler->urls) {
		int i;
		for (i = 0; * (rb_http_handler->urls + i); i++) {
			free (* (rb_http_handler->urls + i));
		}
		free (rb_http_handler->urls);
	}

	free (rb_http_handler);
}

/**
 * @brief Enqueues a message (non-blocking)
 * @param handler The handler that will be used to send the message.
 * @param message Message to be enqueued.
 * @param options Options
 */
int rb_http_produce (struct rb_http_handler_s * handler,
                     char * buff,
                     size_t len,
                     int flags,
                     void *opaque) {

	int error = 0;

	pthread_mutex_lock (&handler->multi_handle_mutex);
	if (handler->left < handler->max_messages) {
		pthread_mutex_unlock (&handler->multi_handle_mutex);
		handler->left++;
		struct rb_http_message_s * message = calloc (1,
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
		pthread_mutex_unlock (&handler->multi_handle_mutex);
	}

	return error;
}

/**
 * @brief Send a message from the queue
 * @param  arg Opaque that contains a struct thread_arguments_t with the URL
 * and message queue.
 */
void * rb_http_send_message (void * arg) {

	rd_thread_sigmask (SIG_BLOCK, SIGINT, RD_SIG_END);
	struct rb_http_handler_s * rb_http_handler = (struct rb_http_handler_s *) arg;

	struct rb_http_message_s * message = NULL;
	rd_fifoq_elm_t * rfqe = NULL;
	CURL * handler;

	if (arg != NULL) {
		while (rb_http_handler->thread_running) {
			rfqe = rd_fifoq_pop (&rb_http_handler->rfq);
			if (rfqe != NULL && rfqe->rfqe_ptr != NULL) {
				message = rfqe->rfqe_ptr;
				rd_fifoq_elm_release (&rb_http_handler->rfq, rfqe);

				handler  =  curl_easy_init();
				curl_easy_setopt (handler, CURLOPT_URL,
				                  rb_http_handler->urls[0]);
				// curl_easy_setopt (handler, CURLOPT_FAILONERROR, 1L);
				// curl_easy_setopt (handler, CURLOPT_VERBOSE, 1L);

				message->headers = NULL;
				message->headers = curl_slist_append (message->headers,
				                                      "Accept: application/json");
				message->headers = curl_slist_append (message->headers,
				                                      "Content-Type: application/json");
				message->headers = curl_slist_append (message->headers, "charsets: utf-8");

				curl_easy_setopt (handler, CURLOPT_PRIVATE, message);
				curl_easy_setopt (handler, CURLOPT_HTTPHEADER, message->headers);
				curl_easy_setopt (handler, CURLOPT_POSTFIELDS,
				                  message->payload);

				pthread_mutex_lock (&rb_http_handler->multi_handle_mutex);
				curl_multi_add_handle (rb_http_handler->multi_handle, handler);
				curl_multi_perform (rb_http_handler->multi_handle,
				                    &rb_http_handler->still_running);
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
void * rb_http_recv_message (void * arg) {

	rd_thread_sigmask (SIG_BLOCK, SIGINT, RD_SIG_END);
	struct rb_http_handler_s * rb_http_handler = (struct rb_http_handler_s *) arg;
	struct rb_http_message_s * message = NULL;
	CURLMsg * msg = NULL;

	while (rb_http_handler->thread_running) {
		while (rb_http_handler->still_running) {
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
			curl_multi_timeout (rb_http_handler->multi_handle, &curl_timeo);
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
				curl_multi_perform (rb_http_handler->multi_handle,
				                    &rb_http_handler->still_running);
				pthread_mutex_unlock (&rb_http_handler->multi_handle_mutex);
				break;
			}
		}
		/* See how the transfers went */
		pthread_mutex_lock (&rb_http_handler->multi_handle_mutex);
		while ((msg = curl_multi_info_read (
		                  rb_http_handler->multi_handle,
		                  &rb_http_handler->msgs_left))) {
			if (msg->msg == CURLMSG_DONE) {
				if (msg->data.result == 0) {
					rb_http_handler->left--;
					curl_multi_remove_handle (rb_http_handler->multi_handle, msg->easy_handle);
					curl_easy_getinfo (msg->easy_handle,
					                   CURLINFO_PRIVATE, &message);
					CURLMsg * report = calloc (1, sizeof (CURLMsg));
					memcpy (report, msg, sizeof (CURLMsg));
					rd_fifoq_add (&rb_http_handler->rfq_reports, report);
				}
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
void rb_http_get_reports (struct rb_http_handler_s * rb_http_handler,
                          cb_report report_fn) {
	rd_fifoq_elm_t * rfqe;
	CURLMsg * report = NULL;
	struct rb_http_message_s * message = NULL;

	while ((rfqe = rd_fifoq_pop (&rb_http_handler->rfq_reports))) {
		if (rfqe != NULL && rfqe->rfqe_ptr != NULL) {
			report = rfqe->rfqe_ptr;
			curl_easy_getinfo (report->easy_handle,
			                   CURLINFO_PRIVATE, &message);
			report_fn (rb_http_handler, report->data.result, NULL, message->payload,
			           message->client_opaque);

			curl_slist_free_all (message->headers);
			curl_easy_cleanup (report->easy_handle);
			if (message->free_message) {
				free (message->payload);
			}
			free (message);
			free (report);
			rd_fifoq_elm_release (&rb_http_handler->rfq_reports, rfqe);
		}
	}
}

/**
 * Split a string (and specify the delimiter to use)
 * @param  a_str   Single sting separated by delimiter
 * @param  a_delim Delimiter
 * @return         Array of strings separated
 */
char** str_split (const char * in_str, const char a_delim) {
	char** result    = 0;
	size_t count     = 0;
	char* last_comma = 0;
	char delim[2];
	delim[0] = a_delim;
	delim[1] = 0;
	char * a_str = strdup (in_str);
	char* tmp        = a_str;

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

	free (a_str);
	return result;
}