#include "nodecap_core.h"

#include <string.h>

#include "fwt_index.h"

void nc_table_init(struct nc_edge_table *t)
{
	memset(t, 0, sizeof(*t));
}

static size_t slot(int64_t index)
{
	return (size_t)((uint64_t)index % NC_TABLE_SIZE);
}

void nc_table_put(struct nc_edge_table *t, const struct nc_edge *e)
{
	struct nc_edge *s = &t->e[slot(e->index)];

	if (s->used)
		t->overwritten++;
	*s = *e;
	s->used = true;
}

bool nc_table_take(struct nc_edge_table *t, int64_t index, struct nc_edge *out)
{
	struct nc_edge *s = &t->e[slot(index)];

	if (!s->used || s->index != index)
		return false;
	*out = *s;
	s->used = false;
	return true;
}

int64_t nc_t0_from_edge(int64_t t_ns, int64_t index, int64_t period_ns)
{
	return t_ns - index * period_ns;
}

int64_t nc_pts_of(int64_t t_ns, int64_t t0_ns)
{
	return t_ns - t0_ns;
}

bool nc_index_of_pts(int64_t pts_ns, int64_t period_ns, int64_t *index)
{
	const struct fwt_plan p = { .t0_ns = 0, .period_ns = period_ns };

	return fwt_index_of(&p, pts_ns, index);
}

int nc_fps_of(int64_t period_ns)
{
	return period_ns > 0 ? (int)((1000000000LL + period_ns / 2) / period_ns) : 0;
}

size_t nc_nv12_size(int w, int h)
{
	return (size_t)w * h * 3 / 2;
}

void nc_testframe_fill(uint8_t *frame, int w, int h, int k)
{
	uint8_t *y = frame, *uv = frame + (size_t)w * h;
	int bar_x = (k * w / 8) % w, bar_w = w / 16;

	/* Diagonal gradient shifted per pattern, plus a moving bright bar. */
	for (int r = 0; r < h; r++) {
		uint8_t *line = y + (size_t)r * w;

		for (int c = 0; c < w; c++) {
			int v = 40 + ((r + c + k * 24) & 0x7f);

			if (c >= bar_x && c < bar_x + bar_w)
				v = 220;
			line[c] = (uint8_t)v;
		}
	}
	for (int r = 0; r < h / 2; r++) {
		uint8_t *line = uv + (size_t)r * w;

		for (int c = 0; c < w; c += 2) {
			line[c] = (uint8_t)(96 + ((c / 2 + k * 8) & 0x3f));      /* U */
			line[c + 1] = (uint8_t)(96 + ((r + k * 8) & 0x3f));      /* V */
		}
	}
}
