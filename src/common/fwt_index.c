#include "fwt_index.h"

bool fwt_index_of(const struct fwt_plan *plan, int64_t t_ns, int64_t *index)
{
	if (!plan || !index || plan->period_ns <= 0)
		return false;

	int64_t d = t_ns - plan->t0_ns;
	int64_t half = plan->period_ns / 2;

	/* Integer rounding, half away from zero (C division truncates toward 0). */
	if (d >= 0)
		*index = (d + half) / plan->period_ns;
	else
		*index = -((-d + half) / plan->period_ns);
	return true;
}

int64_t fwt_edge_time(const struct fwt_plan *plan, int64_t index)
{
	return plan->t0_ns + index * plan->period_ns;
}
