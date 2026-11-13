/* Unit tests for trigd's pure logic and the shared continuity check. */
#include <stdio.h>
#include <string.h>

#include "edge_track.h"
#include "fwt_seqcheck.h"
#include "fwt_trig_msg.h"
#include "trigd_subs.h"
#include "trigd_t0.h"

static int failures;

#define CHECK(cond)                                                          \
	do {                                                                 \
		if (!(cond)) {                                               \
			fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__,     \
				__LINE__, #cond);                            \
			failures++;                                          \
		}                                                            \
	} while (0)

#define S 1000000000LL

static void test_t0(void)
{
	CHECK(trigd_t0_auto(12 * S + 300000000LL, S) == 14 * S);
	CHECK(trigd_t0_auto(12 * S, S) == 14 * S);            /* strictly after now */
	CHECK(trigd_t0_auto(12 * S + 999999999LL, 0) == 13 * S);
	CHECK(trigd_t0_auto(12 * S, 2 * S) == 15 * S);        /* configurable offset */

	/* whole CPTS ticks (2 ns), nearest to 1 s / fps */
	CHECK(trigd_period_from_fps(30) == 33333334LL);
	CHECK(trigd_period_from_fps(60) == 16666666LL);
	CHECK(trigd_period_from_fps(25) == 40000000LL);
	CHECK(trigd_period_from_fps(24) == 41666666LL);
	CHECK(trigd_period_from_fps(1) == 1000000000LL);
	for (int f = 1; f <= 240; f++)
		CHECK(trigd_period_from_fps(f) % TRIGD_TICK_NS == 0);
	CHECK(trigd_period_round(33333333LL) == 33333334LL);
	CHECK(trigd_period_round(33333332LL) == 33333332LL);
	CHECK(trigd_period_from_fps(0) == 0);
}

static void test_first_index(void)
{
	const int64_t p = 33333333LL, m = 100000000LL;

	CHECK(trigd_first_index(20 * S, p, 10 * S, m) == 0);         /* T0 in the future */
	CHECK(trigd_first_index(10 * S + m, p, 10 * S, m) == 0);     /* exactly at margin */
	/* T0 one second ago: first edge at or after now + margin. */
	int64_t k = trigd_first_index(9 * S, p, 10 * S, m);
	CHECK(9 * S + k * p >= 10 * S + m);
	CHECK(9 * S + (k - 1) * p < 10 * S + m);
}

static void test_subs(void)
{
	struct trigd_subs l;
	struct sockaddr_un a = { .sun_family = AF_UNIX }, b = a;
	socklen_t len = sizeof(sa_family_t) + 6;

	memcpy(a.sun_path, "\0aaaaa", 6);
	memcpy(b.sun_path, "\0bbbbb", 6);
	trigd_subs_init(&l);
	CHECK(trigd_subs_add(&l, &a, len) == 1);
	CHECK(trigd_subs_add(&l, &a, len) == 0);   /* no duplicates */
	CHECK(trigd_subs_add(&l, &b, len) == 1);
	CHECK(l.n == 2);
	CHECK(trigd_subs_remove(&l, &a, len));
	CHECK(!trigd_subs_remove(&l, &a, len));
	CHECK(l.n == 1);

	/* Full list refuses. */
	trigd_subs_init(&l);
	for (int i = 0; i < TRIGD_MAX_SUBS; i++) {
		struct sockaddr_un x = { .sun_family = AF_UNIX };

		x.sun_path[1] = (char)('A' + i);
		CHECK(trigd_subs_add(&l, &x, len) == 1);
	}
	CHECK(trigd_subs_add(&l, &a, len) == -1);
}

static void test_seqcheck(void)
{
	struct fwt_seqcheck c;
	int64_t miss = 0;

	fwt_seqcheck_init(&c);
	CHECK(fwt_seqcheck_feed(&c, 7, 0, &miss) == FWT_SEQ_FIRST);
	CHECK(fwt_seqcheck_feed(&c, 7, 1, &miss) == FWT_SEQ_OK);
	CHECK(fwt_seqcheck_feed(&c, 7, 4, &miss) == FWT_SEQ_GAP && miss == 2);
	CHECK(fwt_seqcheck_feed(&c, 7, 4, &miss) == FWT_SEQ_DUP);
	CHECK(fwt_seqcheck_feed(&c, 7, 2, &miss) == FWT_SEQ_BACKWARD);
	CHECK(fwt_seqcheck_feed(&c, 7, 5, &miss) == FWT_SEQ_OK);  /* not rewound by the stray */
	CHECK(fwt_seqcheck_feed(&c, 8, 0, &miss) == FWT_SEQ_NEW_SESSION);
	CHECK(c.gaps == 1 && c.missing == 2 && c.dups == 1 && c.backward == 1);
	CHECK(c.received == 7);
}

static void test_msg(void)
{
	struct fwt_trig_msg m;

	fwt_trig_msg_init(&m, FWT_TRIG_EDGE);
	CHECK(fwt_trig_msg_valid(&m, sizeof(m)));
	CHECK(!fwt_trig_msg_valid(&m, sizeof(m) - 1));
	m.version = 99;
	CHECK(!fwt_trig_msg_valid(&m, sizeof(m)));
}

/* Measured edges (DL-09): hardware events -> indexed edges, missing ones kept. */
#define P60 16666666LL
static void test_edge_track(void)
{
	struct fwt_plan plan = { .session = 7, .t0_ns = 1000 * S, .period_ns = P60 };
	struct edge_track t;
	struct edge e[8];
	int n;

	edge_track_init(&t, &plan, 0, 0);
	CHECK(t.max_dev_ns == P60 / 4);

	/* events a few ns off the grid: measured, captured time kept */
	n = edge_track_event(&t, 1000 * S + 12, e, 8);
	CHECK(n == 1 && e[0].index == 0 && e[0].t_ns == 1000 * S + 12);
	CHECK(!(e[0].flags & (FWT_TRIG_F_COMPUTED | FWT_TRIG_F_TS_MISSING)));
	n = edge_track_event(&t, 1000 * S + P60 - 8, e, 8);
	CHECK(n == 1 && e[0].index == 1);

	/* nothing overdue yet: edge 2 is due at T0 + 2P, margin one period */
	CHECK(edge_track_overdue(&t, 1000 * S + 2 * P60 + P60 - 1, P60, e, 8) == 0);

	/* events for 2 and 3 lost, then 4 arrives: 2, 3 announced as TS_MISSING first */
	n = edge_track_event(&t, 1000 * S + 4 * P60 + 3, e, 8);
	CHECK(n == 3 && e[0].index == 2 && e[1].index == 3 && e[2].index == 4);
	CHECK((e[0].flags & FWT_TRIG_F_TS_MISSING) && (e[0].flags & FWT_TRIG_F_COMPUTED));
	CHECK(e[0].t_ns == 1000 * S + 2 * P60 && !(e[2].flags & FWT_TRIG_F_TS_MISSING));
	CHECK(t.missing == 2 && t.measured == 3 && t.consecutive_missing == 0);

	/* the clock passes edges 5, 6 + margin with no event: overdue, TS_MISSING */
	n = edge_track_overdue(&t, 1000 * S + 6 * P60 + P60 + 1, P60, e, 8);
	CHECK(n == 2 && e[0].index == 5 && e[1].index == 6 && t.consecutive_missing == 2);

	/* a late event for 6 (already announced) and one off the grid: ignored */
	CHECK(edge_track_event(&t, 1000 * S + 6 * P60, e, 8) == 0);
	CHECK(edge_track_event(&t, 1000 * S + 7 * P60 + P60 / 2, e, 8) == 0);
	CHECK(t.ignored == 2 && t.next == 7);

	/* deviation statistics over the measured edges (12, 8, 3 ns) */
	CHECK(t.dev_max_ns == 12);
	CHECK(edge_track_dev_rms(&t) > 8.50 && edge_track_dev_rms(&t) < 8.51);  /* sqrt(217/3) */

	/* late join: first index 100, an event for 100 */
	edge_track_init(&t, &plan, 100, 0);
	n = edge_track_event(&t, 1000 * S + 100 * P60, e, 8);
	CHECK(n == 1 && e[0].index == 100);

	/* out[] too small for the missing edges (one slot is kept for the measured
	 * edge): what fits is announced, no edge is skipped, the rest become overdue */
	edge_track_init(&t, &plan, 0, 0);
	n = edge_track_event(&t, 1000 * S + 5 * P60, e, 3);
	CHECK(n == 2 && e[1].index == 1 && t.next == 2);
	n = edge_track_overdue(&t, 1000 * S + 5 * P60 + P60 + 1, P60, e, 8);
	CHECK(n == 4 && e[0].index == 2 && e[3].index == 5);
}

int main(void)
{
	test_t0();
	test_first_index();
	test_subs();
	test_seqcheck();
	test_msg();
	test_edge_track();
	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("test_trigd: all checks passed\n");
	return 0;
}
