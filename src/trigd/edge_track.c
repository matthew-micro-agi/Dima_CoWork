#include "edge_track.h"

#include <math.h>
#include <string.h>

#include "fwt_trig_msg.h"

void edge_track_init(struct edge_track *t, const struct fwt_plan *plan, int64_t first_index,
		     int64_t max_dev_ns)
{
	memset(t, 0, sizeof(*t));
	t->plan = *plan;
	t->next = first_index;
	t->max_dev_ns = max_dev_ns > 0 ? max_dev_ns : plan->period_ns / 4;
}

static void put_missing(struct edge_track *t, struct edge *e)
{
	e->index = t->next;
	e->t_ns = fwt_edge_time(&t->plan, t->next);
	/* No PTP lock check yet (DL-13): every edge is "time not verified". */
	e->flags = FWT_TRIG_F_COMPUTED | FWT_TRIG_F_TS_MISSING | FWT_TRIG_F_TIME_UNVERIFIED;
	t->next++;
	t->missing++;
	t->consecutive_missing++;
}

int edge_track_event(struct edge_track *t, int64_t t_ns, struct edge *out, int max)
{
	int64_t k;

	if (!fwt_index_of(&t->plan, t_ns, &k))
		return 0;

	int64_t dev = t_ns - fwt_edge_time(&t->plan, k);

	if (dev < 0)
		dev = -dev;
	if (dev > t->max_dev_ns || k < t->next) {
		t->ignored++;  /* not on our grid, or that edge was already announced */
		return 0;
	}

	int n = 0;

	while (t->next < k && n < max - 1)
		put_missing(t, &out[n++]);
	if (t->next != k)
		return n;  /* out[] full: the event is lost, its edge will be overdue */

	out[n].index = k;
	out[n].t_ns = t_ns;
	out[n].flags = FWT_TRIG_F_TIME_UNVERIFIED;
	n++;
	t->next = k + 1;
	t->measured++;
	t->consecutive_missing = 0;
	if (dev > t->dev_max_ns)
		t->dev_max_ns = dev;
	t->dev_sum_sq += (double)dev * (double)dev;
	return n;
}

int edge_track_overdue(struct edge_track *t, int64_t now_ns, int64_t margin_ns, struct edge *out,
		       int max)
{
	int n = 0;

	while (n < max && fwt_edge_time(&t->plan, t->next) + margin_ns < now_ns)
		put_missing(t, &out[n++]);
	return n;
}

double edge_track_dev_rms(const struct edge_track *t)
{
	return t->measured ? sqrt(t->dev_sum_sq / (double)t->measured) : 0.0;
}
