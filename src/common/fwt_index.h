/*
 * Trigger index: the shared definition every node uses (architecture §4.3).
 *
 *   index = round((t_edge - T0) / period)
 *
 * All times are nanoseconds on the PTP clock (PHC). The index is computed from
 * an edge's time, never counted, so a lost event cannot shift later indices.
 */
#ifndef FWT_INDEX_H
#define FWT_INDEX_H

#include <stdbool.h>
#include <stdint.h>

/* Session plan, distributed by the controller to all nodes. */
struct fwt_plan {
	uint32_t session;
	int64_t  t0_ns;      /* time of edge 0 */
	int64_t  period_ns;  /* > 0 */
};

/*
 * Index of the edge at t_ns: round((t_ns - t0) / period), rounding half away
 * from zero. Returns false if the plan is invalid (period <= 0).
 */
bool fwt_index_of(const struct fwt_plan *plan, int64_t t_ns, int64_t *index);

/* Nominal time of edge `index`: t0 + index * period. */
int64_t fwt_edge_time(const struct fwt_plan *plan, int64_t index);

#endif
