#include "librb-http.h"

static void rb_http_send_message(struct rb_http_handler_s *rb_http_handler,
                                 struct rb_http_message_s *message);
static void rb_http_recv_message(struct rb_http_handler_s *rb_http_handler);
static size_t write_null_callback (void *buffer,
                                   size_t size,
                                   size_t nmemb,
                                   void *opaque);

/**
 * @brief Send a message from the queue
 * @param  arg Opaque that contains a struct thread_arguments_t with the URL
 * and message queue.
 */
void *rb_http_process_message_plain (void *arg) {

	struct rb_http_handler_s *rb_http_handler = (struct rb_http_handler_s *) arg;

	rd_fifoq_elm_t *rfqe = NULL;

	if (arg != NULL) {
		while (rb_http_handler->thread_running) {
			rfqe = rd_fifoq_pop (&rb_http_handler->rfq);
			if (rfqe != NULL && rfqe->rfqe_ptr != NULL) {
				rb_http_send_message(rb_http_handler, rfqe->rfqe_ptr);
				rd_fifoq_elm_release (&rb_http_handler->rfq, rfqe);
			} else {
				rb_http_recv_message(rb_http_handler);
			}
		}
	}

	return NULL;
}

void rb_http_send_message(struct rb_http_handler_s *rb_http_handler,
                          struct rb_http_message_s *message) {
	CURL *handler;
	handler  =  curl_easy_init();

	if (handler == NULL) {
		struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));
		report->err_code = -1;
		report->http_code = 0;
		report->handler = NULL;
		rd_fifoq_add (&rb_http_handler->rfq_reports, report);
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
	}
	curl_easy_setopt(handler, CURLOPT_WRITEFUNCTION, write_null_callback);

	if (curl_easy_setopt (handler, CURLOPT_HTTPHEADER,
	                      message->headers) != CURLE_OK) {
		struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));
		report->err_code = -1;
		report->http_code = 0;
		report->handler = NULL;
		rd_fifoq_add (&rb_http_handler->rfq_reports, report);
	}

	if (curl_easy_setopt(handler, CURLOPT_VERBOSE,
	                     rb_http_handler->verbose) != CURLE_OK) {
		struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));
		report->err_code = -1;
		report->http_code = 0;
		report->handler = NULL;
		rd_fifoq_add (&rb_http_handler->rfq_reports, report);
	}

	if (curl_easy_setopt (handler, CURLOPT_TIMEOUT_MS,
	                      rb_http_handler->timeout) != CURLE_OK) {
		struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));
		report->err_code = -1;
		report->http_code = 0;
		report->handler = NULL;
		rd_fifoq_add (&rb_http_handler->rfq_reports, report);
	}

	if (curl_easy_setopt (handler, CURLOPT_CONNECTTIMEOUT_MS,
	                      rb_http_handler->connttimeout) != CURLE_OK) {
		struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));
		report->err_code = -1;
		report->http_code = 0;
		report->handler = NULL;
		rd_fifoq_add (&rb_http_handler->rfq_reports, report);
	}

	if (curl_easy_setopt (handler, CURLOPT_POSTFIELDSIZE,
	                      message->len) != CURLE_OK) {
		struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));
		report->err_code = -1;
		report->http_code = 0;
		report->handler = NULL;
		rd_fifoq_add (&rb_http_handler->rfq_reports, report);
	}

	if (curl_easy_setopt (handler, CURLOPT_POSTFIELDS,
	                      message->payload) != CURLE_OK) {
		struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));
		report->err_code = -1;
		report->http_code = 0;
		report->handler = NULL;
		rd_fifoq_add (&rb_http_handler->rfq_reports, report);
	}

	if (curl_multi_add_handle (rb_http_handler->multi_handle,
	                           handler) != CURLM_OK) {
		struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));
		report->err_code = -1;
		report->http_code = 0;
		report->handler = NULL;
		rd_fifoq_add (&rb_http_handler->rfq_reports, report);
	}
	if (curl_multi_perform (rb_http_handler->multi_handle,
	                        &rb_http_handler->still_running) != CURLM_OK) {
		struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));
		report->err_code = -1;
		report->http_code = 0;
		report->handler = NULL;
		rd_fifoq_add (&rb_http_handler->rfq_reports, report);
	}
}

/**
 * [curl_recv_message  description]
 * @param  arg [description]
 * @return     [description]
 */
void rb_http_recv_message (struct rb_http_handler_s *rb_http_handler) {

	struct rb_http_report_s *report = NULL;
	struct rb_http_message_s *message = NULL;
	CURLMsg *msg = NULL;

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

	if (curl_multi_timeout (rb_http_handler->multi_handle,
	                        &curl_timeo) != CURLM_OK) {
		struct rb_http_report_s *ireport = calloc(1, sizeof(struct rb_http_report_s));
		ireport->err_code = -1;
		ireport->http_code = 0;
		ireport->handler = NULL;
		rd_fifoq_add (&rb_http_handler->rfq_reports, ireport);
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

	if (mc != CURLM_OK) {
		fprintf (stderr, "curl_multi_fdset() failed, code %d.\n", mc);
		struct rb_http_report_s *ireport = calloc(1, sizeof(struct rb_http_report_s));
		ireport->err_code = -1;
		ireport->http_code = 0;
		ireport->handler = NULL;
		rd_fifoq_add (&rb_http_handler->rfq_reports, ireport);
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
		if (curl_multi_perform (rb_http_handler->multi_handle,
		                        &rb_http_handler->still_running) != CURLM_OK) {
			struct rb_http_report_s *ireport = calloc(1, sizeof(struct rb_http_report_s));
			ireport->err_code = -1;
			ireport->http_code = 0;
			ireport->handler = NULL;
			rd_fifoq_add (&rb_http_handler->rfq_reports, ireport);
		}
		break;
	}

	/* See how the transfers went */
	while ((msg = curl_multi_info_read (
	                  rb_http_handler->multi_handle,
	                  &rb_http_handler->msgs_left))) {
		if (msg->msg == CURLMSG_DONE) {
			report = calloc (1, sizeof (struct rb_http_report_s));
			if (curl_multi_remove_handle (rb_http_handler->multi_handle,
			                              msg->easy_handle) != CURLM_OK ) {
				struct rb_http_report_s *ireport = calloc(1, sizeof(struct rb_http_report_s));
				ireport->err_code = -1;
				ireport->http_code = 0;
				ireport->handler = NULL;
				rd_fifoq_add (&rb_http_handler->rfq_reports, ireport);
			}

			if (curl_easy_getinfo (msg->easy_handle,
			                       CURLINFO_PRIVATE, (char **)&message) != CURLE_OK) {
				struct rb_http_report_s *ireport = calloc(1, sizeof(struct rb_http_report_s));
				ireport->err_code = -1;
				ireport->http_code = 0;
				ireport->handler = NULL;
				rd_fifoq_add (&rb_http_handler->rfq_reports, ireport);
			}

			if (report == NULL) {
				struct rb_http_report_s *ireport = calloc(1, sizeof(struct rb_http_report_s));
				ireport->err_code = -1;
				ireport->http_code = 0;
				ireport->handler = NULL;
				rd_fifoq_add (&rb_http_handler->rfq_reports, ireport);
			}

			report->err_code = msg->data.result;
			report->handler = msg->easy_handle;
			curl_easy_getinfo (msg->easy_handle,
			                   CURLINFO_RESPONSE_CODE,
			                   &report->http_code);

			rd_fifoq_add (&rb_http_handler->rfq_reports, report);
		}
	}
}

size_t write_null_callback (void *buffer,
                            size_t size,
                            size_t nmemb,
                            void *opaque) {
	(void) buffer;
	(void) opaque;
	return nmemb * size;
}