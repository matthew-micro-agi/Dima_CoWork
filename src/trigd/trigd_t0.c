#include "trigd_t0.h"

int64_t trigd_t0_auto(int64_t now_ns, int64_t offset_ns)
{
	int64_t next_sec;

	if (now_ns >= 0)
		next_sec = (now_ns / NSEC_PER_SEC + 1) * NSEC_PER_SEC;
	else  /* not expected for real clocks; keep it well defined */
		next_sec = -((-now_ns) / NSEC_PER_SEC) * NSEC_PER_SEC + NSEC_PER_SEC;
	return next_sec + offset_ns;
}

int64_t trigd_period_from_fps(int fps)
{
	if (fps <= 0)
		return 0;
	/* nearest whole tick to 1e9 / fps, in integers: round(1e9 / (fps * tick)) * tick */
	int64_t d = (int64_t)fps * TRIGD_TICK_NS;

	return (NSEC_PER_SEC + d / 2) / d * TRIGD_TICK_NS;
}

int64_t trigd_period_round(int64_t ns)
{
	return (ns + TRIGD_TICK_NS / 2) / TRIGD_TICK_NS * TRIGD_TICK_NS;
}

int64_t trigd_first_index(int64_t t0_ns, int64_t period_ns, int64_t now_ns,
			  int64_t margin_ns)
{
	int64_t earliest = now_ns + margin_ns;

	if (t0_ns >= earliest)
		return 0;
	return (earliest - t0_ns + period_ns - 1) / period_ns;  /* ceil */
}
