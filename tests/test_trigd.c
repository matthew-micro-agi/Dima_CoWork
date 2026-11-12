/* Unit tests for trigd's pure logic and the shared continuity check. */
#include <stdio.h>
#include <string.h>

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

	CHECK(trigd_period_from_fps(30) == 33333333LL);
	CHECK(trigd_period_from_fps(60) == 16666666LL);
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

int main(void)
{
	test_t0();
	test_first_index();
	test_subs();
	test_seqcheck();
	test_msg();
	if (failures) {
		fprintf(stderr, "%d check(s) failed\n", failures);
		return 1;
	}
	printf("test_trigd: all checks passed\n");
	return 0;
}
