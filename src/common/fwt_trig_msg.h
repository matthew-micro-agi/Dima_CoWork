/*
 * Trigger message: what trigd publishes per trigger edge, and the small
 * subscribe protocol (decision log DL-11).
 *
 * Transport: Unix datagram socket on the node. trigd binds a known path;
 * a consumer binds its own (autobind) address and sends SUBSCRIBE; trigd then
 * sends every EDGE to every subscriber, and END when it stops.
 *
 * Internal IPC between our own binaries built from the same source, so a
 * fixed, versioned C struct (no padding, host byte order).
 */
#ifndef FWT_TRIG_MSG_H
#define FWT_TRIG_MSG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FWT_TRIG_MAGIC   0x47545446u  /* "FTTG" in memory (little endian) */
#define FWT_TRIG_VERSION 1

#define FWT_TRIGD_SOCK_DEFAULT "/run/fwtrial/trigd.sock"

enum fwt_trig_type {
	FWT_TRIG_EDGE        = 1,  /* trigd -> consumer: one trigger edge      */
	FWT_TRIG_SUBSCRIBE   = 2,  /* consumer -> trigd                         */
	FWT_TRIG_UNSUBSCRIBE = 3,  /* consumer -> trigd                         */
	FWT_TRIG_END         = 4,  /* trigd -> consumer: session ended / stopped */
};

/* Edge flags. */
#define FWT_TRIG_F_TIME_UNVERIFIED (1u << 0)  /* clock sync not verified (DL-13) */
#define FWT_TRIG_F_COMPUTED        (1u << 1)  /* t_ns from schedule, not measured */
#define FWT_TRIG_F_TS_MISSING      (1u << 2)  /* measured mode: no hardware timestamp for
                                               * this edge; t_ns from schedule (DL-09) */

struct fwt_trig_msg {
	uint32_t magic;      /* FWT_TRIG_MAGIC */
	uint16_t version;    /* FWT_TRIG_VERSION */
	uint16_t type;       /* enum fwt_trig_type */
	uint32_t session;
	uint32_t flags;      /* FWT_TRIG_F_* (EDGE only) */
	int64_t  index;      /* edge index within the session (EDGE, END: last index) */
	int64_t  t_ns;       /* edge time, ns on the PTP clock */
	int64_t  period_ns;  /* session period, ns */
};

_Static_assert(sizeof(struct fwt_trig_msg) == 40, "fwt_trig_msg must stay 40 bytes");
_Static_assert(offsetof(struct fwt_trig_msg, index) == 16, "no padding before index");

static inline void fwt_trig_msg_init(struct fwt_trig_msg *m, uint16_t type)
{
	*m = (struct fwt_trig_msg){ .magic = FWT_TRIG_MAGIC,
				    .version = FWT_TRIG_VERSION,
				    .type = type };
}

/* True if buf/len holds a message we understand. */
static inline bool fwt_trig_msg_valid(const void *buf, long len)
{
	const struct fwt_trig_msg *m = buf;

	return len == (long)sizeof(*m) && m->magic == FWT_TRIG_MAGIC &&
	       m->version == FWT_TRIG_VERSION;
}

#endif
