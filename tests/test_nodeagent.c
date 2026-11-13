/* Unit tests for nodeagent's fetch planning (keyframe-aligned, gaps, limits). */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "na_fetch.h"

static int failures;

#define CHECK(cond)                                                          \
	do {                                                                 \
		if (!(cond)) {                                               \
			fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__,     \
				__LINE__, #cond);                            \
			failures++;                                          \
		}                                                            \
	} while (0)

#define P30 33333333LL

static struct rec_seg segs[4];
static struct rec_sess sess[1];
static struct recorder idx;
static struct na_fetch_plan p;

/* A segment with keyframes every 30 indices from first_idx, 1000 bytes each frame. */
static struct rec_seg seg(int64_t first, int64_t last, bool deleted)
{
	struct rec_seg s = { .session = 7, .deleted = deleted };

	s.i.first_idx = first;
	s.i.last_idx = last;
	s.i.frames = (uint16_t)(last - first + 1);
	s.i.bytes = (uint64_t)s.i.frames * 1000;
	for (int64_t k = first; k <= last && s.i.n_keys < REC_MAX_KEYS; k += 30) {
		s.i.key_off[s.i.n_keys] = (uint32_t)((k - first) * 1000);
		s.i.key_didx[s.i.n_keys] = (uint16_t)(k - first);
		s.i.n_keys++;
	}
	return s;
}

static void setup(uint32_t status)
{
	segs[0] = seg(0, 119, false);
	segs[1] = seg(121, 240, false);     /* index 120 missing (drop) */
	segs[2] = seg(241, 360, true);      /* reclaimed by the ring */
	segs[3] = seg(361, 480, false);
	sess[0] = (struct rec_sess){ .session = 7, .period_ns = P30, .status = status };
	idx = (struct recorder){ .segs = segs, .nsegs = 4, .sess = sess, .nsess = 1 };
}

int main(void)
{
	char buf[256];

	setup(REC_ST_COMPLETE);

	/* Start inside a segment: first keyframe at or after 45 is 60. */
	CHECK(na_fetch_plan(&idx, 7, 45, 200, 0, 600, &p) == 0);
	CHECK(p.from == 60 && p.to == 240 && p.nparts == 2);
	CHECK(p.parts[0].first_idx == 0 && p.parts[0].offset == 60000 && p.parts[0].len == 60000);
	CHECK(p.parts[1].first_idx == 121 && p.parts[1].offset == 0);
	CHECK(p.bytes == 60000 + 120000);
	na_ranges_str(&p, buf, sizeof(buf));
	CHECK(!strcmp(buf, "45-59,120-120"));     /* before the keyframe; the drop */
	CHECK(!p.open_segment_pending);

	/* No keyframe left in the first segment after 95: start at the next one. */
	CHECK(na_fetch_plan(&idx, 7, 95, 130, 0, 600, &p) == 0);
	CHECK(p.from == 121 && p.nparts == 1);
	na_ranges_str(&p, buf, sizeof(buf));
	CHECK(!strcmp(buf, "95-120"));

	/* Across the reclaimed segment: reported missing, not shortened silently. */
	CHECK(na_fetch_plan(&idx, 7, 200, 400, 0, 600, &p) == 0);
	CHECK(p.from == 211 && p.to == 480 && p.nparts == 2);  /* keys 121, 151, 181, 211 */
	na_ranges_str(&p, buf, sizeof(buf));
	CHECK(!strcmp(buf, "200-210,241-360"));

	/* Last 2 s: from the end of what is available, keyframe-aligned. */
	CHECK(na_fetch_plan(&idx, 7, 0, -1, 2, 600, &p) == 0);
	CHECK(p.to == 480 && p.from == 421);      /* 480 - 60 + 1 = 421, a keyframe */
	CHECK(p.nparts == 1 && p.parts[0].offset == 60000);

	/* Running session asked beyond the last closed segment: open segment pending. */
	setup(REC_ST_RECORDING);
	CHECK(na_fetch_plan(&idx, 7, 400, 600, 0, 600, &p) == 0);
	CHECK(p.open_segment_pending && p.to == 480);
	na_ranges_str(&p, buf, sizeof(buf));
	CHECK(!strcmp(buf, "400-420,481-600"));

	/* Errors */
	setup(REC_ST_COMPLETE);
	CHECK(na_fetch_plan(&idx, 9, 0, 10, 0, 600, &p) == -ENOENT);    /* unknown session */
	CHECK(na_fetch_plan(&idx, 7, 241, 360, 0, 600, &p) == -ENOENT); /* only deleted data */
	CHECK(na_fetch_plan(&idx, 7, 10, 5, 0, 600, &p) == -EINVAL);    /* to < from */
	CHECK(na_fetch_plan(&idx, 7, 0, 100, 0, 1, &p) == -E2BIG);      /* > 1 s at 30 fps */

	if (failures) {
		fprintf(stderr, "test_nodeagent: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_nodeagent: all passed\n");
	return 0;
}
