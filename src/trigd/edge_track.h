/*
 * Measured edges (DL-09, gap G1): turn hardware timestamp events of the
 * trigger edges into indexed edges. Pure logic, unit-tested on the laptop.
 *
 *   event at t  -> index k = round((t - T0) / period); the edge carries the
 *                  captured t (no COMPUTED flag)
 *   no event for an edge by its time + margin -> the edge is still announced,
 *                  with its computed time and FWT_TRIG_F_TS_MISSING, so a frame
 *                  is never lost over a lost event; counted
 *
 * Callers feed all queued events first, then ask for overdue edges: a stalled
 * trigd finds the events waiting in the kernel queue and never reports a
 * false miss.
 */
#ifndef EDGE_TRACK_H
#define EDGE_TRACK_H

#include <stdbool.h>
#include <stdint.h>

#include "edge_source.h"
#include "fwt_index.h"

struct edge_track {
	struct fwt_plan plan;
	int64_t  next;             /* next edge to announce */
	int64_t  max_dev_ns;       /* an event further off its grid time is not ours */
	/* counters */
	uint64_t measured;         /* edges with a hardware time */
	uint64_t missing;          /* edges announced without one (TS_MISSING) */
	uint64_t ignored;          /* events off the grid, or for an edge already announced */
	int64_t  consecutive_missing;
	/* deviation of captured times from the plan, |t - (T0 + k*period)| */
	int64_t  dev_max_ns;
	double   dev_sum_sq;       /* for RMS */
};

/* max_dev <= 0: a quarter period. */
void edge_track_init(struct edge_track *t, const struct fwt_plan *plan, int64_t first_index,
		     int64_t max_dev_ns);

/* A hardware event at t_ns: writes the overdue missing edges before it and the
 * measured edge itself. Returns the number written (<= max), 0 if ignored. */
int edge_track_event(struct edge_track *t, int64_t t_ns, struct edge *out, int max);

/* Clock at now_ns (PTP): announce edges whose time + margin has passed without
 * an event, as TS_MISSING. Returns the number written. */
int edge_track_overdue(struct edge_track *t, int64_t now_ns, int64_t margin_ns, struct edge *out,
		       int max);

/* RMS deviation of the measured edges from the plan, ns (0 if none). */
double edge_track_dev_rms(const struct edge_track *t);

#endif
