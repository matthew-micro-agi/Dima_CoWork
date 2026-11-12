/* Unit tests for the trigger index definition (fwt_index.c). */
#include <stdio.h>

#include "fwt_index.h"

static int failures;

#define CHECK(cond)                                                          \
	do {                                                                 \
		if (!(cond)) {                                               \
			fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__,     \
				__LINE__, #cond);                            \
			failures++;                                          \
		}                                                            \
	} while (0)

static int64_t idx(const struct fwt_plan *p, int64_t t)
{
	int64_t i = -999;
	CHECK(fwt_index_of(p, t, &i));
	return i;
}

int main(void)
{
	/* 30 fps: period 33,333,333 ns (as programmed into the CPTS). */
	const struct fwt_plan p = { .session = 7, .t0_ns = 1000000000LL,
				    .period_ns = 33333333LL };

	/* Exact grid points. */
	CHECK(idx(&p, p.t0_ns) == 0);
	CHECK(idx(&p, fwt_edge_time(&p, 1)) == 1);
	CHECK(idx(&p, fwt_edge_time(&p, 41200)) == 41200);

	/* Jitter around a grid point stays on the same index. */
	CHECK(idx(&p, fwt_edge_time(&p, 41200) + 85) == 41200);
	CHECK(idx(&p, fwt_edge_time(&p, 41200) - 85) == 41200);

	/* Just below / above half a period. */
	CHECK(idx(&p, p.t0_ns + p.period_ns / 2 - 1) == 0);
	CHECK(idx(&p, p.t0_ns + p.period_ns / 2 + 1) == 1);

	/* A lost event leaves a visible gap: next edge keeps its own index. */
	CHECK(idx(&p, fwt_edge_time(&p, 102)) == 102);

	/* Before T0: negative indices (late-join and sanity checks). */
	CHECK(idx(&p, p.t0_ns - p.period_ns) == -1);

	/* 24 h at 60 fps does not overflow. */
	const struct fwt_plan p60 = { .session = 1, .t0_ns = 1731420000000000000LL,
				      .period_ns = 16666667LL };
	CHECK(idx(&p60, fwt_edge_time(&p60, 5184000)) == 5184000);

	/* Invalid plan is rejected. */
	const struct fwt_plan bad = { .session = 1, .t0_ns = 0, .period_ns = 0 };
	int64_t i;
	CHECK(!fwt_index_of(&bad, 0, &i));

	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("test_index: all checks passed\n");
	return 0;
}
