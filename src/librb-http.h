#include <string.h>

#define RB_HTTP_MESSAGE_F_FREE 1
#define RB_HTTP_MESSAGE_F_COPY 2

////////////////////
/// Types
////////////////////
struct rb_http_handler_s;
struct rb_http_message_s;

////////////////////
/// Functions
////////////////////
struct rb_http_handler_s * rb_http_handler (char * urls_str,
        long curlmopt_maxconnects);
void rb_http_handler_destroy (struct rb_http_handler_s * rb_http_handler);
void rb_http_produce (struct rb_http_handler_s * handler,
                      char * buff,
                      size_t len,
                      int flags);