#ifndef RB_HTTP_HANDLER
#define RB_HTTP_HANDLER

#include "../config.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <curl/curl.h>
#include <librd/rdlog.h>
#include <librd/rdqueue.h>
#include <librd/rdthread.h>

#define RB_HTTP_MESSAGE_F_FREE        1
#define RB_HTTP_MESSAGE_F_COPY        2
#define DEFAULT_MAX_TOTAL_CONNECTIONS 4
#define DEFAULT_MAX_MESSAGES          512
#define DEFAULT_TIMEOUT               10000L
#define DEFAULT_CONTTIMEOUT           3000L
#define DEFAULT_CONNECTIONS           4
#define MAX_CONNECTIONS               4096

#define NORMAL_MODE 0
#define CHUNKED_MODE   1

////////////////////////////////////////////////////////////////////////////////
// Structures
////////////////////////////////////////////////////////////////////////////////

// @brief Contains the "handler" information.
struct rb_http_handler_s {
	CURLM *multi_handle;     // NORMAL_MODE: Curl multi handler
	int still_running;       // NORMAL_MODE: Number of easy to be processed
	int msgs_left;           // NORMAL_MODE
	int left;                // NORMAL_MODE

	struct rb_http_options_s *options; // Options
	int thread_running;                // Keep threads running if set to 1
	rd_fifoq_t rfq;                    // Message queue
	rd_fifoq_t rfq_reports;            // Reports queue
	struct rb_http_threaddata_s *threads[MAX_CONNECTIONS];   // For GZIP_MODE
};

// @brief Contains the "handler" options.
struct rb_http_options_s {
	char *url;               // Endpoint URL
	int mode;                // NORMAL_MODE or GZIP_MODE
	int max_messages;        // Max messages in queue
	int connections;         // Number of simultaneous connections
	long timeout;            // Total timeout
	long conntimeout;        // Connection timeout
	long verbose;            // Curl verbose mode if set to 1
};

// @brief Contains information per thread.
struct rb_http_threaddata_s {
	int dirty;               // Set to 1 if last chunk hasn't been send yet
	rd_fifoq_t rfq_pending;  // Chunks writed waiting for response
	CURL *easy_handle;       // Curl easy handler
	pthread_t p_thread;      // Thread id
	struct rb_http_handler_s *rb_http_handler;       // Ref to the handler
};

// @brief The message to send.
struct rb_http_message_s {
	char *payload;                // Content of the message
	size_t len;                   // Length of the message
	int free_message;             // If message should be free'd by the library
	int copy;                     // If message should be copied by the library
	void *client_opaque;          // Opaque
	struct curl_slist *headers;   // HTTP headers
};

// @brief Contains one or more reports for a transfer
struct rb_http_report_s {
	int err_code;          // Curl error code
	long http_code;        // HTTP response code
	rd_fifoq_t *rfq_msgs;  // Messages in the report
	CURL *handler;         // Curl handler used for messages
};


////////////////////////////////////////////////////////////////////////////////
/// Types
////////////////////////////////////////////////////////////////////////////////

/**
 * [void  description]
 * @param  opaque [description]
 * @return        [description]
 */
typedef void (*cb_report) (struct rb_http_handler_s *rb_http_handler,
                           int status_code,
                           long http_code,
                           const char *status_code_str,
                           char *buff,
                           size_t bufsiz,
                           void *opaque);

////////////////////////////////////////////////////////////////////////////////
/// Functions
////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Creates a handler to produce messages.
 * @param  urls_str List of comma sepparated URLs. If the first URL doesn't work
 * it will try the next one.
 * @return          Handler for send messages to the provided URL.
 */
struct rb_http_handler_s *rb_http_handler_create (const char *urls_str,
        char *err,
        size_t errbuf);

/**
 * Initializes threads
 * @param rb_http_handler Handler to initialize
 */
void rb_http_handler_run (struct rb_http_handler_s *rb_http_handler);

/**
 * Destroy and cleans the Handler
 * @param  rb_http_handler Handler to destroy
 * @param  err             Error string
 * @param  errsize         Length of the error
 */
void rb_http_handler_destroy (struct rb_http_handler_s *rb_http_handler,
                              char *err,
                              size_t errsize);

/**
 * [rb_http_produce  description]
 * @param  handler [description]
 * @param  buff    [description]
 * @param  len     [description]
 * @param  flags   [description]
 * @param  err     [description]
 * @param  errsize [description]
 * @param  opaque  [description]
 * @return         [description]
 */
int rb_http_produce (struct rb_http_handler_s *handler,
                     char *buff,
                     size_t len,
                     int flags,
                     char *err,
                     size_t errsize,
                     void *opaque);

/**
 * [rb_http_batch_produce  description]
 * @param  handler [description]
 * @param  buff    [description]
 * @param  len     [description]
 * @param  flags   [description]
 * @param  err     [description]
 * @param  errsize [description]
 * @param  opaque  [description]
 * @return         [description]
 */
int rb_http_batch_produce (struct rb_http_handler_s *handler,
                           char *buff,
                           size_t len,
                           int flags,
                           char *err,
                           size_t errsize,
                           void *opaque);

/**
 * [rb_http_get_reports  description]
 * @param  rb_http_handler [description]
 * @param  report_fn       [description]
 * @param  timeout_ms      [description]
 * @return                 [description]
 */
int rb_http_get_reports (struct rb_http_handler_s *rb_http_handler,
                         cb_report report_fn,
                         int timeout_ms);

/**
 * [rb_http_handler_set_opt  description]
 * @param  rb_http_handler [description]
 * @param  key             [description]
 * @param  val             [description]
 * @param  err             [description]
 * @param  errsize         [description]
 * @return                 [description]
 */
int rb_http_handler_set_opt (struct rb_http_handler_s *rb_http_handler,
                             const char *key,
                             const char *val,
                             char *err,
                             size_t errsize);

#endif