/* Unit tests for frame-to-edge binding by time (DL-27, nc_match). */
#include <stdio.h>

#include "nc_match.h"

static int failures;

#define CHECK(cond)                                                          \
	do {                                                                 \
		if (!(cond)) {                                               \
			fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__,     \
				__LINE__, #cond);                            \
			failures++;                                          \
		}                                                            \
	} while (0)

#define MS  1000000LL
#define P   (1000000000LL / 60)
#define LAT (25 * MS)
#define T0  (1000 * MS)  /* monotonic time of edge 0 */

static void setup(struct nc_match *m, int64_t window)
{
	struct nc_match_cfg c = { .period_ns = P, .latency_ns = LAT, .window_ns = window };

	nc_match_init(m, &c);
}

static void edge(struct nc_match *m, int64_t i)
{
	struct nc_medge e = { .index = i, .t_ns = 5000 * MS + i * P, .t_mono_ns = T0 + i * P,
			      .session = 3 };

	CHECK(nc_match_edge(m, &e));
}

/* frame at edge i + latency + d; tag = 1000 + i (or given) */
static enum nc_match_result frame(struct nc_match *m, int64_t i, int64_t d, uint64_t tag,
				  int64_t *index, uint64_t *displaced)
{
	return nc_match_frame(m, T0 + i * P + LAT + d, tag, index, displaced);
}

/* Take every decided edge; store tag (0 = no frame) per index. */
static void collect(struct nc_match *m, int64_t now, uint64_t *tags, int n)
{
	struct nc_medge d;

	while (nc_match_take_decided(m, now, &d)) {
		CHECK(d.index >= 0 && d.index < n);
		if (d.index >= 0 && d.index < n)
			tags[d.index] = d.has_frame ? d.tag : 0;
	}
}

static void test_normal_with_jitter(void)
{
	struct nc_match m;
	uint64_t tags[100] = { 0 }, disp;
	int64_t idx;

	setup(&m, 0);
	CHECK(nc_match_window(&m) == P / 2);
	for (int i = 0; i < 100; i++) {
		edge(&m, i);
		int64_t j = (i % 5 - 2) * MS / 2;  /* -1 .. +1 ms */

		CHECK(frame(&m, i, j, 1000 + i, &idx, &disp) == NC_M_HELD && idx == i);
		collect(&m, T0 + i * P + LAT, tags, 100);
	}
	collect(&m, INT64_MAX, tags, 100);
	for (int i = 0; i < 100; i++)
		CHECK(tags[i] == (uint64_t)(1000 + i));
	CHECK(m.st.bound == 100 && m.st.no_frame == 0 && m.st.no_edge == 0 && m.st.lost == 0);
	CHECK(m.st.lat_min == LAT - MS && m.st.lat_max == LAT + MS);

	int64_t mean;

	CHECK(nc_match_latency_mean(&m, &mean) && mean == LAT);
}

/* An edge is decided only after its window: by a later frame, or by the clock. */
static void test_decide_timing(void)
{
	struct nc_match m;
	struct nc_medge d;
	uint64_t disp;
	int64_t idx;

	setup(&m, 0);
	edge(&m, 0);
	CHECK(frame(&m, 0, 0, 7, &idx, &disp) == NC_M_HELD);
	/* a nearer frame could still come: not decided */
	CHECK(!nc_match_take_decided(&m, T0 + LAT + P / 4, &d));
	/* clock beyond window + grace (one period) */
	CHECK(!nc_match_take_decided(&m, T0 + LAT + P / 2 + P - 1, &d));
	CHECK(nc_match_take_decided(&m, T0 + LAT + P / 2 + P + 1, &d) && d.has_frame && d.tag == 7);
	CHECK(d.t_ns == 5000 * MS && d.session == 3);

	/* a frame past the window decides it at once */
	edge(&m, 1);
	edge(&m, 2);
	CHECK(frame(&m, 1, 0, 8, &idx, &disp) == NC_M_HELD);
	CHECK(frame(&m, 2, 0, 9, &idx, &disp) == NC_M_HELD && idx == 2);
	CHECK(nc_match_take_decided(&m, 0, &d) && d.index == 1 && d.tag == 8);
	CHECK(!nc_match_take_decided(&m, 0, &d));
}

/* Edge without a frame: a gap, never filled by a neighbour's frame. */
static void test_dropped_frame(void)
{
	struct nc_match m;
	uint64_t tags[10] = { 0 }, disp;
	int64_t idx;

	setup(&m, 0);
	for (int i = 0; i < 10; i++) {
		edge(&m, i);
		if (i != 4)
			CHECK(frame(&m, i, 0, 1000 + i, &idx, &disp) == NC_M_HELD && idx == i);
	}
	collect(&m, INT64_MAX, tags, 10);
	CHECK(tags[4] == 0 && tags[3] == 1003 && tags[5] == 1005);
	CHECK(m.st.no_frame == 1 && m.st.bound == 9);
}

/* Extra frame between two edges: loses against the real frame of the nearer edge. */
static void test_extra_frame(void)
{
	struct nc_match m;
	uint64_t tags[10] = { 0 }, disp;
	int64_t idx;

	setup(&m, 0);
	for (int i = 0; i < 4; i++)
		edge(&m, i);
	CHECK(frame(&m, 0, 0, 1000, &idx, &disp) == NC_M_HELD);
	/* 0.4 P after edge 0's frame: nearer edge 0, which has a nearer frame */
	CHECK(frame(&m, 0, P * 4 / 10, 66, &idx, &disp) == NC_M_LOST && idx == 0);
	CHECK(frame(&m, 1, 0, 1001, &idx, &disp) == NC_M_HELD && idx == 1);
	/* 0.6 P after edge 1's frame: nearer edge 2, arrives before edge 2's frame */
	CHECK(frame(&m, 1, P * 6 / 10, 67, &idx, &disp) == NC_M_HELD && idx == 2);
	CHECK(frame(&m, 2, 0, 1002, &idx, &disp) == NC_M_HELD_DISPLACED && disp == 67);
	CHECK(frame(&m, 3, 0, 1003, &idx, &disp) == NC_M_HELD);
	for (int i = 0; i < 10; i++)
		tags[i] = 0;
	collect(&m, INT64_MAX, tags, 4);
	for (int i = 0; i < 4; i++)
		CHECK(tags[i] == (uint64_t)(1000 + i));
	CHECK(m.st.lost == 2 && m.st.bound == 4);
}

/* Late frame (3/4 period): lands nearer the next edge and loses to its real
 * frame; its own edge is a gap, never a wrong index. */
static void test_late_frame(void)
{
	struct nc_match m;
	uint64_t tags[4] = { 0 }, disp;
	int64_t idx;

	setup(&m, 0);
	for (int i = 0; i < 4; i++)
		edge(&m, i);
	CHECK(frame(&m, 0, 0, 1000, &idx, &disp) == NC_M_HELD);
	CHECK(frame(&m, 1, P * 3 / 4, 1001, &idx, &disp) == NC_M_HELD && idx == 2);
	CHECK(frame(&m, 2, 0, 1002, &idx, &disp) == NC_M_HELD_DISPLACED && disp == 1001);
	CHECK(frame(&m, 3, 0, 1003, &idx, &disp) == NC_M_HELD);
	collect(&m, INT64_MAX, tags, 4);
	CHECK(tags[0] == 1000 && tags[1] == 0 && tags[2] == 1002 && tags[3] == 1003);
	CHECK(m.st.no_frame == 1 && m.st.lost == 1);
}

/*
 * Known limit of a half-period window: if the real frame is missing, an extra
 * frame in that window takes the edge. A tighter window rejects it.
 */
static void test_extra_with_drop_window(void)
{
	struct nc_match m;
	uint64_t tags[3] = { 0 }, disp;
	int64_t idx;

	setup(&m, 0);
	for (int i = 0; i < 3; i++)
		edge(&m, i);
	CHECK(frame(&m, 0, 0, 1000, &idx, &disp) == NC_M_HELD);
	CHECK(frame(&m, 0, P * 6 / 10, 66, &idx, &disp) == NC_M_HELD && idx == 1);  /* wrong */
	CHECK(frame(&m, 2, 0, 1002, &idx, &disp) == NC_M_HELD);
	collect(&m, INT64_MAX, tags, 3);
	CHECK(tags[1] == 66);

	setup(&m, 3 * MS);  /* --cam-window-ms 3 */
	for (int i = 0; i < 3; i++)
		edge(&m, i);
	CHECK(frame(&m, 0, 0, 1000, &idx, &disp) == NC_M_HELD);
	CHECK(frame(&m, 0, P * 6 / 10, 66, &idx, &disp) == NC_M_NO_EDGE);
	CHECK(frame(&m, 2, 0, 1002, &idx, &disp) == NC_M_HELD);
	tags[1] = 99;
	collect(&m, INT64_MAX, tags, 3);
	CHECK(tags[1] == 0 && m.st.no_frame == 1 && m.st.no_edge == 1);
}

/* Latency set wrong by 5 ms: still bound (inside the window), and measured. */
static void test_latency_off(void)
{
	struct nc_match m;
	uint64_t tags[50] = { 0 }, disp;
	int64_t idx, mean;

	setup(&m, 0);
	for (int i = 0; i < 50; i++) {
		edge(&m, i);
		CHECK(frame(&m, i, 5 * MS, 1000 + i, &idx, &disp) == NC_M_HELD && idx == i);
	}
	collect(&m, INT64_MAX, tags, 50);
	CHECK(m.st.bound == 50 && nc_match_latency_mean(&m, &mean) && mean == LAT + 5 * MS);
}

static void test_no_edges(void)
{
	struct nc_match m;
	uint64_t disp;
	int64_t idx, mean;
	struct nc_medge d;

	setup(&m, 0);
	CHECK(frame(&m, 0, 0, 1, &idx, &disp) == NC_M_NO_EDGE);
	CHECK(!nc_match_take_decided(&m, INT64_MAX, &d));
	CHECK(!nc_match_latency_mean(&m, &mean));
	for (int i = 0; i < NC_MATCH_EDGES; i++)
		edge(&m, i);
	struct nc_medge e = { .index = 999 };

	CHECK(!nc_match_edge(&m, &e) && m.st.edge_overflow == 1);
}

int main(void)
{
	test_normal_with_jitter();
	test_decide_timing();
	test_dropped_frame();
	test_extra_frame();
	test_late_frame();
	test_extra_with_drop_window();
	test_latency_off();
	test_no_edges();
	if (failures) {
		fprintf(stderr, "test_match: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_match: all passed\n");
	return 0;
}
