/*
 * Recorder control socket: lock requests to a running recorder (nodecap).
 * Local Unix datagram socket, fixed versioned struct (same pattern as the
 * trigger message, DL-11). The control interface (nodeagent) will use it too.
 * When no recorder runs, rec-tool opens /rec itself instead.
 */
#ifndef REC_CTL_H
#define REC_CTL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define REC_CTL_MAGIC   0x4c435246u  /* "FRCL" in memory */
#define REC_CTL_VERSION 1
#define REC_CTL_SOCK_DEFAULT "/run/fwtrial/rec.sock"

enum rec_ctl_type {
	REC_CTL_LOCK   = 1,  /* session, from_idx, to_idx, ttl_s, text = reason */
	REC_CTL_UNLOCK = 2,  /* lock_id */
	REC_CTL_REPLY  = 3,  /* result (0 or -errno), lock_id, text = message */
};

struct rec_ctl_msg {
	uint32_t magic;
	uint16_t version;
	uint16_t type;
	uint32_t session;
	int32_t  result;
	int64_t  from_idx, to_idx, ttl_s;
	uint64_t lock_id;
	char     text[72];
};

_Static_assert(sizeof(struct rec_ctl_msg) == 120, "rec_ctl_msg must stay 120 bytes");

static inline void rec_ctl_init(struct rec_ctl_msg *m, uint16_t type)
{
	*m = (struct rec_ctl_msg){ .magic = REC_CTL_MAGIC, .version = REC_CTL_VERSION,
				   .type = type };
}

static inline bool rec_ctl_valid(const struct rec_ctl_msg *m, long len)
{
	return len == (long)sizeof(*m) && m->magic == REC_CTL_MAGIC &&
	       m->version == REC_CTL_VERSION;
}

#endif
