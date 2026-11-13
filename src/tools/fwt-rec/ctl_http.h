/*
 * HTTP transport to one node's control interface (DL-04), libcurl. Only this
 * file knows about HTTP: a controller swaps it for CoAP or Zephyr's client.
 */
#ifndef CTL_HTTP_H
#define CTL_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct ctl_http {
	void *curl;
	char  base[160];       /* http://host:port */
	char  err[256];        /* last transport error, for messages */
};

struct ctl_buf {
	uint8_t *data;
	size_t   len, cap;
};

/* X-Fwt-* headers of a fetch (architecture §5.5) */
struct ctl_fetch_info {
	uint32_t session;
	int64_t  from, to;            /* actual range returned */
	char     missing[600];        /* "a-b,c-d" or "none" */
	uint64_t missing_inside;
	char     open_segment[16];    /* "none" | "not-included" */
	uint64_t bytes;               /* body bytes written */
};

/* addr: "host" or "host:port" (default port 8080). Returns 0 or -1. */
int  ctl_http_init(struct ctl_http *h, const char *addr);
void ctl_http_free(struct ctl_http *h);

/*
 * One CBOR exchange: GET (body NULL) or POST (body, may be empty). The reply
 * body goes into *reply (replaced), *status = HTTP status. Returns 0 if the
 * exchange happened (any status), -1 on a transport error (see h->err).
 */
int ctl_http_call(struct ctl_http *h, const char *path, const uint8_t *body, size_t body_len,
		  bool post, struct ctl_buf *reply, long *status);

/*
 * GET a fetch: body appended to out (only if the status is 200; otherwise the
 * CBOR error goes into *err_reply), headers into *info. Returns as above.
 */
int ctl_http_fetch(struct ctl_http *h, const char *path, FILE *out, struct ctl_fetch_info *info,
		   struct ctl_buf *err_reply, long *status);

void ctl_buf_free(struct ctl_buf *b);

#endif
