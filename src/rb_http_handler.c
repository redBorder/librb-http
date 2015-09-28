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
	long curlmopt_maxconnects;
	char ** urls;
	pthread_mutex_t multi_handle_mutex;
	CURLM * multi_handle;
	rd_fifoq_t rfq;
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
};

////////////////////
// Private functions
////////////////////
static char** str_split (char* a_str, const char a_delim);
static void * curl_send_message (void * arg);
static void * curl_recv_message (void * arg);

/**
 * @brief Creates a handler to produce messages.
 * @param  urls_str List of comma sepparated URLs. If the first URL doesn't work
 * it will try the next one.
 * @return          Handler for send messages to the provided URL.
 */
struct rb_http_handler_s * rb_http_handler (char * urls_str,
        long curlmopt_maxconnects) {

	struct rb_http_handler_s * rb_http_handler = NULL;

	if (urls_str != NULL) {
		rb_http_handler = calloc (1, sizeof (struct rb_http_handler_s));

		rd_fifoq_init (&rb_http_handler->rfq);

		rb_http_handler->curlmopt_maxconnects = curlmopt_maxconnects;

		rb_http_handler->urls = str_split (urls_str, ',');
		rb_http_handler->urls[0] = urls_str;
		rb_http_handler->still_running = 0;
		rb_http_handler->msgs_left = 0;
		pthread_mutex_init (&rb_http_handler->multi_handle_mutex, NULL);
		rb_http_handler->multi_handle = curl_multi_init();
		rb_http_handler->thread_running = 1;

		rd_thread_create (&rb_http_handler->rd_thread_send, "curl_send_message", NULL,
		                  curl_send_message,
		                  rb_http_handler);

		rd_thread_create (&rb_http_handler->rd_thread_recv, "curl_recv_message", NULL,
		                  curl_recv_message,
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
void rb_http_handler_destroy (struct rb_http_handler_s * rb_http_handler) {
	rb_http_handler->thread_running = 0;

	rd_thread_kill_join (rb_http_handler->rd_thread_send, NULL);
	rd_thread_kill_join (rb_http_handler->rd_thread_recv, NULL);

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
void rb_http_produce (struct rb_http_handler_s * handler,
                      char * buff,
                      size_t len,
                      int flags) {

	struct rb_http_message_s * message = calloc (1,
	                                     sizeof (struct rb_http_message_s));

	message->len = len;

	if (flags & RB_HTTP_MESSAGE_F_COPY) {
		message->payload = calloc (len, sizeof (char));
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
}

/**
 * @brief Send a message from the queue
 * @param  arg Opaque that contains a struct thread_arguments_t with the URL
 * and message queue.
 */
void * curl_send_message (void * arg) {

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

				struct curl_slist * headers = NULL;
				headers = curl_slist_append (headers, "Accept: application/json");
				headers = curl_slist_append (headers,
				                             "Content-Type: application/json");
				headers = curl_slist_append (headers, "charsets: utf-8");

				curl_easy_setopt (handler, CURLOPT_HTTPHEADER, headers);
				curl_easy_setopt (handler, CURLOPT_POSTFIELDS,
				                  message->payload);

				pthread_mutex_lock (&rb_http_handler->multi_handle_mutex);
				curl_multi_add_handle (rb_http_handler->multi_handle, handler);
				curl_multi_perform (rb_http_handler->multi_handle,
				                    &rb_http_handler->still_running);
				pthread_mutex_unlock (&rb_http_handler->multi_handle_mutex);
				// curl_slist_free_all (headers);
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
void * curl_recv_message (void * arg) {

	rd_thread_sigmask (SIG_BLOCK, SIGINT, RD_SIG_END);
	struct rb_http_handler_s * rb_http_handler = (struct rb_http_handler_s *) arg;
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
					printf ("Mensaje enviado correctamente\n");
				}
				// printf ("HTTP transfer completed with status %d\n",
				// msg->data.result);
			}
		}
		pthread_mutex_unlock (&rb_http_handler->multi_handle_mutex);
	}

	rd_thread_exit ();
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