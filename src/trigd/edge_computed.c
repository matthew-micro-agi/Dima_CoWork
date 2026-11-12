/*
 * Computed edge source: edge times come from the schedule, t_k = T0 + k*period.
 *
 * The CPTS fires the real edges in hardware; this source only announces them.
 * A timer (CLOCK_MONOTONIC) wakes trigd shortly after each edge time. The
 * announced t_k is always the formula, never the wake-up time, so Linux
 * scheduling jitter cannot leak into the data. If a wake-up is late by more
 * than a period, every due edge is announced in order: in computed mode a late
 * wake-up is not a lost edge (the hardware fired it).
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "edge_source.h"
#include "fwt_trig_msg.h"

/* Wake this long after an edge's nominal time (margin for clock-offset error). */
#define WAKE_LAG_NS 1000000LL  /* 1 ms */

struct computed {
	struct edge_source       base;
	const struct trigd_clock *clk;
	struct fwt_plan          plan;
	int64_t                  next_index;
	int                      tfd;
};

static int c_fd(struct edge_source *src)
{
	return ((struct computed *)src)->tfd;
}

/* Arm the timer for edge `next_index` (absolute CLOCK_MONOTONIC). */
static int arm_timer(struct computed *c)
{
	int64_t off;
	int r = trigd_clock_mono_offset(c->clk, &off);

	if (r)
		return r;

	int64_t t_edge = fwt_edge_time(&c->plan, c->next_index);
	struct itimerspec its;

	memset(&its, 0, sizeof(its));
	its.it_value = trigd_ns_to_ts(t_edge - off + WAKE_LAG_NS);
	if (its.it_value.tv_sec == 0 && its.it_value.tv_nsec == 0)
		its.it_value.tv_nsec = 1;  /* 0 would disarm the timer */
	if (timerfd_settime(c->tfd, TFD_TIMER_ABSTIME, &its, NULL) != 0)
		return -errno;
	return 0;
}

static int c_ready(struct edge_source *src, struct edge *out, int max)
{
	struct computed *c = (struct computed *)src;
	uint64_t expirations;
	int64_t now;
	int n = 0, r;

	/* Drain the timer (non-blocking fd: EAGAIN just means a spurious wake). */
	if (read(c->tfd, &expirations, sizeof(expirations)) < 0 && errno != EAGAIN)
		return -errno;

	r = trigd_clock_now(c->clk, &now);
	if (r)
		return r;

	while (n < max && fwt_edge_time(&c->plan, c->next_index) <= now) {
		out[n].index = c->next_index;
		out[n].t_ns = fwt_edge_time(&c->plan, c->next_index);
		/* No PTP lock check yet (DL-13): every edge is "time not verified". */
		out[n].flags = FWT_TRIG_F_COMPUTED | FWT_TRIG_F_TIME_UNVERIFIED;
		c->next_index++;
		n++;
	}

	r = arm_timer(c);
	return r ? r : n;
}

static void c_destroy(struct edge_source *src)
{
	struct computed *c = (struct computed *)src;

	if (c->tfd >= 0)
		close(c->tfd);
	free(c);
}

struct edge_source *edge_source_computed_new(const struct trigd_clock *clk,
					     const struct fwt_plan *plan,
					     int64_t first_index)
{
	struct computed *c = calloc(1, sizeof(*c));

	if (!c)
		return NULL;
	c->base.name = "computed";
	c->base.fd = c_fd;
	c->base.ready = c_ready;
	c->base.destroy = c_destroy;
	c->clk = clk;
	c->plan = *plan;
	c->next_index = first_index;

	c->tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
	if (c->tfd < 0 || arm_timer(c) != 0) {
		int e = errno;

		c_destroy(&c->base);
		errno = e ? e : EINVAL;
		return NULL;
	}
	return &c->base;
}
