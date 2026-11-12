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
	return fps > 0 ? NSEC_PER_SEC / fps : 0;
}

int64_t trigd_first_index(int64_t t0_ns, int64_t period_ns, int64_t now_ns,
			  int64_t margin_ns)
{
	int64_t earliest = now_ns + margin_ns;

	if (t0_ns >= earliest)
		return 0;
	return (earliest - t0_ns + period_ns - 1) / period_ns;  /* ceil */
}
