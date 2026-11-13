/*
 * Edge source: where trigd gets its edges from.
 *
 *   computed: t_k = T0 + k * period, woken by a timer
 *   measured: hardware timestamps of the edges from /dev/ptpN (decision log
 *             DL-09; needs the CPTS loop overlay, setup.md section 7)
 *
 * Both expose a file descriptor to wait on, so trigd's main loop can poll the
 * edge source and its subscriber socket together. Consumers only ever see
 * {session, index, t_ns, flags}; switching sources changes nothing for them.
 */
#ifndef EDGE_SOURCE_H
#define EDGE_SOURCE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "fwt_index.h"
#include "trigd_clock.h"

struct edge {
	int64_t  index;
	int64_t  t_ns;
	uint32_t flags;  /* FWT_TRIG_F_* */
};

struct edge_source {
	const char *name;
	int (*fd)(struct edge_source *src);
	/* Called when fd() is readable: fill up to max edges, return count or -errno. */
	int (*ready)(struct edge_source *src, struct edge *out, int max);
	void (*destroy)(struct edge_source *src);
};

/* Computed mode, starting at edge first_index (> 0 for a late join).
 * Returns NULL on error (errno set). */
struct edge_source *edge_source_computed_new(const struct trigd_clock *clk,
					     const struct fwt_plan *plan,
					     int64_t first_index);

/* Measured mode: hardware timestamps on PTP timestamp input extts_index. auto_mode:
 * fall back to computed times (with a WARNING) if the input is unavailable or no
 * event arrives for the first edges; otherwise never fall back. NULL on error. */
struct edge_source *edge_source_measured_new(const struct trigd_clock *clk,
					     const struct fwt_plan *plan, int64_t first_index,
					     unsigned extts_index, bool auto_mode);

/* Hardware events the measured source ignored (off the grid, or late for an
 * edge already announced). */
uint64_t edge_source_measured_ignored(struct edge_source *src);

#endif
