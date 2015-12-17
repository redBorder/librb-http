#include <sys/queue.h>
#include <stdlib.h>

// @brief The message to send.
struct rb_http_message_s {
	char *payload;                // Content of the message
	size_t len;                   // Length of the message
	int free_message;             // If message should be free'd by the library
	int copy;                     // If message should be copied by the library
	void *client_opaque;          // Opaque
	TAILQ_ENTRY(rb_http_message_s) tailq;
};

typedef TAILQ_HEAD(, rb_http_message_s) rb_http_msg_q_t;

#define rb_http_msg_q_init(q) TAILQ_INIT(q)

#define rb_http_msg_q_add(q, e) TAILQ_INSERT_TAIL(q, e, tailq)

#define rb_http_msg_q_empty(q) TAILQ_EMPTY(q)

static struct rb_http_message_s *rb_http_msg_q_pop(rb_http_msg_q_t *q)
__attribute__((unused));

static struct rb_http_message_s *rb_http_msg_q_pop(rb_http_msg_q_t *q) {
	struct rb_http_message_s *p = NULL;

	if (!rb_http_msg_q_empty(q)) {
		p = TAILQ_FIRST(q);
		TAILQ_REMOVE(q, p, tailq);
	}

	return p;
}