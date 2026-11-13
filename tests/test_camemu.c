/* Unit tests for the camera emulator logic and its protocol (DL-26). */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include "camemu_core.h"
#include "fwt_cam_proto.h"
#include "fwt_stamp.h"

static int failures;

#define CHECK(cond)                                                          \
	do {                                                                 \
		if (!(cond)) {                                               \
			fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__,     \
				__LINE__, #cond);                            \
			failures++;                                          \
		}                                                            \
	} while (0)

#define MS 1000000LL
#define PERIOD (1000000000LL / 60)

static void test_plan_no_faults(void)
{
	struct ce_faults f = { .latency_ns = 25 * MS };
	struct ce_rng r;
	struct ce_event ev[2];
	bool dropped;

	ce_rng_seed(&r, 1);
	int n = ce_plan_edge(&f, &r, 1000 * MS, 7, PERIOD, ev, &dropped);

	CHECK(n == 1 && !dropped);
	CHECK(ev[0].due_ns == 1025 * MS && ev[0].index == 7 && ev[0].kind == CE_FRAME);
}

static void test_plan_jitter_bounds(void)
{
	struct ce_faults f = { .latency_ns = 25 * MS, .jitter_ns = 2 * MS };
	struct ce_rng r;
	struct ce_event ev[2];
	bool dropped, below = false, above = false;

	ce_rng_seed(&r, 2);
	for (int k = 0; k < 10000; k++) {
		ce_plan_edge(&f, &r, 0, k, PERIOD, ev, &dropped);
		CHECK(ev[0].due_ns >= 23 * MS && ev[0].due_ns <= 27 * MS);
		below |= ev[0].due_ns < 24 * MS;
		above |= ev[0].due_ns > 26 * MS;
	}
	CHECK(below && above);
}

/* Fault rates come out near the asked percentage; kinds land where they should. */
static void test_plan_faults(void)
{
	struct ce_faults f = { .latency_ns = 25 * MS, .drop_pct = 5, .extra_pct = 10,
			       .late_pct = 20 };
	struct ce_rng r;
	struct ce_event ev[2];
	bool dropped;
	int drops = 0, extras = 0, lates = 0, N = 100000;

	ce_rng_seed(&r, 3);
	for (int k = 0; k < N; k++) {
		int n = ce_plan_edge(&f, &r, 0, k, PERIOD, ev, &dropped);

		drops += dropped;
		CHECK(n == (dropped ? 0 : 1) + (n > 0 && ev[n - 1].kind == CE_SPURIOUS));
		for (int j = 0; j < n; j++) {
			int64_t after = ev[j].due_ns - 25 * MS;

			if (ev[j].kind == CE_SPURIOUS) {
				extras++;
				CHECK(after >= PERIOD * 3 / 10 && after <= PERIOD * 7 / 10);
			} else if (ev[j].kind == CE_LATE) {
				lates++;
				CHECK(after == PERIOD * 3 / 4);
			} else {
				CHECK(after == 0);
			}
		}
	}
	CHECK(drops > N * 4 / 100 && drops < N * 6 / 100);
	CHECK(extras > N * 9 / 100 && extras < N * 11 / 100);
	/* late is decided for kept frames only: 20 % of 95 % */
	CHECK(lates > N * 17 / 100 && lates < N * 21 / 100);
}

static void test_seed_reproducible(void)
{
	struct ce_rng a, b;

	ce_rng_seed(&a, 42);
	ce_rng_seed(&b, 42);
	for (int k = 0; k < 100; k++)
		CHECK(ce_rand(&a) == ce_rand(&b));
}

static void test_sched_order(void)
{
	struct ce_sched s;
	struct ce_event e, out;
	int64_t times[] = { 50, 10, 30, 20, 40 };

	ce_sched_init(&s);
	CHECK(ce_sched_next(&s) == INT64_MAX);
	for (int k = 0; k < 5; k++) {
		e = (struct ce_event){ .due_ns = times[k], .index = k };
		CHECK(ce_sched_add(&s, &e));
	}
	CHECK(ce_sched_next(&s) == 10);
	CHECK(!ce_sched_pop_due(&s, 9, &out));
	CHECK(ce_sched_pop_due(&s, 35, &out) && out.due_ns == 10);
	CHECK(ce_sched_pop_due(&s, 35, &out) && out.due_ns == 20);
	CHECK(ce_sched_pop_due(&s, 35, &out) && out.due_ns == 30);
	CHECK(!ce_sched_pop_due(&s, 35, &out));
	CHECK(s.n == 2);

	ce_sched_init(&s);
	for (int k = 0; k < CE_SCHED_MAX; k++)
		ce_sched_add(&s, &e);
	CHECK(!ce_sched_add(&s, &e) && s.overflow == 1);
}

static void test_bufq(void)
{
	struct ce_bufq q;

	ce_bufq_init(&q, 4);
	CHECK(ce_bufq_take(&q) == -1);
	CHECK(ce_bufq_queue(&q, 4) == -EINVAL);
	CHECK(ce_bufq_queue(&q, -1) == -EINVAL);
	CHECK(ce_bufq_queue(&q, 2) == 0);
	CHECK(ce_bufq_queue(&q, 2) == -EBUSY);
	CHECK(ce_bufq_queue(&q, 0) == 0);
	CHECK(ce_bufq_queue(&q, 3) == 0);
	CHECK(ce_bufq_take(&q) == 2);  /* queue order */
	CHECK(ce_bufq_queue(&q, 2) == 0);
	CHECK(ce_bufq_take(&q) == 0);
	CHECK(ce_bufq_take(&q) == 3);
	CHECK(ce_bufq_take(&q) == 2);
	CHECK(ce_bufq_take(&q) == -1);
	ce_bufq_queue(&q, 1);
	ce_bufq_clear(&q);
	CHECK(ce_bufq_take(&q) == -1 && q.count == 4);
}

/* V4L2 behaviour: no buffer -> frame lost, sequence still advances. */
static void test_deliver(void)
{
	struct ce_faults f = { 0 };
	struct ce_stream st = { 0 };
	struct ce_bufq q;
	struct ce_rng r;
	uint32_t seq;

	ce_rng_seed(&r, 5);
	ce_bufq_init(&q, 2);
	ce_stream_on(&st);
	ce_bufq_queue(&q, 0);
	CHECK(ce_deliver(&st, &q, &f, &r, &seq) == 0 && seq == 0);
	CHECK(ce_deliver(&st, &q, &f, &r, &seq) == -1 && seq == 1);
	ce_bufq_queue(&q, 1);
	CHECK(ce_deliver(&st, &q, &f, &r, &seq) == 1 && seq == 2);
	CHECK(st.delivered == 2 && st.lost_nobuf == 1);

	ce_stream_on(&st);  /* STREAMON resets the sequence */
	ce_bufq_queue(&q, 0);
	CHECK(ce_deliver(&st, &q, &f, &r, &seq) == 0 && seq == 0);

	/* jumps: always 2..4 ahead, never a lost buffer */
	f.seqjump_pct = 100;
	uint32_t prev = seq;

	for (int k = 0; k < 100; k++) {
		ce_bufq_queue(&q, 0);
		CHECK(ce_deliver(&st, &q, &f, &r, &seq) == 0);
		CHECK(seq - prev >= 2 && seq - prev <= 4);
		prev = seq;
	}
	CHECK(st.seq_jumps == 100);
}

/* Noise and motion don't break the drawn index (ground truth). */
static void test_pattern_stamp(void)
{
	const int w = 1920, h = 1200;
	struct ce_pattern p;
	struct ce_rng r;
	struct fwt_stamp_geom g;
	uint8_t *a = malloc((size_t)w * h * 3 / 2), *b = malloc((size_t)w * h * 3 / 2);
	uint32_t v = 0;

	ce_rng_seed(&r, 6);
	CHECK(ce_pattern_init(&p, w, h, 12, &r) == 0);
	CHECK(fwt_stamp_geom_default(w, h, &g));
	ce_pattern_fill(&p, a, &r);
	ce_pattern_fill(&p, b, &r);
	CHECK(memcmp(a, b, (size_t)w * h) != 0);  /* it moves */
	size_t out_of_range = 0;

	for (size_t i = 0; i < (size_t)w * h; i++)
		out_of_range += a[i] < 16 || a[i] > 235;
	CHECK(out_of_range == 0);  /* luma stays in video range */

	CHECK(fwt_stamp_draw(a, w, a + (size_t)w * h, w, w, h, &g, 123456789u));
	CHECK(fwt_stamp_read(a, w, w, h, &g, &v) && v == 123456789u);
	CHECK(fwt_stamp_draw(b, w, b + (size_t)w * h, w, w, h, &g, FWT_CAM_STAMP_SPURIOUS));
	CHECK(fwt_stamp_read(b, w, w, h, &g, &v) && v == FWT_CAM_STAMP_SPURIOUS);
	ce_pattern_free(&p);
	CHECK(ce_pattern_init(&p, 15, 16, 12, &r) == -EINVAL);
	free(a);
	free(b);
}

/* The protocol over a real SOCK_SEQPACKET pair, shared memory fd included. */
static void test_proto(void)
{
	int sv[2];
	struct fwt_cam_msg m, got;
	int fd = -1;

	CHECK(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) == 0);

	fwt_cam_msg_init(&m, FWT_CAM_FORMAT);
	m.width = 1920;
	m.height = 1200;
	m.fourcc = FWT_CAM_FOURCC_NV12;
	CHECK(fwt_cam_send(sv[0], &m, -1) == 0);
	CHECK(fwt_cam_recv(sv[1], &got, &fd) == 1 && fd == -1);
	CHECK(got.type == FWT_CAM_FORMAT && got.width == 1920 && got.height == 1200);

	/* a buffer written on one side is seen through the passed fd */
	int mfd = memfd_create("test", MFD_CLOEXEC);

	CHECK(mfd >= 0 && ftruncate(mfd, 4096) == 0);
	uint8_t *p1 = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0);

	fwt_cam_msg_init(&m, FWT_CAM_REQBUFS);
	m.count = 1;
	m.buf_size = 4096;
	CHECK(fwt_cam_send(sv[0], &m, mfd) == 0);
	CHECK(fwt_cam_recv(sv[1], &got, &fd) == 1 && fd >= 0 && fd != mfd);
	uint8_t *p2 = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);

	p1[100] = 0xa5;
	CHECK(p2 != MAP_FAILED && p2[100] == 0xa5);
	munmap(p1, 4096);
	munmap(p2, 4096);
	close(fd);

	/* an fd sent to a receiver that doesn't want one is closed, not leaked */
	CHECK(fwt_cam_send(sv[0], &m, mfd) == 0);
	CHECK(fwt_cam_recv(sv[1], &got, NULL) == 1);
	close(mfd);

	/* wrong size, wrong magic */
	uint32_t junk[3] = { 1, 2, 3 };

	CHECK(send(sv[0], junk, sizeof(junk), 0) == sizeof(junk));
	CHECK(fwt_cam_recv(sv[1], &got, NULL) == -EBADMSG);
	fwt_cam_msg_init(&m, FWT_CAM_QBUF);
	m.magic = 0;
	CHECK(fwt_cam_send(sv[0], &m, -1) == 0);
	CHECK(fwt_cam_recv(sv[1], &got, NULL) == -EBADMSG);

	/* peer gone: 0, and sending never raises SIGPIPE */
	close(sv[0]);
	CHECK(fwt_cam_recv(sv[1], &got, NULL) == 0);
	fwt_cam_msg_init(&m, FWT_CAM_QBUF);
	CHECK(fwt_cam_send(sv[1], &m, -1) == -EPIPE);
	close(sv[1]);

	CHECK(fwt_cam_buf_size(1920, 1200) == 3457024 && fwt_cam_buf_size(1920, 1200) % 4096 == 0);
	CHECK(fwt_cam_nv12_size(1920, 1200) == 3456000);
}

int main(void)
{
	test_plan_no_faults();
	test_plan_jitter_bounds();
	test_plan_faults();
	test_seed_reproducible();
	test_sched_order();
	test_bufq();
	test_deliver();
	test_pattern_stamp();
	test_proto();
	if (failures) {
		fprintf(stderr, "test_camemu: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_camemu: all passed\n");
	return 0;
}
