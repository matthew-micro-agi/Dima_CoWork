/* T0 choice for a session plan (decision log DL-12). */
#ifndef TRIGD_T0_H
#define TRIGD_T0_H

#include <stdint.h>

#define NSEC_PER_SEC 1000000000LL

/*
 * Default T0: the next whole second strictly after now, plus offset_ns.
 * E.g. now = 12.300 s, offset 1 s -> 14.000 s; now = 12.000 s -> 14.000 s.
 */
int64_t trigd_t0_auto(int64_t now_ns, int64_t offset_ns);

/* Period in ns for a frame rate (integer division: 30 fps -> 33,333,333 ns).
 * Returns 0 for fps <= 0. All nodes use the same integer period. */
int64_t trigd_period_from_fps(int fps);

/*
 * First edge this node can still arm: 0 if T0 is at least margin_ns in the
 * future, otherwise the first k with T0 + k*period >= now + margin (late join:
 * the node joins the shared grid instead of starting its own).
 * period_ns must be > 0.
 */
int64_t trigd_first_index(int64_t t0_ns, int64_t period_ns, int64_t now_ns,
			  int64_t margin_ns);

#endif
