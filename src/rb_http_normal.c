#include "../config.h"
#include "rb_http_normal.h"

static size_t write_null_callback(void *buffer, size_t size, size_t nmemb,
                                  void *opaque) {
  (void)buffer;
  (void)opaque;
  return nmemb * size;
}

static void rb_http_send_message(struct rb_http_handler_s *rb_http_handler,
                                 struct rb_http_message_s *message) {
  CURL *handler;
  handler = curl_easy_init();

  if (handler == NULL) {
    struct rb_http_report_s *report =
        calloc(1, sizeof(struct rb_http_report_s));
    report->err_code = -1;
    report->http_code = 0;
    report->handler = NULL;
    rd_fifoq_add(&rb_http_handler->rfq_reports, report);
  }

  if (curl_easy_setopt(handler, CURLOPT_URL, rb_http_handler->options->url) !=
      CURLE_OK) {
    struct rb_http_report_s *report =
        calloc(1, sizeof(struct rb_http_report_s));
    report->err_code = -1;
    report->http_code = 0;
    report->handler = NULL;
    rd_fifoq_add(&rb_http_handler->rfq_reports, report);
  }

  message->headers = NULL;
  message->headers =
      curl_slist_append(message->headers, "Accept: application/json");
  message->headers =
      curl_slist_append(message->headers, "Content-Type: application/json");
  message->headers = curl_slist_append(message->headers, "charsets: utf-8");

  if (curl_easy_setopt(handler, CURLOPT_PRIVATE, message) != CURLE_OK) {
    struct rb_http_report_s *report =
        calloc(1, sizeof(struct rb_http_report_s));
    report->err_code = -1;
    report->http_code = 0;
    report->handler = NULL;
    rd_fifoq_add(&rb_http_handler->rfq_reports, report);
  }
  curl_easy_setopt(handler, CURLOPT_WRITEFUNCTION, write_null_callback);

  if (curl_easy_setopt(handler, CURLOPT_HTTPHEADER, message->headers) !=
      CURLE_OK) {
    struct rb_http_report_s *report =
        calloc(1, sizeof(struct rb_http_report_s));
    report->err_code = -1;
    report->http_code = 0;
    report->handler = NULL;
    rd_fifoq_add(&rb_http_handler->rfq_reports, report);
  }

  if (curl_easy_setopt(handler, CURLOPT_VERBOSE,
                       rb_http_handler->options->verbose) != CURLE_OK) {
    struct rb_http_report_s *report =
        calloc(1, sizeof(struct rb_http_report_s));
    report->err_code = -1;
    report->http_code = 0;
    report->handler = NULL;
    rd_fifoq_add(&rb_http_handler->rfq_reports, report);
  }

  if (curl_easy_setopt(handler, CURLOPT_TIMEOUT_MS,
                       rb_http_handler->options->timeout) != CURLE_OK) {
    struct rb_http_report_s *report =
        calloc(1, sizeof(struct rb_http_report_s));
    report->err_code = -1;
    report->http_code = 0;
    report->handler = NULL;
    rd_fifoq_add(&rb_http_handler->rfq_reports, report);
  }

  if (curl_easy_setopt(handler, CURLOPT_CONNECTTIMEOUT_MS,
                       rb_http_handler->options->conntimeout) != CURLE_OK) {
    struct rb_http_report_s *report =
        calloc(1, sizeof(struct rb_http_report_s));
    report->err_code = -1;
    report->http_code = 0;
    report->handler = NULL;
    rd_fifoq_add(&rb_http_handler->rfq_reports, report);
  }

  if (curl_easy_setopt(handler, CURLOPT_POSTFIELDSIZE, message->len) !=
      CURLE_OK) {
    struct rb_http_report_s *report =
        calloc(1, sizeof(struct rb_http_report_s));
    report->err_code = -1;
    report->http_code = 0;
    report->handler = NULL;
    rd_fifoq_add(&rb_http_handler->rfq_reports, report);
  }

  if (curl_easy_setopt(handler, CURLOPT_POSTFIELDS, message->payload) !=
      CURLE_OK) {
    struct rb_http_report_s *report =
        calloc(1, sizeof(struct rb_http_report_s));
    report->err_code = -1;
    report->http_code = 0;
    report->handler = NULL;
    rd_fifoq_add(&rb_http_handler->rfq_reports, report);
  }

  if (rb_http_handler->options->insecure) {
    curl_easy_setopt(handler, CURLOPT_SSL_VERIFYPEER, 0);
    curl_easy_setopt(handler, CURLOPT_SSL_VERIFYHOST, 0);
  }

  if (curl_multi_add_handle(rb_http_handler->multi_handle, handler) !=
      CURLM_OK) {
    struct rb_http_report_s *report =
        calloc(1, sizeof(struct rb_http_report_s));
    report->err_code = -1;
    report->http_code = 0;
    report->handler = NULL;
    rd_fifoq_add(&rb_http_handler->rfq_reports, report);
  }
  if (curl_multi_perform(rb_http_handler->multi_handle,
                         &rb_http_handler->still_running) != CURLM_OK) {
    struct rb_http_report_s *report =
        calloc(1, sizeof(struct rb_http_report_s));
    report->err_code = -1;
    report->http_code = 0;
    report->handler = NULL;
    rd_fifoq_add(&rb_http_handler->rfq_reports, report);
  }
}

/**
 * [curl_recv_message  description]
 * @param  arg [description]
 * @return     [description]
 */
static void rb_http_recv_message(struct rb_http_handler_s *rb_http_handler) {

  struct rb_http_report_s *report = NULL;
  struct rb_http_message_s *message = NULL;
  CURLMsg *msg = NULL;

  struct timeval timeout;
  int rc;       /* select() return code */
  CURLMcode mc; /* curl_multi_fdset() return code */

  fd_set fdread;
  fd_set fdwrite;
  fd_set fdexcep;
  int maxfd = -1;

  long curl_timeo = -1;

  FD_ZERO(&fdread);
  FD_ZERO(&fdwrite);
  FD_ZERO(&fdexcep);

  /* set a suitable timeout to play around with */
  timeout.tv_sec = 1;
  timeout.tv_usec = 0;

  if (curl_multi_timeout(rb_http_handler->multi_handle, &curl_timeo) !=
      CURLM_OK) {
    struct rb_http_report_s *ireport =
        calloc(1, sizeof(struct rb_http_report_s));
    ireport->err_code = -1;
    ireport->http_code = 0;
    ireport->handler = NULL;
    rd_fifoq_add(&rb_http_handler->rfq_reports, ireport);
  }

  if (curl_timeo >= 0) {
    timeout.tv_sec = curl_timeo / 1000;
    if (timeout.tv_sec > 1)
      timeout.tv_sec = 1;
    else
      timeout.tv_usec = (curl_timeo % 1000) * 1000;
  }

  /* get file descriptors from the transfers */
  mc = curl_multi_fdset(rb_http_handler->multi_handle, &fdread, &fdwrite,
                        &fdexcep, &maxfd);

  if (mc != CURLM_OK) {
    fprintf(stderr, "curl_multi_fdset() failed, code %d.\n", mc);
    struct rb_http_report_s *ireport =
        calloc(1, sizeof(struct rb_http_report_s));
    ireport->err_code = -1;
    ireport->http_code = 0;
    ireport->handler = NULL;
    rd_fifoq_add(&rb_http_handler->rfq_reports, ireport);
  }

  /* On success the value of maxfd is guaranteed to be >= -1. We call
     select(maxfd + 1, ...); specially in case of (maxfd == -1) there are
     no fds ready yet so we call select(0, ...) --or Sleep() on Windows--
     to sleep 100ms, which is the minimum suggested value in the
     curl_multi_fdset() doc. */

  if (maxfd == -1) {
    /* Portable sleep for platforms other than Windows. */
    struct timeval wait = {0, 100 * 1000}; /* 100ms */
    rc = select(0, NULL, NULL, NULL, &wait);
  } else {
    /* Note that on some platforms 'timeout' may be modified by select().
       If you need access to the original value save a copy beforehand. */
    rc = select(maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout);
  }

  switch (rc) {
  case -1:
    /* select error */
    break;
  case 0:  /* timeout */
  default: /* action */
    if (curl_multi_perform(rb_http_handler->multi_handle,
                           &rb_http_handler->still_running) != CURLM_OK) {
      struct rb_http_report_s *ireport =
          calloc(1, sizeof(struct rb_http_report_s));
      ireport->err_code = -1;
      ireport->http_code = 0;
      ireport->handler = NULL;
      rd_fifoq_add(&rb_http_handler->rfq_reports, ireport);
    }
    break;
  }

  /* See how the transfers went */
  while ((msg = curl_multi_info_read(rb_http_handler->multi_handle,
                                     &rb_http_handler->msgs_left))) {
    if (msg->msg == CURLMSG_DONE) {
      report = calloc(1, sizeof(struct rb_http_report_s));

      if (curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE,
                            (char **)&message) != CURLE_OK) {
        report->err_code = -1;
        report->http_code = 0;
        report->handler = NULL;
        rd_fifoq_add(&rb_http_handler->rfq_reports, report);
        continue;
      }

      report->err_code = msg->data.result;
      report->handler = msg->easy_handle;
      curl_easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE,
                        &report->http_code);

      if (curl_multi_remove_handle(rb_http_handler->multi_handle,
                                   msg->easy_handle) != CURLM_OK) {
        report->err_code = -1;
        report->http_code = 0;
        report->handler = msg->easy_handle;
        rd_fifoq_add(&rb_http_handler->rfq_reports, report);
        continue;
      }

      rd_fifoq_add(&rb_http_handler->rfq_reports, report);
    }
  }
}

void *rb_http_process_normal(void *arg) {

  struct rb_http_threaddata_s *rb_http_threaddata =
      (struct rb_http_threaddata_s *)arg;

  struct rb_http_handler_s *rb_http_handler =
      (struct rb_http_handler_s *)rb_http_threaddata->rb_http_handler;

  assert(rb_http_threaddata != NULL);
  assert(rb_http_handler != NULL);
  assert(rb_http_handler->options != NULL);

  rd_fifoq_elm_t *rfqe = NULL;

  if (arg != NULL) {
    while (rb_http_handler->thread_running) {
      rfqe = rd_fifoq_pop(&rb_http_threaddata->rfq);
      if (rfqe != NULL && rfqe->rfqe_ptr != NULL) {
        rb_http_send_message(rb_http_handler, rfqe->rfqe_ptr);
        rd_fifoq_elm_release(&rb_http_threaddata->rfq, rfqe);
      } else {
        rb_http_recv_message(rb_http_handler);
      }
    }
  }

  return NULL;
}

int rb_http_get_reports_normal(struct rb_http_handler_s *rb_http_handler,
                               cb_report report_fn, int timeout_ms) {
  rd_fifoq_elm_t *rfqe;
  struct rb_http_report_s *report = NULL;
  struct rb_http_message_s *message = NULL;
  int nowait = 0;
  long http_code = 0;
  char *str_error = NULL;

  if (timeout_ms == 0) {
    nowait = 1;
  }

  while ((rfqe = rd_fifoq_pop0(&rb_http_handler->rfq_reports, nowait,
                               timeout_ms)) != NULL) {
    if (rfqe->rfqe_ptr != NULL) {
      report = rfqe->rfqe_ptr;

      if (report->handler != NULL) {
        curl_easy_getinfo(report->handler, CURLINFO_PRIVATE, (char **)&message);

        http_code = report->http_code;

        if (message != NULL) {
          ATOMIC_OP(sub, fetch, &rb_http_handler->left, 1);
          str_error = strdup(curl_easy_strerror(report->err_code));
          report_fn(rb_http_handler, report->err_code, http_code, str_error,
                    message->payload, message->len, message->client_opaque);
          curl_slist_free_all(message->headers);

          if (message->free_message && message->payload != NULL) {
            free(message->payload);
            message->payload = NULL;
            free(message);
            message = NULL;
          }

          curl_easy_cleanup(report->handler);
        }
      }
      free(report);
      report = NULL;
      rd_fifoq_elm_release(&rb_http_handler->rfq_reports, rfqe);
    }
  }

  return rb_http_handler->left;
}
