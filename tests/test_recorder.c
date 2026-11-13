/*
 * Unit tests for the recorder: manifest records, segment cut (DL-18), ring
 * reclaim order, locks and lock cap, torn manifest tail, crash recovery,
 * session reuse (DL-20). Each test works in its own temp directory.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fwt_sei.h"
#include "rec_scan.h"
#include "recorder.h"

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
#define PAYLOAD 1000

static FILE *quiet_log;

/* One access unit like the encoder + nodecap produce: [VPS SPS PPS] SEI slice. */
static size_t make_au(uint8_t *b, int64_t index, bool key)
{
	size_t n = 0;

	if (key) {
		static const uint8_t ps[] = { 0, 0, 0, 1, 32 << 1, 1, 0x11,
					      0, 0, 0, 1, 33 << 1, 1, 0x22,
					      0, 0, 0, 1, 34 << 1, 1, 0x33 };
		memcpy(b, ps, sizeof(ps));
		n = sizeof(ps);
	}
	struct fwt_sei s = { FWT_SEI_VERSION, 1, 3, index, 1000 + index * P30 };

	n += fwt_sei_build(&s, b + n, FWT_SEI_NAL_MAX);
	b[n++] = 0; b[n++] = 0; b[n++] = 0; b[n++] = 1;
	b[n++] = (uint8_t)((key ? 19 : 1) << 1);
	b[n++] = 1;
	b[n++] = 0x80;  /* first_slice_segment_in_pic_flag */
	memset(b + n, 0x55, PAYLOAD);
	return n + PAYLOAD;
}

/* Encoder-like stream: a keyframe every 30 *output* frames. */
struct gen {
	uint64_t out;
};

static int put(struct recorder *r, struct gen *g, int64_t index)
{
	uint8_t b[PAYLOAD + 256];
	bool key = g->out++ % 30 == 0;
	struct iovec v = { b, make_au(b, index, key) };

	return rec_write_frame(r, &v, 1, index, 1000 + index * P30, key);
}

static char *tmpdir(void)
{
	static char d[64];

	snprintf(d, sizeof(d), "/tmp/test_recorder.XXXXXX");
	return mkdtemp(d);
}

static struct rec_config cfg(const char *dir, uint64_t ring_max, int cap_pct)
{
	return (struct rec_config){ .dir = dir, .ring_max_bytes = ring_max, .seg_seconds = 4,
				    .lock_cap_pct = cap_pct, .min_seg_bytes = 50000,
				    .log = quiet_log };
}

static bool exists(const struct recorder *r, uint32_t s, int64_t idx)
{
	char p[512];
	struct stat st;

	rec_seg_path(r, s, idx, p, sizeof(p));
	return stat(p, &st) == 0;
}

static size_t live_segs(const struct recorder *r, uint32_t s)
{
	size_t n = 0;

	for (size_t i = 0; i < r->nsegs; i++)
		n += r->segs[i].session == s && !r->segs[i].deleted;
	return n;
}

static void test_record(void)
{
	struct rec_record x;

	rec_record_init(&x, REC_SEGMENT, 7);
	x.seq = 1;
	x.u.seg.first_idx = 120;
	rec_record_seal(&x);
	CHECK(rec_record_valid(&x));
	x.u.seg.first_idx = 121;                 /* any change breaks the CRC */
	CHECK(!rec_record_valid(&x));
	CHECK(rec_crc32("123456789", 9) == 0xcbf43926u);  /* standard check value */
}

static void test_basic(void)
{
	char *d = tmpdir();
	struct rec_config c = cfg(d, 0, 20);
	struct recorder r;
	struct gen g = { 0 };

	CHECK(rec_open(&r, &c) == 0);
	CHECK(rec_session_start(&r, 5, 1000, P30) == 0);
	CHECK(r.seg_frames == 120);
	for (int64_t i = 0; i < 300; i++)
		CHECK(put(&r, &g, i) == 0);
	CHECK(rec_session_end(&r) == 0);
	CHECK(r.nsegs == 3);
	CHECK(r.segs[0].i.first_idx == 0 && r.segs[0].i.last_idx == 119);
	CHECK(r.segs[1].i.first_idx == 120 && r.segs[2].i.first_idx == 240);
	CHECK(r.segs[2].i.frames == 60);
	CHECK(r.segs[0].i.n_keys == 4 && r.segs[0].i.key_didx[1] == 30);
	CHECK(exists(&r, 5, 0) && exists(&r, 5, 120) && exists(&r, 5, 240));
	CHECK(rec_find_session(&r, 5)->status == REC_ST_COMPLETE);
	CHECK(rec_find_session(&r, 5)->frames == 300);

	/* Another recorder can't take /rec while this one owns it. */
	struct recorder r2;

	CHECK(rec_open(&r2, &c) == -EBUSY);
	rec_close(&r);

	/* Reopen: same index from the manifest; reusing session 5 is refused. */
	CHECK(rec_open(&r, &c) == 0);
	CHECK(r.nsegs == 3 && r.nsess == 1 && r.segs[1].i.bytes > 0);
	CHECK(rec_session_start(&r, 5, 1000, P30) == -EEXIST);

	/* Scanner agrees with the record. */
	char p[512];
	size_t len;
	struct rec_pic *pics;

	rec_seg_path(&r, 5, 120, p, sizeof(p));
	uint8_t *buf = rec_read_file(p, &len);
	long n = rec_scan(buf, len, &pics);

	CHECK(n == 120 && len == r.segs[1].i.bytes);
	CHECK(pics[0].key && pics[0].au_off == 0 && pics[0].sei.index == 120);
	CHECK(pics[30].key && pics[30].au_off == r.segs[1].i.key_off[1]);
	free(pics);
	free(buf);
	rec_close(&r);
}

/* DL-18: a drop shifts the encoder's keyframes; the cut waits for one. */
static void test_cut_after_drop(void)
{
	char *d = tmpdir();
	struct rec_config c = cfg(d, 0, 20);
	struct recorder r;
	struct gen g = { 0 };

	CHECK(rec_open(&r, &c) == 0);
	CHECK(rec_session_start(&r, 6, 1000, P30) == 0);
	for (int64_t i = 0; i < 260; i++)
		if (i != 100)
			CHECK(put(&r, &g, i) == 0);
	CHECK(rec_session_end(&r) == 0);
	/* outputs 0,30,60,90,120 are keys -> index 121 is the first key >= 120 */
	CHECK(r.nsegs == 3);
	CHECK(r.segs[0].i.last_idx == 120 && r.segs[0].i.missing == 1);
	CHECK(r.segs[1].i.first_idx == 121);
	CHECK(exists(&r, 6, 121));
	CHECK(rec_find_session(&r, 6)->missing == 1);
	rec_close(&r);
}

/* Ring: closed session goes first (whole), then the running one's oldest. */
static void test_ring(void)
{
	char *d = tmpdir();
	struct rec_config c = cfg(d, 700000, 50);  /* ~5 segments of ~130 KB */
	struct recorder r;
	struct gen g = { 0 };

	CHECK(rec_open(&r, &c) == 0);
	CHECK(rec_session_start(&r, 1, 1000, P30) == 0);
	for (int64_t i = 0; i < 360; i++)
		put(&r, &g, i);
	rec_session_end(&r);
	CHECK(live_segs(&r, 1) == 3);

	g.out = 0;
	CHECK(rec_session_start(&r, 2, 1000, P30) == 0);
	for (int64_t i = 0; i < 720; i++)
		CHECK(put(&r, &g, i) == 0);
	CHECK(live_segs(&r, 1) == 0);                /* closed session deleted whole */
	CHECK(!exists(&r, 1, 0) && !exists(&r, 1, 240));
	CHECK(live_segs(&r, 2) >= 3 && live_segs(&r, 2) < 6);
	CHECK(!exists(&r, 2, 0));                    /* running session's oldest went too */
	CHECK(rec_used_bytes(&r) <= 700000);
	rec_session_end(&r);
	CHECK(rec_used_bytes(&r) <= 700000);
	rec_close(&r);
}

static void test_locks(void)
{
	char *d = tmpdir();
	struct rec_config c = cfg(d, 700000, 50);
	struct recorder r;
	struct gen g = { 0 };
	uint64_t id = 0, id2 = 0;
	char why[160];

	CHECK(rec_open(&r, &c) == 0);
	CHECK(rec_session_start(&r, 1, 1000, P30) == 0);
	for (int64_t i = 0; i < 360; i++)
		put(&r, &g, i);
	rec_session_end(&r);

	/* Lock the middle segment of session 1. */
	CHECK(rec_lock(&r, 1, 130, 140, 3600, "event A", &id, why, sizeof(why)) == 0);
	CHECK(rec_lock(&r, 9, 0, 1, 3600, "x", &id2, why, sizeof(why)) == -ENOENT);
	/* Locking everything would exceed 50 % of the ring: refused. */
	CHECK(rec_lock(&r, 1, 0, 359, 3600, "too much", &id2, why, sizeof(why)) == -EDQUOT);
	CHECK(r.st.locks_refused == 1);

	/* Session 2 with a post-event window reaching into its future. */
	g.out = 0;
	CHECK(rec_session_start(&r, 2, 1000, P30) == 0);
	for (int64_t i = 0; i < 100; i++)
		put(&r, &g, i);
	CHECK(rec_lock(&r, 2, 90, 200, 3600, "post-event", &id2, why, sizeof(why)) == 0);
	for (int64_t i = 100; i < 720; i++)
		CHECK(put(&r, &g, i) == 0);
	rec_session_end(&r);

	CHECK(exists(&r, 1, 120));                   /* locked: survived the ring */
	CHECK(!exists(&r, 1, 0) && !exists(&r, 1, 240));
	CHECK(exists(&r, 2, 0) && exists(&r, 2, 120));  /* in [90, 200]: kept */
	CHECK(rec_used_bytes(&r) <= 700000);

	/* Unlock: segment becomes reclaimable; lock state survives a reopen. */
	CHECK(rec_unlock(&r, id) == 0);
	CHECK(rec_unlock(&r, id) == -ENOENT);
	rec_close(&r);
	CHECK(rec_open(&r, &c) == 0);
	CHECK(r.nlocks == 2 && !r.locks[0].active && r.locks[1].active);
	rec_close(&r);
}

/* Power loss mid-append: the torn record is cut off at the next start. */
static void test_torn_tail(void)
{
	char *d = tmpdir(), p[300];
	struct rec_config c = cfg(d, 0, 20);
	struct recorder r;
	struct gen g = { 0 };

	CHECK(rec_open(&r, &c) == 0);
	CHECK(rec_session_start(&r, 3, 1000, P30) == 0);
	for (int64_t i = 0; i < 130; i++)
		put(&r, &g, i);
	rec_session_end(&r);
	size_t nrec = (size_t)r.seq;
	rec_close(&r);

	snprintf(p, sizeof(p), "%s/%s", d, REC_MANIFEST_NAME);
	int fd = open(p, O_WRONLY | O_APPEND);

	CHECK(write(fd, "torn record garbage", 19) == 19);
	close(fd);

	CHECK(rec_open(&r, &c) == 0);
	CHECK(r.st.torn_bytes_cut == 19);
	CHECK(r.seq == nrec);
	struct stat st;

	CHECK(stat(p, &st) == 0 && (size_t)st.st_size == nrec * REC_SIZE);
	rec_close(&r);
}

/* Crash mid-session: the open segment is rebuilt from its SEIs. */
static void test_recovery(void)
{
	char *d = tmpdir();
	struct rec_config c = cfg(d, 0, 20);
	struct recorder r;
	struct gen g = { 0 };

	CHECK(rec_open(&r, &c) == 0);
	CHECK(rec_session_start(&r, 4, 1000, P30) == 0);
	for (int64_t i = 0; i < 220; i++)   /* segment 0 closed, segment 120 open */
		put(&r, &g, i);
	rec_abandon(&r);                    /* crash: no records for the open one */

	CHECK(rec_open(&r, &c) == 0);
	CHECK(r.st.recovered_segments == 1);
	const struct rec_sess *s = rec_find_session(&r, 4);

	CHECK(s && s->status == REC_ST_INTERRUPTED);
	CHECK(r.nsegs == 2);
	/* keys at 120, 150, 180, 210: the last group (210..219) is dropped */
	CHECK(r.segs[1].i.first_idx == 120 && r.segs[1].i.last_idx == 209);
	CHECK(r.segs[1].i.frames == 90 && (r.segs[1].i.flags & REC_SEG_F_RECOVERED));
	CHECK(r.segs[1].i.n_keys == 3);
	CHECK(s->last_index == 209 && s->frames == 210);

	char p[512];
	struct stat st;

	rec_seg_path(&r, 4, 120, p, sizeof(p));
	CHECK(stat(p, &st) == 0 && (uint64_t)st.st_size == r.segs[1].i.bytes);
	rec_close(&r);

	/* A second start finds nothing more to do. */
	CHECK(rec_open(&r, &c) == 0);
	CHECK(r.st.recovered_segments == 0 && r.nsegs == 2);
	rec_close(&r);
}

int main(void)
{
	quiet_log = getenv("TEST_VERBOSE") ? stderr : NULL;
	test_record();
	test_basic();
	test_cut_after_drop();
	test_ring();
	test_locks();
	test_torn_tail();
	test_recovery();
	if (failures) {
		fprintf(stderr, "test_recorder: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_recorder: all passed\n");
	return 0;
}
