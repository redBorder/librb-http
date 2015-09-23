////////////////////
/// Functions
////////////////////
struct rb_http_handler_t * rb_http_handler (char * urls_str,
        long curlmopt_maxconnects);
void rb_http_produce (struct rb_http_handler_t * handler,
                      char * message);
