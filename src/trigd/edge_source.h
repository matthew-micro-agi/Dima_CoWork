/*
 * Edge source: where trigd gets its edges from.
 *
 *   computed (now):   t_k = T0 + k * period, woken by a timer
 *   measured (later): hardware timestamps of the edges from /dev/ptpN,
 *                     once the timestamp route exists (decision log DL-09)
 *
 * Both expose a file descriptor to wait on, so trigd's main loop can poll the
 * edge source and its subscriber socket together. Consumers only ever see
 * {session, index, t_ns, flags}; switching sources changes nothing for them.
 */
#ifndef EDGE_SOURCE_H
#define EDGE_SOURCE_H

#include <stdint.h>

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

#endif
