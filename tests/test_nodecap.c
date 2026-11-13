/* Unit tests for nodecap's pure logic: edge table, timestamps, test frames. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fwt_stamp.h"
#include "nodecap_core.h"

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

static void test_table(void)
{
	static struct nc_edge_table t;
	struct nc_edge e = { 0 }, got;

	nc_table_init(&t);
	for (int64_t i = 100; i < 110; i++) {
		e.index = i;
		e.t_ns = i * 1000;
		nc_table_put(&t, &e);
	}
	CHECK(nc_table_take(&t, 105, &got) && got.t_ns == 105000);
	CHECK(!nc_table_take(&t, 105, &got));            /* taken once only */
	CHECK(!nc_table_take(&t, 99, &got));             /* never put */
	CHECK(t.overwritten == 0);

	e.index = 100 + NC_TABLE_SIZE;                   /* same slot as 100 */
	nc_table_put(&t, &e);
	CHECK(t.overwritten == 1);
	CHECK(!nc_table_take(&t, 100, &got));            /* replaced */
	CHECK(nc_table_take(&t, 100 + NC_TABLE_SIZE, &got));
}

static void test_time(void)
{
	const int64_t p = 33333333LL, t0 = 1731400000LL * S;
	int64_t idx = -1;

	CHECK(nc_t0_from_edge(t0 + 90 * p, 90, p) == t0);
	CHECK(nc_pts_of(t0 + 90 * p, t0) == 90 * p);
	CHECK(nc_index_of_pts(90 * p, p, &idx) && idx == 90);
	CHECK(nc_index_of_pts(90 * p + 1000, p, &idx) && idx == 90);     /* +1 us */
	CHECK(nc_index_of_pts(90 * p - 1000, p, &idx) && idx == 90);     /* -1 us */
	CHECK(nc_index_of_pts(90 * p + p / 2 + 1, p, &idx) && idx == 91);
	CHECK(!nc_index_of_pts(0, 0, &idx));
	/* The encoder hands timestamps through V4L2 in microseconds. */
	CHECK(nc_index_of_pts((90 * p / 1000) * 1000, p, &idx) && idx == 90);

	CHECK(nc_fps_of(33333333LL) == 30);
	CHECK(nc_fps_of(16666666LL) == 60);
	CHECK(nc_fps_of(0) == 0);
}

static void test_frames(void)
{
	const int w = 1920, h = 1200;
	size_t n = nc_nv12_size(w, h);
	uint8_t *a = malloc(n), *b = malloc(n);
	struct fwt_stamp_geom g;
	uint32_t v = 0;

	CHECK(n == 1920u * 1200 * 3 / 2);
	nc_testframe_fill(a, w, h, 0);
	nc_testframe_fill(b, w, h, 1);
	CHECK(memcmp(a, b, n) != 0);                     /* patterns differ: motion */

	CHECK(fwt_stamp_geom_default(w, h, &g));
	CHECK(fwt_stamp_draw(a, w, a + (size_t)w * h, w, w, h, &g, 4242));
	CHECK(fwt_stamp_read(a, w, w, h, &g, &v) && v == 4242);
	free(a);
	free(b);
}

int main(void)
{
	test_table();
	test_time();
	test_frames();
	if (failures) {
		fprintf(stderr, "test_nodecap: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_nodecap: all passed\n");
	return 0;
}
