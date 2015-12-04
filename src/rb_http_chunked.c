#include "rb_http_chunked.h"

// static size_t def(z_stream *strm, size_t chunksize,
//                   char *message, size_t message_len, void *ptr, int flush) {

// 	size_t writed = 0;
// 	size_t have = 0;

// 	char *out = calloc(chunksize, sizeof(char));
// 	strm->avail_in = message_len;
// 	strm->next_in = (Bytef *)message;

// 	strm->avail_out = chunksize;
// 	strm->next_out = (Bytef *)out;

// 	deflate(strm, flush);
// 	have = chunksize - strm->avail_out;

// 	memcpy((char *)ptr, out, have);
// 	free(out);
// 	writed += have;

// 	return writed;
// }

// static z_stream *prepare_zstream() {

// 	z_stream strm;
// 	z_stream *p_strm = calloc(1, sizeof(z_stream));

// 	memcpy(p_strm, &strm, sizeof(z_stream));

// 	strm.zalloc = Z_NULL;
// 	strm.zfree = Z_NULL;
// 	strm.opaque = Z_NULL;

// 	return p_strm;
// }

static size_t read_callback_batch(void *ptr, size_t size, size_t nmemb,
                                  void *userp) {

	(void) size;

	rd_fifoq_elm_t *rfqe = NULL;
	size_t writed = 0;
	struct rb_http_message_s *message = NULL;
	struct rb_http_threaddata_s *rb_http_threaddata =
	    (struct rb_http_threaddata_s *) userp;

	struct rb_http_handler_s *rb_http_handler =
	    (struct rb_http_handler_s *) rb_http_threaddata->rb_http_handler;

	// Send remaining message if neccesary
	if (rb_http_threaddata->message_left != NULL ) {
		message = rb_http_threaddata->message_left;

		if (message->len <= nmemb - writed) {
			if (rb_http_threaddata->chunks == 0 && writed == 0) {
				rd_fifoq_init(&rb_http_threaddata->rfq_pending);
			}
			memcpy((char *)ptr + writed, message->payload, message->len);
			writed += message->len;
			rd_fifoq_add(&rb_http_threaddata->rfq_pending, message);
			rb_http_threaddata->message_left = NULL;
		} else {
			return CURL_READFUNC_ABORT;
		}
	}

	// Get a bunch of chunks
	while ((rfqe = rd_fifoq_pop_timedwait(&rb_http_handler->rfq, 500)) != NULL
	        && rfqe->rfqe_ptr != NULL) {
		message = rfqe->rfqe_ptr;

		if (message->len <= nmemb - writed) {
			// If the message fit the buffer
			if (rb_http_threaddata->chunks == 0 && writed == 0) {
				rd_fifoq_init(&rb_http_threaddata->rfq_pending);
			}
			memcpy((char *)ptr + writed, message->payload, message->len);
			writed += message->len;
			rd_fifoq_add(&rb_http_threaddata->rfq_pending, message);
			rd_fifoq_elm_release(&rb_http_handler->rfq, rfqe);
		} else {
			// If the message doesn't fit the buffer we need to save it for later
			rb_http_threaddata->message_left = message;
			break;
		}
	}

	// If there is no data to send
	if (writed == 0) {

		// And we already sent data
		if (rb_http_threaddata->chunks > 0) {

			// Send the zero-length chunk and reset chunks counter
			rb_http_threaddata->chunks = 0;
		} else {

			// Is not the first time we are not getting any data. Pause transfer.
			return CURL_READFUNC_PAUSE;
		}
	} else {
		// If we send data increase number of chunks
		rb_http_threaddata->chunks++;
	}

	return writed;
}

static size_t write_null_callback (void *buffer,
                                   size_t size,
                                   size_t nmemb,
                                   void *opaque) {
	(void) buffer;
	(void) opaque;

	return nmemb * size;
}

void *rb_http_process_chunked (void *arg) {

	struct rb_http_threaddata_s *rb_http_threaddata =
	    (struct rb_http_threaddata_s *) arg;

	struct rb_http_handler_s *rb_http_handler =
	    (struct rb_http_handler_s *)rb_http_threaddata->rb_http_handler;

	assert(rb_http_threaddata != NULL);
	assert(rb_http_handler != NULL);
	assert(rb_http_handler->options != NULL);

	while (rb_http_threaddata->rb_http_handler->thread_running) {
		if (curl_easy_setopt (rb_http_threaddata->easy_handle,
		                      CURLOPT_URL,
		                      rb_http_handler->options->url)
		        != CURLE_OK) {
			struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));
			report->err_code = -1;
			report->http_code = 0;
			report->handler = NULL;
			rd_fifoq_add (&rb_http_handler->rfq_reports, report);
		}

		struct curl_slist *headers = NULL;

		headers = curl_slist_append (headers,
		                             "Accept: application/json");
		headers = curl_slist_append (headers,
		                             "Content-Type: application/json");
		headers = curl_slist_append (headers, "charsets: utf-8");
		headers = curl_slist_append(headers, "Expect:");
		headers = curl_slist_append(headers, "Transfer-Encoding: chunked");

		// curl_easy_setopt(rb_http_threaddata->easy_handle, CURLOPT_ACCEPT_ENCODING,
		// "deflate");

		curl_easy_setopt(rb_http_threaddata->easy_handle, CURLOPT_WRITEFUNCTION,
		                 write_null_callback);

		if (curl_easy_setopt (rb_http_threaddata->easy_handle, CURLOPT_HTTPHEADER,
		                      headers) != CURLE_OK) {
			struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));
			report->err_code = -1;
			report->http_code = 0;
			report->handler = NULL;
			rd_fifoq_add (&rb_http_handler->rfq_reports, report);
		}

		curl_easy_setopt(rb_http_threaddata->easy_handle, CURLOPT_NOSIGNAL, 1);

		if (curl_easy_setopt(rb_http_threaddata->easy_handle, CURLOPT_VERBOSE,
		                     rb_http_handler->options->verbose) != CURLE_OK) {
			struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));
			report->err_code = -1;
			report->http_code = 0;
			report->handler = NULL;
			rd_fifoq_add (&rb_http_handler->rfq_reports, report);
		}

		if (curl_easy_setopt (rb_http_threaddata->easy_handle, CURLOPT_TIMEOUT_MS,
		                      rb_http_handler->options->timeout) != CURLE_OK) {
			struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));
			report->err_code = -1;
			report->http_code = 0;
			report->handler = NULL;
			rd_fifoq_add (&rb_http_handler->rfq_reports, report);
		}

		if (curl_easy_setopt (rb_http_threaddata->easy_handle,
		                      CURLOPT_CONNECTTIMEOUT_MS,
		                      rb_http_handler->options->conntimeout) != CURLE_OK) {
			struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));
			report->err_code = -1;
			report->http_code = 0;
			report->handler = NULL;
			rd_fifoq_add (&rb_http_handler->rfq_reports, report);
		}

		curl_easy_setopt(rb_http_threaddata->easy_handle, CURLOPT_POST, 1L);
		curl_easy_setopt(rb_http_threaddata->easy_handle, CURLOPT_READDATA,
		                 rb_http_threaddata);
		curl_easy_setopt(rb_http_threaddata->easy_handle, CURLOPT_READFUNCTION,
		                 read_callback_batch);
		CURLcode res;

		res = curl_easy_perform (rb_http_threaddata->easy_handle);

		struct rb_http_report_s *report = calloc(1, sizeof(struct rb_http_report_s));

		report->rfq_msgs = &rb_http_threaddata->rfq_pending;
		report->err_code = res;
		report->handler = rb_http_threaddata->easy_handle;
		curl_easy_getinfo (rb_http_threaddata->easy_handle,
		                   CURLINFO_RESPONSE_CODE,
		                   &report->http_code);

		rd_fifoq_add (&rb_http_handler->rfq_reports, report);
	}

	return NULL;
}

int rb_http_get_reports_chunked(struct rb_http_handler_s *rb_http_handler,
                                cb_report report_fn, int timeout_ms) {
	rd_fifoq_elm_t *rfqe = NULL;
	rd_fifoq_elm_t *rfqm = NULL;
	struct rb_http_report_s *report = NULL;
	struct rb_http_message_s *message = NULL;
	int nowait = 0;
	long http_code = 0;

	if (timeout_ms == 0) {
		nowait = 1;
	}

	while ((rfqe = rd_fifoq_pop0(&rb_http_handler->rfq_reports,
	                             nowait,
	                             timeout_ms)) != NULL) {
		if (rfqe->rfqe_ptr != NULL) {
			report = (struct rb_http_report_s *)rfqe->rfqe_ptr;
			http_code = report->http_code;

			while ((rfqm = rd_fifoq_pop(report->rfq_msgs)) != NULL) {
				if (rfqm->rfqe_ptr != NULL) {
					ATOMIC_OP(sub, fetch, &rb_http_handler->left, 1);
					message = rfqm->rfqe_ptr;
					report_fn(rb_http_handler,
					          report->err_code,
					          http_code,
					          NULL,
					          message->payload,
					          message->len,
					          message->client_opaque);

					if (message->free_message && message->payload != NULL) {
						free (message->payload); message->payload = NULL;
					}
					curl_slist_free_all(message->headers);
					free(message); message = NULL;
				}
				rd_fifoq_elm_release(report->rfq_msgs, rfqm);
			}

			free(report); report = NULL;
			rd_fifoq_elm_release(&rb_http_handler->rfq_reports, rfqe);
		}
	}

	return rb_http_handler->left;
}