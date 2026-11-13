/*
 * Built-in test source (DL-26 "test"): one frame per trigger edge, made on the
 * spot from 8 pre-made patterns, bound to its edge by construction. nodecap
 * draws the index into its copy of the frame (DL-17), so a frame is copied once.
 */
#include <stdlib.h>
#include <time.h>

#include "nc_source.h"
#include "nodecap_core.h"

#define N_PATTERNS 8

struct src_test {
	struct nc_source base;
	size_t size;
	uint8_t *patterns[N_PATTERNS];
	bool pending;
	struct nc_frame f;
	uint32_t sequence;
};

static int t_fd(struct nc_source *s)
{
	(void)s;
	return -1;  /* frames follow edges */
}

static void t_on_edge(struct nc_source *s, const struct fwt_trig_msg *edge)
{
	struct src_test *t = (struct src_test *)s;
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	t->f = (struct nc_frame){
		.data = t->patterns[(uint64_t)edge->index % N_PATTERNS],
		.len = t->size,
		.t_mono_ns = (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec,
		.sequence = t->sequence++,
		.slot = -1,
		.has_edge = true,
		.edge = *edge,
		.draw_stamp = true,
		.stamp = (uint32_t)edge->index,
	};
	t->pending = true;
}

static int t_dequeue(struct nc_source *s, struct nc_frame *f)
{
	struct src_test *t = (struct src_test *)s;

	if (!t->pending)
		return 0;
	*f = t->f;
	t->pending = false;
	return 1;
}

static void t_release(struct nc_source *s, const struct nc_frame *f)
{
	(void)s;
	(void)f;  /* patterns are reused */
}

static void t_destroy(struct nc_source *s)
{
	struct src_test *t = (struct src_test *)s;

	for (int k = 0; k < N_PATTERNS; k++)
		free(t->patterns[k]);
	free(t);
}

static const struct nc_source_ops test_ops = {
	.fd = t_fd, .on_edge = t_on_edge, .dequeue = t_dequeue,
	.release = t_release, .destroy = t_destroy,
};

struct nc_source *nc_source_test_new(int w, int h)
{
	struct src_test *t = calloc(1, sizeof(*t));

	if (!t)
		return NULL;
	t->base = (struct nc_source){ .ops = &test_ops, .name = "test" };
	t->size = nc_nv12_size(w, h);
	for (int k = 0; k < N_PATTERNS; k++) {
		t->patterns[k] = malloc(t->size);
		if (!t->patterns[k]) {
			t_destroy(&t->base);
			return NULL;
		}
		nc_testframe_fill(t->patterns[k], w, h, k);
	}
	return &t->base;
}
