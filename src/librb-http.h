#include <string.h>
#include <curl/curl.h>
#include <librd/rdqueue.h>
#include <librd/rdthread.h>

#define RB_HTTP_MESSAGE_F_FREE 1
#define RB_HTTP_MESSAGE_F_COPY 2
#define DEFAULT_MAX_TOTAL_CONNECTIONS 4
#define DEFAULT_MAX_MESSAGES 512
#define DEFAULT_TIMEOUT 10000L
#define DEFAULT_CONTTIMEOUT 3000L

////////////////////
/// Types
////////////////////
struct rb_http_handler_s;
struct rb_http_message_s;
typedef void (*cb_report) (struct rb_http_handler_s *rb_http_handler,
                           int status_code,
                           long http_code,
                           const char *status_code_str,
                           char *buff, size_t bufsiz,
                           void *opaque);

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
	long timeout;
	long connttimeout;
	long verbose;
	char *url;
	CURLM *multi_handle;
	rd_fifoq_t rfq;
	rd_fifoq_t rfq_reports;
	pthread_t p_thread_send;
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
/// Functions
////////////////////
struct rb_http_handler_s *rb_http_handler_create (
    const char *urls_str,
    char *err,
    size_t errbuf);

int rb_http_handler_destroy (struct rb_http_handler_s *rb_http_handler,
                             char *err,
                             size_t errsize);

int rb_http_produce (struct rb_http_handler_s *handler,
                     char *buff,
                     size_t len,
                     int flags,
                     char *err,
                     size_t errsize,
                     void *opaque);

int rb_http_get_reports (struct rb_http_handler_s *rb_http_handler,
                         cb_report report_fn,
                         int timeout_ms);

int rb_http_handler_set_opt (struct rb_http_handler_s *rb_http_handler,
                             const char *key,
                             const char *val,
                             char *err,
                             size_t errsize);

void *rb_http_process_message_plain (void *arg);