#include "rb_http_handler.h"

/**
 * [rb_http_process_default  description]
 * @param  arg [description]
 * @return     [description]
 */
void *rb_http_process_normal (void *arg);

/**
 * [rb_http_get_reports_normal  description]
 * @param  rb_http_handler [description]
 * @param  report_fn       [description]
 * @param  timeout_ms      [description]
 * @return                 [description]
 */
int rb_http_get_reports_normal (struct rb_http_handler_s *rb_http_handler,
                                cb_report report_fn, int timeout_ms);