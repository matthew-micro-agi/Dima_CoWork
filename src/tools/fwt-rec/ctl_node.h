/*
 * Node control protocol, client side (ICD v1, icd/fwt-icd-v1.cddl): requests
 * encoded and replies decoded with the zcbor code generated from the CDDL,
 * into plain C structs. No I/O: the transport (ctl_http.h now; CoAP or
 * Zephyr's HTTP client on a controller later) is separate, so this file is
 * the part a controller reuses as is.
 *
 * All decoders return 0, or -EBADMSG if the bytes are not that ICD message.
 */
#ifndef CTL_NODE_H
#define CTL_NODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct ctl_identity {
	char     node[64];
	char     version[64];
	uint32_t fps_min, fps_max;
	uint32_t width, height;
};

struct ctl_health {
	char     state[16];      /* "idle" | "recording" | "failed" */
	uint32_t session;
	uint64_t ring_used, ring_locked, ring_free, depth_s;
	uint64_t frames, missing;
	int64_t  temp_mc;
	int64_t  phc_ns;         /* node's PTP clock now */
	bool     time_verified;
	char     message[128];
};

struct ctl_arm {
	uint32_t session;
	int64_t  t0;             /* PTP time of edge 0 */
	uint64_t period;         /* ns */
};

struct ctl_session {
	uint32_t session;
	char     status[16];     /* "recording" | "complete" | "interrupted" */
	int64_t  start_wall;
	int64_t  t0;
	uint64_t period;
	int64_t  first_idx, last_idx;  /* available range, -1 if nothing */
	uint64_t frames, missing;
	uint64_t segments, bytes;
	bool     truncated;
};

struct ctl_segment {
	int64_t  first_idx, last_idx;
	int64_t  t_first, t_last;
	uint64_t bytes, frames, missing;
};

struct ctl_error {
	char code[32];
	char message[160];
};

/* Request bodies. Return 0 or -ENOBUFS. */
int ctl_enc_arm_req(uint32_t session, uint32_t fps, uint8_t *buf, size_t cap, size_t *len);

int ctl_dec_identity(const uint8_t *b, size_t n, struct ctl_identity *out);
int ctl_dec_health(const uint8_t *b, size_t n, struct ctl_health *out);
int ctl_dec_arm_rep(const uint8_t *b, size_t n, struct ctl_arm *out);
int ctl_dec_session_rep(const uint8_t *b, size_t n, uint32_t *session);
int ctl_dec_error(const uint8_t *b, size_t n, struct ctl_error *out);

/*
 * One page of GET /v1/sessions: *found = 1 and *out filled if `session` is on
 * it; *last = the page's last session (for ?after=), *more = another page.
 */
int ctl_dec_sessions_find(const uint8_t *b, size_t n, uint32_t session, struct ctl_session *out,
			  int *found, uint32_t *last, bool *more);

/* One page of GET /v1/segments: up to cap segments into out[], *count, *more. */
int ctl_dec_segments(const uint8_t *b, size_t n, struct ctl_segment *out, size_t cap,
		     size_t *count, bool *more);

#endif
