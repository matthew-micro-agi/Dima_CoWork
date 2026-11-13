#include "nc_match.h"

#include <string.h>

static int64_t iabs64(int64_t v)
{
	return v < 0 ? -v : v;
}

void nc_match_init(struct nc_match *m, const struct nc_match_cfg *cfg)
{
	memset(m, 0, sizeof(*m));
	m->cfg = *cfg;
	if (m->cfg.window_ns <= 0)
		m->cfg.window_ns = cfg->period_ns / 2;
	if (m->cfg.grace_ns <= 0)
		m->cfg.grace_ns = cfg->period_ns;
	m->last_frame_mono = INT64_MIN;
	m->st.lat_min = INT64_MAX;
	m->st.lat_max = INT64_MIN;
}

int64_t nc_match_window(const struct nc_match *m)
{
	return m->cfg.window_ns;
}

bool nc_match_edge(struct nc_match *m, const struct nc_medge *e)
{
	if (m->n == NC_MATCH_EDGES) {
		m->st.edge_overflow++;
		return false;
	}
	m->e[m->n] = *e;
	m->e[m->n].has_frame = false;
	m->n++;
	m->st.edges++;
	return true;
}

enum nc_match_result nc_match_frame(struct nc_match *m, int64_t t_mono_ns, uint64_t tag,
				    int64_t *index, uint64_t *displaced)
{
	struct nc_medge *best = NULL;
	int64_t best_dist = INT64_MAX;

	m->st.frames++;
	if (t_mono_ns > m->last_frame_mono)
		m->last_frame_mono = t_mono_ns;
	for (int k = 0; k < m->n; k++) {
		int64_t d = iabs64(t_mono_ns - (m->e[k].t_mono_ns + m->cfg.latency_ns));

		if (d < best_dist) {
			best_dist = d;
			best = &m->e[k];
		}
	}
	if (!best || best_dist > m->cfg.window_ns) {
		m->st.no_edge++;
		return NC_M_NO_EDGE;
	}
	*index = best->index;
	if (best->has_frame && best->dist_ns <= best_dist) {
		m->st.lost++;
		return NC_M_LOST;
	}

	enum nc_match_result r = NC_M_HELD;

	if (best->has_frame) {
		*displaced = best->tag;
		m->st.lost++;
		r = NC_M_HELD_DISPLACED;
	}
	best->has_frame = true;
	best->tag = tag;
	best->frame_mono_ns = t_mono_ns;
	best->dist_ns = best_dist;
	return r;
}

bool nc_match_take_decided(struct nc_match *m, int64_t now_mono_ns, struct nc_medge *out)
{
	if (!m->n)
		return false;

	const struct nc_medge *e = &m->e[0];
	int64_t end = e->t_mono_ns + m->cfg.latency_ns + m->cfg.window_ns;
	bool decided = now_mono_ns == INT64_MAX || m->last_frame_mono > end ||
		       now_mono_ns > end + m->cfg.grace_ns;

	if (!decided)
		return false;
	*out = *e;
	memmove(&m->e[0], &m->e[1], (size_t)(m->n - 1) * sizeof(m->e[0]));
	m->n--;
	if (out->has_frame) {
		int64_t lat = out->frame_mono_ns - out->t_mono_ns;

		m->st.bound++;
		m->st.lat_sum += lat;
		if (lat < m->st.lat_min)
			m->st.lat_min = lat;
		if (lat > m->st.lat_max)
			m->st.lat_max = lat;
	} else {
		m->st.no_frame++;
	}
	return true;
}

bool nc_match_latency_mean(const struct nc_match *m, int64_t *mean_ns)
{
	if (!m->st.bound)
		return false;
	*mean_ns = m->st.lat_sum / (int64_t)m->st.bound;
	return true;
}
