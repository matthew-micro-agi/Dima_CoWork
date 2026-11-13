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

/*
 * The CPTS makes periods in whole ticks of its 500 MHz clock (2 ns): an odd
 * period runs 1 ns short per edge and the edges drift off the plan T0 + k*P
 * (30 ppb at 30 fps, measured G1 2026-11-13). Periods are therefore whole ticks.
 */
#define TRIGD_TICK_NS 2

/* Period in ns for a frame rate: the whole number of ticks nearest to 1 s / fps
 * (30 fps -> 33,333,334 ns, 60 fps -> 16,666,666 ns). 0 for fps <= 0. All nodes
 * use the same function, so a pack shares one period. */
int64_t trigd_period_from_fps(int fps);

/* ns rounded to the nearest whole tick (half up). */
int64_t trigd_period_round(int64_t ns);

/*
 * First edge this node can still arm: 0 if T0 is at least margin_ns in the
 * future, otherwise the first k with T0 + k*period >= now + margin (late join:
 * the node joins the shared grid instead of starting its own).
 * period_ns must be > 0.
 */
int64_t trigd_first_index(int64_t t0_ns, int64_t period_ns, int64_t now_ns,
			  int64_t margin_ns);

#endif
