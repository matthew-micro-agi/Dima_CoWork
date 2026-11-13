/*
 * cam-emu pure logic (no sockets, unit-tested on the laptop), DL-26:
 *   - plan: what a trigger edge turns into (frame, drop, extra, late), when
 *   - schedule: frames waiting for their capture time
 *   - buffer queue: slots queued by the client, V4L2 order (FIFO)
 *   - delivery: buffer or lost, sequence counter, sequence jumps
 *   - pattern: moving, noisy NV12 content
 */
#ifndef CAMEMU_CORE_H
#define CAMEMU_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- random numbers: xorshift64*, reproducible with --seed ---- */
struct ce_rng {
	uint64_t s;
};

void     ce_rng_seed(struct ce_rng *r, uint64_t seed);
uint64_t ce_rand(struct ce_rng *r);
double   ce_uniform(struct ce_rng *r);              /* [0, 1) */
bool     ce_chance(struct ce_rng *r, double pct);   /* true with pct % */

/* ---- faults and timing ---- */
struct ce_faults {
	int64_t latency_ns;   /* edge -> frame timestamp */
	int64_t jitter_ns;    /* uniform +- around the latency */
	double  drop_pct;     /* edge gives no frame (sensor missed the trigger) */
	double  extra_pct;    /* an extra frame between two edges */
	double  late_pct;     /* frame 3/4 of a period later than it should be */
	double  seqjump_pct;  /* sequence skips 1-3 values without a lost frame */
};

enum ce_kind {
	CE_FRAME    = 0,  /* normal frame of its edge */
	CE_SPURIOUS = 1,  /* belongs to no edge, stamp FWT_CAM_STAMP_SPURIOUS */
	CE_LATE     = 2,  /* frame of its edge, timestamp shifted by 3/4 period */
};

const char *ce_kind_str(enum ce_kind k);

struct ce_event {
	int64_t      due_ns;  /* capture time, monotonic: delivered at this time */
	int64_t      index;   /* true edge index (ground truth) */
	enum ce_kind kind;
};

/*
 * Plan one edge (edge time already monotonic). Writes 0..2 events into out:
 * none if dropped; the frame (normal or late); maybe a spurious one between
 * this edge and the next. *dropped is set if the edge gives no frame.
 */
int ce_plan_edge(const struct ce_faults *f, struct ce_rng *r, int64_t edge_mono_ns,
		 int64_t index, int64_t period_ns, struct ce_event out[2], bool *dropped);

/* ---- schedule: events sorted by due time ---- */
#define CE_SCHED_MAX 64

struct ce_sched {
	struct ce_event e[CE_SCHED_MAX];
	int             n;
	uint64_t        overflow;
};

void ce_sched_init(struct ce_sched *s);
/* Insert sorted. False (counted in overflow) if full. */
bool ce_sched_add(struct ce_sched *s, const struct ce_event *e);
/* Earliest due time, or INT64_MAX if empty. */
int64_t ce_sched_next(const struct ce_sched *s);
/* Take the earliest event if due at now. */
bool ce_sched_pop_due(struct ce_sched *s, int64_t now_ns, struct ce_event *out);

/* ---- buffers queued by the client (V4L2: dequeued in queue order) ---- */
struct ce_bufq {
	int  count;                    /* buffers that exist (REQBUFS) */
	int  fifo[32];
	int  head, n;
	bool queued[32];
};

void ce_bufq_init(struct ce_bufq *q, int count);
/* 0, -EINVAL bad slot, -EBUSY already queued. */
int  ce_bufq_queue(struct ce_bufq *q, int slot);
/* Next queued slot, or -1 if none. */
int  ce_bufq_take(struct ce_bufq *q);
/* Everything back to the client (STREAMOFF). */
void ce_bufq_clear(struct ce_bufq *q);

/* ---- stream: sequence and counters ---- */
struct ce_stream {
	bool     on;
	uint32_t sequence;     /* next sequence number */
	uint64_t delivered;
	uint64_t lost_nobuf;   /* frame due, no buffer queued: sequence advanced */
	uint64_t seq_jumps;
};

void ce_stream_on(struct ce_stream *st);

/*
 * A frame is due: maybe jump the sequence, take a buffer. Returns the slot,
 * or -1 if lost (no buffer). *seq gets the frame's sequence number either way.
 */
int ce_deliver(struct ce_stream *st, struct ce_bufq *q, const struct ce_faults *f,
	       struct ce_rng *r, uint32_t *seq);

/* ---- content: moving checkerboard + per-pixel noise ---- */
struct ce_pattern {
	int      w, h, square;
	int8_t  *noise;      /* w*h + CE_NOISE_SLACK values in [-amp, amp] */
	size_t   noise_len;
	uint8_t *row[2];     /* two checkerboard rows, w + 2*square long */
	uint64_t frame;
};

#define CE_NOISE_SLACK 65536

/* amp: noise amplitude in Y levels (0 = none). Returns 0 or -ENOMEM/-EINVAL. */
int  ce_pattern_init(struct ce_pattern *p, int w, int h, int amp, struct ce_rng *r);
void ce_pattern_free(struct ce_pattern *p);
/* Fill the next frame (NV12, stride w): the board moves 4 px per frame,
 * the noise moves randomly. */
void ce_pattern_fill(struct ce_pattern *p, uint8_t *nv12, struct ce_rng *r);

#endif
