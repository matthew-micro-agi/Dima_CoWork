/*
 * Index continuity check: detect gaps, duplicates and backward steps in a
 * stream of (session, index). Never fills or renumbers anything; it only
 * reports. Used by trig-listen now, by the capture process later.
 */
#ifndef FWT_SEQCHECK_H
#define FWT_SEQCHECK_H

#include <stdbool.h>
#include <stdint.h>

enum fwt_seq_result {
	FWT_SEQ_FIRST,        /* first index seen (of a session) */
	FWT_SEQ_OK,           /* index == previous + 1 */
	FWT_SEQ_GAP,          /* index > previous + 1: *missing set */
	FWT_SEQ_DUP,          /* index == previous */
	FWT_SEQ_BACKWARD,     /* index < previous */
	FWT_SEQ_NEW_SESSION,  /* session changed: restarts the check */
};

struct fwt_seqcheck {
	bool     have;
	uint32_t session;
	int64_t  last_index;

	/* Totals since init (all sessions). */
	int64_t  received;
	int64_t  gaps;       /* number of gap events */
	int64_t  missing;    /* number of missing indices */
	int64_t  dups;
	int64_t  backward;
};

void fwt_seqcheck_init(struct fwt_seqcheck *c);

/* Feed one (session, index). On FWT_SEQ_GAP, *missing (if non-NULL) is set. */
enum fwt_seq_result fwt_seqcheck_feed(struct fwt_seqcheck *c, uint32_t session,
				      int64_t index, int64_t *missing);

const char *fwt_seq_result_str(enum fwt_seq_result r);

#endif
