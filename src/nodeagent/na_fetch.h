/*
 * Fetch planning (architecture §5.5, DL-22): which segment bytes answer a
 * request, keyframe-aligned, and what is missing. Pure logic over the
 * recorder's index; unit-tested on the laptop.
 */
#ifndef NA_FETCH_H
#define NA_FETCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "recorder.h"

#define NA_FETCH_MAX_PARTS 256  /* 10 min of 4 s segments = 150 */
#define NA_FETCH_MAX_GAPS  32

struct na_fetch_part {
	int64_t  first_idx;   /* identifies the segment file */
	uint64_t offset;      /* start byte (a keyframe) */
	uint64_t len;
};

struct na_range {
	int64_t from, to;     /* inclusive */
};

struct na_fetch_plan {
	uint32_t session;
	int64_t  from, to;            /* actual range returned */
	uint64_t bytes;
	size_t   nparts;
	struct na_fetch_part parts[NA_FETCH_MAX_PARTS];
	size_t   ngaps;
	struct na_range gaps[NA_FETCH_MAX_GAPS];  /* requested but not returned */
	bool     gaps_truncated;
	uint64_t missing_inside;      /* frames missing inside returned segments */
	bool     open_segment_pending;  /* session still recording past `to` */
};

/*
 * Plan a fetch of [from_idx, to_idx] (to_idx < 0: up to the last available) or
 * of the last last_s seconds (last_s > 0: overrides the range).
 * Returns 0, -ENOENT (unknown session / nothing available in the range),
 * -EINVAL (bad range), -E2BIG (longer than max_s seconds).
 */
int na_fetch_plan(const struct recorder *idx, uint32_t session, int64_t from_idx,
		  int64_t to_idx, int64_t last_s, int64_t max_s, struct na_fetch_plan *p);

/* "a-b,c-d" or "none" */
void na_ranges_str(const struct na_fetch_plan *p, char *buf, size_t len);

#endif
