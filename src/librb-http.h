#include <string.h>

#define RB_HTTP_MESSAGE_F_FREE 1
#define RB_HTTP_MESSAGE_F_COPY 2

////////////////////
/// Types
////////////////////
struct rb_http_handler_s;
struct rb_http_message_s;
typedef void (*cb_report) (struct rb_http_handler_s * rb_http_handler,
                           int status_code,
                           const char * status_code_str,
                           char * buff,
                           void * opaque);

////////////////////
/// Functions
////////////////////
struct rb_http_handler_s * rb_http_handler (const char * urls_str,
        long curlmopt_maxconnects,char *err,size_t errbuf);
void rb_http_handler_destroy (struct rb_http_handler_s * rb_http_handler);
void rb_http_produce (struct rb_http_handler_s * handler,
                      char * buff,
                      size_t len,
                      int flags,
                      void *opaque);
void rb_http_get_reports (struct rb_http_handler_s * rb_http_handler,
                          cb_report report_fn);