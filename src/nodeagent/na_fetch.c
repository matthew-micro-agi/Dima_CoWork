#include "na_fetch.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void add_gap(struct na_fetch_plan *p, int64_t from, int64_t to)
{
	if (from > to)
		return;
	if (p->ngaps == NA_FETCH_MAX_GAPS) {
		p->gaps_truncated = true;
		return;
	}
	p->gaps[p->ngaps++] = (struct na_range){ from, to };
}

static int cmp_seg(const void *a, const void *b)
{
	const struct rec_seg *x = *(const struct rec_seg *const *)a;
	const struct rec_seg *y = *(const struct rec_seg *const *)b;

	return (x->i.first_idx > y->i.first_idx) - (x->i.first_idx < y->i.first_idx);
}

/* Byte offset of the first keyframe with index >= from in segment s; false if none. */
static bool key_at_or_after(const struct rec_seg *s, int64_t from, uint64_t *off,
			    int64_t *idx)
{
	for (unsigned k = 0; k < s->i.n_keys; k++) {
		int64_t ki = s->i.first_idx + s->i.key_didx[k];

		if (ki >= from) {
			*off = s->i.key_off[k];
			*idx = ki;
			return true;
		}
	}
	return false;
}

int na_fetch_plan(const struct recorder *idx, uint32_t session, int64_t from_idx,
		  int64_t to_idx, int64_t last_s, int64_t max_s, struct na_fetch_plan *p)
{
	const struct rec_sess *ss = rec_find_session(idx, session);
	size_t n = 0;

	memset(p, 0, sizeof(*p));
	p->session = session;
	if (!ss || ss->period_ns <= 0)
		return -ENOENT;

	/* Available segments of the session, by index. */
	const struct rec_seg **v = malloc(idx->nsegs * sizeof(*v) + 1);

	if (!v)
		return -ENOMEM;
	for (size_t i = 0; i < idx->nsegs; i++)
		if (idx->segs[i].session == session && !idx->segs[i].deleted)
			v[n++] = &idx->segs[i];
	qsort(v, n, sizeof(*v), cmp_seg);
	if (!n) {
		free(v);
		return -ENOENT;
	}

	int64_t avail_last = v[n - 1]->i.last_idx;
	int64_t fps = (1000000000LL + ss->period_ns / 2) / ss->period_ns;

	if (last_s > 0) {
		to_idx = avail_last;
		from_idx = avail_last - last_s * fps + 1;
		if (from_idx < 0)
			from_idx = 0;
	}
	if (to_idx < 0)
		to_idx = avail_last;
	if (from_idx < 0 || to_idx < from_idx) {
		free(v);
		return -EINVAL;
	}
	if ((to_idx - from_idx + 1) > max_s * fps) {
		free(v);
		return -E2BIG;
	}

	int64_t covered = from_idx - 1;  /* last index accounted for */

	for (size_t i = 0; i < n && p->nparts < NA_FETCH_MAX_PARTS; i++) {
		const struct rec_seg *s = v[i];
		uint64_t off = 0;
		int64_t start = s->i.first_idx;

		if (s->i.last_idx < from_idx)
			continue;
		if (s->i.first_idx > to_idx)
			break;
		if (p->nparts == 0 && s->i.first_idx < from_idx) {
			/* Start inside: first keyframe at or after the requested start. */
			if (!key_at_or_after(s, from_idx, &off, &start)) {
				if (!(s->i.flags & REC_SEG_F_KEYS_FULL))
					continue;  /* no keyframe left here: next segment */
				/* more keyframes than recorded: last known one, earlier than asked */
				off = s->i.key_off[s->i.n_keys - 1];
				start = s->i.first_idx + s->i.key_didx[s->i.n_keys - 1];
			}
		}
		if (p->nparts == 0)
			p->from = start;
		add_gap(p, covered + 1, (start < from_idx ? from_idx : start) - 1);
		p->parts[p->nparts++] = (struct na_fetch_part){ s->i.first_idx, off,
								s->i.bytes - off };
		p->bytes += s->i.bytes - off;
		p->missing_inside += s->i.missing;
		p->to = s->i.last_idx;
		covered = s->i.last_idx;
	}
	free(v);
	if (!p->nparts)
		return -ENOENT;

	/* Requested beyond what is available: missing, or not yet closed. */
	if (to_idx > covered) {
		if (ss->status == REC_ST_RECORDING)
			p->open_segment_pending = true;
		add_gap(p, covered + 1, to_idx);
	}
	return 0;
}

void na_ranges_str(const struct na_fetch_plan *p, char *buf, size_t len)
{
	size_t o = 0;

	buf[0] = 0;
	if (!p->ngaps) {
		snprintf(buf, len, "none");
		return;
	}
	for (size_t i = 0; i < p->ngaps && o < len; i++)
		o += (size_t)snprintf(buf + o, len - o, "%s%" PRId64 "-%" PRId64, i ? "," : "",
				      p->gaps[i].from, p->gaps[i].to);
	if (p->gaps_truncated && o < len)
		snprintf(buf + o, len - o, ",...");
}
