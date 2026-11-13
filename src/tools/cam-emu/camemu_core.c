#include "camemu_core.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* ---- random numbers ---- */

void ce_rng_seed(struct ce_rng *r, uint64_t seed)
{
	r->s = seed ? seed : 0x9e3779b97f4a7c15ull;
}

uint64_t ce_rand(struct ce_rng *r)
{
	r->s ^= r->s >> 12;
	r->s ^= r->s << 25;
	r->s ^= r->s >> 27;
	return r->s * 0x2545f4914f6cdd1dull;
}

double ce_uniform(struct ce_rng *r)
{
	return (double)(ce_rand(r) >> 11) / (double)(1ull << 53);
}

bool ce_chance(struct ce_rng *r, double pct)
{
	return pct > 0 && ce_uniform(r) * 100.0 < pct;
}

const char *ce_kind_str(enum ce_kind k)
{
	switch (k) {
	case CE_FRAME:    return "frame";
	case CE_SPURIOUS: return "spurious";
	case CE_LATE:     return "late";
	}
	return "?";
}

/* ---- plan ---- */

static int64_t jitter(const struct ce_faults *f, struct ce_rng *r)
{
	if (f->jitter_ns <= 0)
		return 0;
	return (int64_t)((ce_uniform(r) * 2.0 - 1.0) * (double)f->jitter_ns);
}

int ce_plan_edge(const struct ce_faults *f, struct ce_rng *r, int64_t edge_mono_ns,
		 int64_t index, int64_t period_ns, struct ce_event out[2], bool *dropped)
{
	int n = 0;

	*dropped = ce_chance(r, f->drop_pct);
	if (!*dropped) {
		bool late = ce_chance(r, f->late_pct);

		out[n++] = (struct ce_event){
			.due_ns = edge_mono_ns + f->latency_ns + jitter(f, r) +
				  (late ? period_ns * 3 / 4 : 0),
			.index = index,
			.kind = late ? CE_LATE : CE_FRAME,
		};
	}
	if (ce_chance(r, f->extra_pct)) {
		/* somewhere between this edge's frame and the next one */
		int64_t at = period_ns * 3 / 10 + (int64_t)(ce_uniform(r) * (double)period_ns * 0.4);

		out[n++] = (struct ce_event){
			.due_ns = edge_mono_ns + f->latency_ns + at,
			.index = index,
			.kind = CE_SPURIOUS,
		};
	}
	return n;
}

/* ---- schedule ---- */

void ce_sched_init(struct ce_sched *s)
{
	memset(s, 0, sizeof(*s));
}

bool ce_sched_add(struct ce_sched *s, const struct ce_event *e)
{
	if (s->n == CE_SCHED_MAX) {
		s->overflow++;
		return false;
	}
	int k = s->n;

	while (k > 0 && s->e[k - 1].due_ns > e->due_ns) {
		s->e[k] = s->e[k - 1];
		k--;
	}
	s->e[k] = *e;
	s->n++;
	return true;
}

int64_t ce_sched_next(const struct ce_sched *s)
{
	return s->n ? s->e[0].due_ns : INT64_MAX;
}

bool ce_sched_pop_due(struct ce_sched *s, int64_t now_ns, struct ce_event *out)
{
	if (!s->n || s->e[0].due_ns > now_ns)
		return false;
	*out = s->e[0];
	memmove(&s->e[0], &s->e[1], (size_t)(s->n - 1) * sizeof(s->e[0]));
	s->n--;
	return true;
}

/* ---- buffer queue ---- */

void ce_bufq_init(struct ce_bufq *q, int count)
{
	memset(q, 0, sizeof(*q));
	q->count = count;
}

int ce_bufq_queue(struct ce_bufq *q, int slot)
{
	if (slot < 0 || slot >= q->count)
		return -EINVAL;
	if (q->queued[slot])
		return -EBUSY;
	q->fifo[(q->head + q->n) % 32] = slot;
	q->n++;
	q->queued[slot] = true;
	return 0;
}

int ce_bufq_take(struct ce_bufq *q)
{
	if (!q->n)
		return -1;
	int slot = q->fifo[q->head];

	q->head = (q->head + 1) % 32;
	q->n--;
	q->queued[slot] = false;
	return slot;
}

void ce_bufq_clear(struct ce_bufq *q)
{
	ce_bufq_init(q, q->count);
}

/* ---- stream ---- */

void ce_stream_on(struct ce_stream *st)
{
	st->on = true;
	st->sequence = 0;
}

int ce_deliver(struct ce_stream *st, struct ce_bufq *q, const struct ce_faults *f,
	       struct ce_rng *r, uint32_t *seq)
{
	if (ce_chance(r, f->seqjump_pct)) {
		st->sequence += 1 + (uint32_t)(ce_rand(r) % 3);
		st->seq_jumps++;
	}
	*seq = st->sequence++;

	int slot = ce_bufq_take(q);

	if (slot < 0)
		st->lost_nobuf++;
	else
		st->delivered++;
	return slot;
}

/* ---- pattern ---- */

int ce_pattern_init(struct ce_pattern *p, int w, int h, int amp, struct ce_rng *r)
{
	memset(p, 0, sizeof(*p));
	if (w < 16 || h < 16 || (w & 1) || (h & 1) || amp < 0 || amp > 100)
		return -EINVAL;
	p->w = w;
	p->h = h;
	p->square = w / 12 > 8 ? (w / 12) & ~1 : 8;
	p->noise_len = (size_t)w * h + CE_NOISE_SLACK;
	p->noise = malloc(p->noise_len);
	for (int k = 0; k < 2; k++)
		p->row[k] = malloc((size_t)w + 2 * p->square);
	if (!p->noise || !p->row[0] || !p->row[1]) {
		ce_pattern_free(p);
		return -ENOMEM;
	}
	for (size_t i = 0; i < p->noise_len; i++)
		p->noise[i] = amp ? (int8_t)((int)(ce_rand(r) % (2u * amp + 1)) - amp) : 0;
	for (int k = 0; k < 2; k++)
		for (int x = 0; x < w + 2 * p->square; x++)
			p->row[k][x] = ((x / p->square + k) & 1) ? 190 : 60;
	return 0;
}

void ce_pattern_free(struct ce_pattern *p)
{
	free(p->noise);
	free(p->row[0]);
	free(p->row[1]);
	p->noise = NULL;
	p->row[0] = p->row[1] = NULL;
}

void ce_pattern_fill(struct ce_pattern *p, uint8_t *nv12, struct ce_rng *r)
{
	int period = 2 * p->square;
	int shift = (int)((p->frame * 4) % (uint64_t)period);
	const int8_t *nz = p->noise + ce_rand(r) % CE_NOISE_SLACK;

	for (int y = 0; y < p->h; y++) {
		/* rows drift down by 1 px per frame too */
		const uint8_t *src = p->row[((y + (int)p->frame) / p->square) & 1] + (period - shift);
		uint8_t *dst = nv12 + (size_t)y * p->w;
		const int8_t *n = nz + (size_t)y * p->w;

		for (int x = 0; x < p->w; x++) {
			int v = src[x] + n[x];

			dst[x] = (uint8_t)(v < 16 ? 16 : v > 235 ? 235 : v);
		}
	}
	/* chroma: slow colour bands moving down */
	uint8_t *uv = nv12 + (size_t)p->w * p->h;

	for (int y = 0; y < p->h / 2; y++) {
		int band = ((y + (int)(p->frame / 2)) / 32) % 4;
		uint8_t u = (uint8_t)(96 + 24 * band), v = (uint8_t)(160 - 24 * band);
		uint8_t *row = uv + (size_t)y * p->w;

		for (int x = 0; x < p->w; x += 2) {
			row[x] = u;
			row[x + 1] = v;
		}
	}
	p->frame++;
}
