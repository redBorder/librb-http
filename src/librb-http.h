#include <string.h>

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
                             const char *val, char *err,
                             size_t errsize);