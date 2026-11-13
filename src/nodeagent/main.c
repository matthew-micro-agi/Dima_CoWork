/*
 * nodeagent: HTTP + CBOR control interface of the node (icd/fwt-icd-v1.cddl).
 * See agent.h. GET never changes state; POST bodies are CBOR decoded by the
 * code generated from the ICD (src/nodeagent/gen); fetch returns raw H.265.
 */
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/un.h>
#include <unistd.h>

#include <microhttpd.h>

#include "agent.h"
#include "fwt_version.h"
#include "icd_decode.h"
#include "icd_encode.h"
#include "na_fetch.h"
#include "rec_ctl.h"

#define CBOR_TYPE "application/cbor"

static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
	(void)sig;
	g_stop = 1;
}

static struct zcbor_string zs(const char *s)
{
	return (struct zcbor_string){ (const uint8_t *)s, strlen(s) };
}

/* ---- index -------------------------------------------------------------- */

void na_index_refresh(struct agent *a)
{
	char path[512];
	struct stat st;

	snprintf(path, sizeof(path), "%s/%s", a->o.rec_dir, REC_MANIFEST_NAME);
	if (stat(path, &st) != 0) {  /* nothing recorded yet */
		if (a->idx_loaded)
			rec_close(&a->idx);
		a->idx_loaded = false;
		return;
	}
	if (a->idx_loaded && st.st_size == a->idx_size)
		return;
	if (a->idx_loaded)
		rec_close(&a->idx);
	a->idx_loaded = rec_open_readonly(&a->idx, a->o.rec_dir) == 0;
	a->idx_size = st.st_size;
}

/* ---- responses ---------------------------------------------------------- */

static enum MHD_Result send_buf(struct MHD_Connection *c, unsigned status, const void *buf,
				size_t len, const char *type)
{
	struct MHD_Response *r = MHD_create_response_from_buffer_copy(len, buf);

	if (!r)
		return MHD_NO;
	MHD_add_response_header(r, MHD_HTTP_HEADER_CONTENT_TYPE, type);

	enum MHD_Result ret = MHD_queue_response(c, status, r);

	MHD_destroy_response(r);
	return ret;
}

static enum MHD_Result send_error(struct MHD_Connection *c, unsigned status, const char *code,
				  const char *msg)
{
	uint8_t buf[512];
	size_t len = 0;
	struct error e = { zs(code), zs(msg) };

	if (cbor_encode_error(buf, sizeof(buf), &e, &len) != ZCBOR_SUCCESS)
		return send_buf(c, 500, "", 0, CBOR_TYPE);
	return send_buf(c, status, buf, len, CBOR_TYPE);
}

/* Encode with a generated encoder into a heap buffer and send it. */
#define SEND_CBOR(c, fn, input, cap)                                                   \
	({                                                                             \
		size_t n_ = 0;                                                         \
		uint8_t *b_ = malloc(cap);                                             \
		enum MHD_Result r_;                                                    \
		if (b_ && fn(b_, (cap), (input), &n_) == ZCBOR_SUCCESS)                \
			r_ = send_buf((c), 200, b_, n_, CBOR_TYPE);                    \
		else                                                                   \
			r_ = send_error((c), 500, "encode", "response encoding failed"); \
		free(b_);                                                              \
		r_;                                                                    \
	})

/* Strict number from the query string. */
static bool qnum(struct MHD_Connection *c, const char *key, long long *v, bool *given)
{
	const char *s = MHD_lookup_connection_value(c, MHD_GET_ARGUMENT_KIND, key);
	char *end;

	*given = s != NULL;
	if (!s)
		return true;
	errno = 0;
	*v = strtoll(s, &end, 10);
	return *s && !*end && errno == 0;
}

/* ---- GET handlers --------------------------------------------------------- */

static enum MHD_Result h_identity(struct agent *a, struct MHD_Connection *c)
{
	char host[64] = "node";
	struct identity id;

	(void)a;
	gethostname(host, sizeof(host) - 1);
	id = (struct identity){ zs(host), zs(FWT_VERSION), 1, 60, 1920, 1200 };
	return SEND_CBOR(c, cbor_encode_identity, &id, 512);
}

static long long read_sysfs_ll(const char *path, int base)
{
	char buf[64];
	FILE *f = fopen(path, "r");
	long long v = 0;

	if (f && fgets(buf, sizeof(buf), f))
		v = strtoll(buf, NULL, base);
	if (f)
		fclose(f);
	return v;
}

static enum MHD_Result h_health(struct agent *a, struct MHD_Connection *c)
{
	struct health h = { 0 };
	char path[256], life[64] = "";
	const char *state = a->state == NA_RECORDING ? "recording" :
			    a->state == NA_FAILED ? "failed" : "idle";
	int64_t now = rec_now_wall_ns();
	uint64_t frames = 0, missing = 0, bytes = 0, ns = 0;

	na_index_refresh(a);
	if (a->idx_loaded) {
		h.health_ring_used = rec_used_bytes(&a->idx);
		h.health_ring_locked = rec_locked_bytes(&a->idx, now);
		uint32_t s = a->session;

		if (!s && a->idx.nsess)
			s = a->idx.sess[a->idx.nsess - 1].session;
		h.health_session = s;
		for (size_t i = 0; i < a->idx.nsegs; i++) {
			const struct rec_seg *g = &a->idx.segs[i];
			const struct rec_sess *ss = rec_find_session(&a->idx, g->session);

			if (g->session == s) {
				frames += g->i.frames;
				missing += g->i.missing;
			}
			if (!g->deleted && ss) {  /* bytes per second, all available data */
				bytes += g->i.bytes;
				ns += (uint64_t)g->i.frames * (uint64_t)ss->period_ns;
			}
		}
	} else {
		h.health_session = a->session;
	}

	struct statvfs v;
	uint64_t fs_free = 0;

	if (statvfs(a->o.rec_dir, &v) == 0)
		fs_free = (uint64_t)v.f_bavail * v.f_frsize;
	fs_free = fs_free > NA_RESERVE_BYTES ? fs_free - NA_RESERVE_BYTES : 0;
	if (a->o.ring_max_mb) {
		uint64_t ring = a->o.ring_max_mb << 20;
		uint64_t rf = ring > h.health_ring_used ? ring - h.health_ring_used : 0;

		if (rf < fs_free)
			fs_free = rf;
	}
	h.health_ring_free = fs_free;

	double bps = ns ? bytes / (ns / 1e9) : a->o.bitrate / 8.0;

	h.health_depth_s = bps > 0 ? (uint64_t)((h.health_ring_used + h.health_ring_free) / bps) : 0;

	snprintf(path, sizeof(path), "/sys/block/%s/device/life_time", a->o.emmc);
	FILE *f = fopen(path, "r");

	if (f && fgets(life, sizeof(life), f)) {
		char *e;

		h.health_emmc_life_a = strtoull(life, &e, 16);
		h.health_emmc_life_b = strtoull(e, NULL, 16);
	}
	if (f)
		fclose(f);
	snprintf(path, sizeof(path), "/sys/block/%s/device/pre_eol_info", a->o.emmc);
	h.health_emmc_pre_eol = (uint64_t)read_sysfs_ll(path, 16);
	h.health_temp_mc = read_sysfs_ll("/sys/class/thermal/thermal_zone0/temp", 10);
	h.health_frames = frames;
	h.health_missing = missing;
	h.health_phc_ns = na_ptp_now(a);
	h.health_time_verified = false;  /* DL-13: PTP lock not checked yet */
	h.health_state = zs(state);
	h.health_message = zs(a->message);
	return SEND_CBOR(c, cbor_encode_health, &h, 1024);
}

static enum MHD_Result h_sessions(struct agent *a, struct MHD_Connection *c)
{
	long long after = 0;
	bool given;

	if (!qnum(c, "after", &after, &given))
		return send_error(c, 400, "bad-request", "after must be a number");
	na_index_refresh(a);

	struct sessions_rep *r = calloc(1, sizeof(*r));
	size_t k = 0;

	if (!r)
		return send_error(c, 500, "no-memory", "");
	if (a->idx_loaded && given) {  /* start after the given session (manifest order) */
		for (; k < a->idx.nsess && a->idx.sess[k].session != (uint32_t)after; k++)
			;
		k = k < a->idx.nsess ? k + 1 : a->idx.nsess;
	}
	for (; a->idx_loaded && k < a->idx.nsess; k++) {
		if (r->sessions_rep_sessions_session_info_m_count == 512) {
			r->sessions_rep_more = true;
			break;
		}
		const struct rec_sess *s = &a->idx.sess[k];
		struct session_info *o =
			&r->sessions_rep_sessions_session_info_m[r->sessions_rep_sessions_session_info_m_count++];
		int64_t lo = -1, hi = -1;
		uint64_t segs = 0, bytes = 0, frames = 0, missing = 0;

		for (size_t i = 0; i < a->idx.nsegs; i++) {
			const struct rec_seg *g = &a->idx.segs[i];

			if (g->session != s->session)
				continue;
			frames += g->i.frames;
			missing += g->i.missing;
			if (g->deleted)
				continue;
			segs++;
			bytes += g->i.bytes;
			if (lo < 0 || g->i.first_idx < lo)
				lo = g->i.first_idx;
			if (g->i.last_idx > hi)
				hi = g->i.last_idx;
		}
		*o = (struct session_info){
			.session_info_session = s->session,
			.session_info_status = zs(rec_status_str(s->status)),
			.session_info_start_wall = s->wall_ns,
			.session_info_t0 = s->t0_ns,
			.session_info_period = (uint64_t)s->period_ns,
			.session_info_first_idx = lo,
			.session_info_last_idx = hi,
			.session_info_frames = frames,
			.session_info_missing = missing,
			.session_info_segments = segs,
			.session_info_bytes = bytes,
			.session_info_truncated = s->deleted_segments > 0,
		};
	}

	enum MHD_Result ret = SEND_CBOR(c, cbor_encode_sessions_rep, r, 512 * 200 + 64);

	free(r);
	return ret;
}

static enum MHD_Result h_segments(struct agent *a, struct MHD_Connection *c)
{
	long long session = 0, from = 0;
	bool sg, fg;

	if (!qnum(c, "session", &session, &sg) || !sg || session <= 0 || session > UINT32_MAX ||
	    !qnum(c, "from_idx", &from, &fg))
		return send_error(c, 400, "bad-request", "need session=<n> [&from_idx=<n>]");
	na_index_refresh(a);
	if (!a->idx_loaded || !rec_find_session(&a->idx, (uint32_t)session))
		return send_error(c, 404, "no-session", "unknown session");

	struct segments_rep *r = calloc(1, sizeof(*r));
	int64_t now = rec_now_wall_ns();

	if (!r)
		return send_error(c, 500, "no-memory", "");
	r->segments_rep_session = (uint64_t)session;
	for (size_t i = 0; i < a->idx.nsegs; i++) {
		const struct rec_seg *g = &a->idx.segs[i];

		if (g->session != (uint32_t)session || g->deleted || g->i.first_idx < from)
			continue;
		if (r->segments_rep_segments_segment_info_m_count == 512) {
			r->segments_rep_more = true;
			break;
		}
		r->segments_rep_segments_segment_info_m[r->segments_rep_segments_segment_info_m_count++] =
			(struct segment_info){
				.segment_info_first_idx = g->i.first_idx,
				.segment_info_last_idx = g->i.last_idx,
				.segment_info_t_first = g->i.t_first,
				.segment_info_t_last = g->i.t_last,
				.segment_info_bytes = g->i.bytes,
				.segment_info_frames = g->i.frames,
				.segment_info_missing = g->i.missing,
				.segment_info_keys = g->i.n_keys,
				.segment_info_locked = rec_seg_locked(&a->idx, g, now),
				.segment_info_recovered = (g->i.flags & REC_SEG_F_RECOVERED) != 0,
			};
	}

	enum MHD_Result ret = SEND_CBOR(c, cbor_encode_segments_rep, r, 512 * 140 + 64);

	free(r);
	return ret;
}

/* ---- fetch: raw H.265, streamed from the segment files -------------------- */

struct fetch {
	struct agent *a;
	size_t n, cur;
	int fd[NA_FETCH_MAX_PARTS];
	uint64_t left[NA_FETCH_MAX_PARTS];
};

static ssize_t fetch_read(void *cls, uint64_t pos, char *buf, size_t max)
{
	struct fetch *f = cls;

	(void)pos;
	while (f->cur < f->n) {
		if (f->left[f->cur] == 0) {
			f->cur++;
			continue;
		}
		size_t want = f->left[f->cur] < max ? (size_t)f->left[f->cur] : max;
		ssize_t k = read(f->fd[f->cur], buf, want);

		if (k <= 0)
			return MHD_CONTENT_READER_END_WITH_ERROR;
		f->left[f->cur] -= (uint64_t)k;
		return k;
	}
	return MHD_CONTENT_READER_END_OF_STREAM;
}

static void fetch_free(void *cls)
{
	struct fetch *f = cls;

	for (size_t i = 0; i < f->n; i++)
		if (f->fd[i] >= 0)
			close(f->fd[i]);
	pthread_mutex_lock(&f->a->lock);
	f->a->fetches--;
	pthread_mutex_unlock(&f->a->lock);
	free(f);
}

static enum MHD_Result h_fetch(struct agent *a, struct MHD_Connection *c)
{
	long long session = 0, from = 0, to = -1, last_s = 0;
	bool sg, fg, tg, lg;

	if (!qnum(c, "session", &session, &sg) || !sg || session <= 0 || session > UINT32_MAX ||
	    !qnum(c, "from_idx", &from, &fg) || !qnum(c, "to_idx", &to, &tg) ||
	    !qnum(c, "last_s", &last_s, &lg) || (!lg && !fg) || (lg && last_s <= 0))
		return send_error(c, 400, "bad-request",
				  "need session=<n> and from_idx=<n>[&to_idx=<n>] or last_s=<n>");
	if (!tg)
		to = -1;
	if (a->fetches >= NA_MAX_FETCHES)
		return send_error(c, 503, "busy", "too many fetches in progress");

	struct na_fetch_plan *p = malloc(sizeof(*p));

	if (!p)
		return send_error(c, 500, "no-memory", "");
	na_index_refresh(a);

	int e = a->idx_loaded ? na_fetch_plan(&a->idx, (uint32_t)session, from, to,
					      lg ? last_s : 0, NA_MAX_FETCH_S, p)
			      : -ENOENT;

	if (e) {
		free(p);
		return e == -E2BIG ? send_error(c, 400, "range-too-long", "at most 600 s per fetch")
		     : e == -EINVAL ? send_error(c, 400, "bad-request", "need 0 <= from <= to")
				    : send_error(c, 404, "no-data", "nothing recorded in that range");
	}

	/* Open every file now: the ring may delete one later, an open file stays readable. */
	struct fetch *f = calloc(1, sizeof(*f));

	if (!f) {
		free(p);
		return send_error(c, 500, "no-memory", "");
	}
	f->a = a;
	for (size_t i = 0; i < p->nparts; i++) {
		char path[512];

		rec_seg_path(&a->idx, p->session, p->parts[i].first_idx, path, sizeof(path));
		f->fd[i] = open(path, O_RDONLY | O_CLOEXEC);
		f->n++;
		if (f->fd[i] < 0 || lseek(f->fd[i], (off_t)p->parts[i].offset, SEEK_SET) < 0) {
			for (size_t k = 0; k <= i; k++)
				if (f->fd[k] >= 0)
					close(f->fd[k]);
			free(f);
			free(p);
			return send_error(c, 410, "gone", "segment deleted before the fetch started; "
					  "list segments again");
		}
		f->left[i] = p->parts[i].len;
	}
	struct MHD_Response *r = MHD_create_response_from_callback(p->bytes, 64 * 1024,
								   fetch_read, f, fetch_free);
	char v[64], gaps[600];

	if (!r) {  /* not fetch_free(): it takes a->lock, which we hold */
		for (size_t i = 0; i < f->n; i++)
			close(f->fd[i]);
		free(f);
		free(p);
		return MHD_NO;
	}
	a->fetches++;  /* fetch_free() decrements when the response is done */
	MHD_add_response_header(r, MHD_HTTP_HEADER_CONTENT_TYPE, "video/H265");
	snprintf(v, sizeof(v), "%" PRIu32, p->session);
	MHD_add_response_header(r, "X-Fwt-Session", v);
	snprintf(v, sizeof(v), "%" PRId64, p->from);
	MHD_add_response_header(r, "X-Fwt-From", v);
	snprintf(v, sizeof(v), "%" PRId64, p->to);
	MHD_add_response_header(r, "X-Fwt-To", v);
	na_ranges_str(p, gaps, sizeof(gaps));
	MHD_add_response_header(r, "X-Fwt-Missing", gaps);
	snprintf(v, sizeof(v), "%" PRIu64, p->missing_inside);
	MHD_add_response_header(r, "X-Fwt-Missing-Inside", v);
	MHD_add_response_header(r, "X-Fwt-Open-Segment",
				p->open_segment_pending ? "not-included" : "none");
	free(p);

	enum MHD_Result ret = MHD_queue_response(c, 200, r);

	MHD_destroy_response(r);
	return ret;
}

/* ---- POST handlers ---------------------------------------------------------- */

static enum MHD_Result h_arm(struct agent *a, struct MHD_Connection *c, const uint8_t *body,
			     size_t len)
{
	struct arm_req q;
	size_t used = 0;
	const char *code = "";
	char msg[160];

	if (cbor_decode_arm_req(body, len, &q, &used) != ZCBOR_SUCCESS || used != len ||
	    q.arm_req_session > UINT32_MAX)
		return send_error(c, 400, "bad-request", "body must be arm-req (CBOR, see ICD)");

	int st = na_arm(a, (uint32_t)q.arm_req_session, (unsigned)q.arm_req_fps,
			q.arm_req_t0_present, q.arm_req_t0.arm_req_t0, &code, msg, sizeof(msg));

	if (st)
		return send_error(c, (unsigned)st, code, msg);

	struct arm_rep r = { a->session, a->t0_ns, (uint64_t)a->period_ns };

	return SEND_CBOR(c, cbor_encode_arm_rep, &r, 128);
}

static enum MHD_Result h_disarm(struct agent *a, struct MHD_Connection *c)
{
	const char *code = "";
	char msg[160];
	uint32_t s = a->session;
	int st = na_disarm(a, &code, msg, sizeof(msg));

	if (st)
		return send_error(c, (unsigned)st, code, msg);

	struct session_rep r = { s };

	return SEND_CBOR(c, cbor_encode_session_rep, &r, 64);
}

/* Lock/unlock: to the running recorder over its socket, else open /rec directly. */
static int ctl_request(struct agent *a, struct rec_ctl_msg *req, struct rec_ctl_msg *rep)
{
	struct sockaddr_un to = { .sun_family = AF_UNIX }, self = { .sun_family = AF_UNIX };
	int fd;

	if (strlen(a->o.ctl_sock) >= sizeof(to.sun_path))
		return 1;
	strcpy(to.sun_path, a->o.ctl_sock);
	fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0 || bind(fd, (struct sockaddr *)&self, sizeof(sa_family_t)) != 0 ||
	    sendto(fd, req, sizeof(*req), 0, (struct sockaddr *)&to, sizeof(to)) < 0) {
		if (fd >= 0)
			close(fd);
		return 1;  /* no recorder listening */
	}

	struct pollfd p = { .fd = fd, .events = POLLIN };
	ssize_t n = -1;

	if (poll(&p, 1, 3000) == 1)
		n = recv(fd, rep, sizeof(*rep), 0);
	close(fd);
	return n >= 0 && rec_ctl_valid(rep, n) ? 0 : -1;
}

static int lock_offline(struct agent *a, const struct rec_ctl_msg *q, struct rec_ctl_msg *rep)
{
	struct recorder r;
	struct rec_config cfg = { .dir = a->o.rec_dir, .log = stderr };
	int e = rec_open(&r, &cfg);

	rec_ctl_init(rep, REC_CTL_REPLY);
	if (e) {
		rep->result = e;
		snprintf(rep->text, sizeof(rep->text), "recorder busy or /rec unavailable");
		return 0;
	}
	if (q->type == REC_CTL_UNLOCK) {
		rep->result = rec_unlock(&r, q->lock_id);
		rep->lock_id = q->lock_id;
		snprintf(rep->text, sizeof(rep->text), "%s", rep->result ? "no such lock" : "unlocked");
	} else {
		rep->result = rec_lock(&r, q->session, q->from_idx, q->to_idx, q->ttl_s, q->text,
				       &rep->lock_id, rep->text, sizeof(rep->text));
	}
	rec_close(&r);
	return 0;
}

static enum MHD_Result lock_reply(struct agent *a, struct MHD_Connection *c,
				  struct rec_ctl_msg *q)
{
	struct rec_ctl_msg rep;
	int via = a->state == NA_RECORDING ? ctl_request(a, q, &rep) : 1;

	if (via < 0)
		return send_error(c, 504, "recorder-timeout", "no reply from the recorder");
	if (via == 1)
		lock_offline(a, q, &rep);
	rep.text[sizeof(rep.text) - 1] = 0;
	if (rep.result == -EDQUOT)
		return send_error(c, 409, "lock-cap", rep.text);
	if (rep.result == -ENOENT)
		return send_error(c, 404, q->type == REC_CTL_LOCK ? "no-session" : "no-lock",
				  rep.text);
	if (rep.result == -EBUSY)
		return send_error(c, 503, "busy", rep.text);
	if (rep.result)
		return send_error(c, 400, "bad-request", rep.text);
	if (q->type == REC_CTL_UNLOCK)
		return SEND_CBOR(c, cbor_encode_ok_rep, NULL, 16);

	struct lock_rep r = { rep.lock_id };

	return SEND_CBOR(c, cbor_encode_lock_rep, &r, 64);
}

static enum MHD_Result h_lock(struct agent *a, struct MHD_Connection *c, const uint8_t *body,
			      size_t len)
{
	struct lock_req q;
	struct rec_ctl_msg m;
	size_t used = 0;

	if (cbor_decode_lock_req(body, len, &q, &used) != ZCBOR_SUCCESS || used != len ||
	    q.lock_req_session == 0 || q.lock_req_session > UINT32_MAX ||
	    q.lock_req_ttl_s == 0 || q.lock_req_ttl_s > 30ull * 24 * 3600)
		return send_error(c, 400, "bad-request", "body must be lock-req (CBOR, see ICD); "
				  "ttl 1 s .. 30 days");
	rec_ctl_init(&m, REC_CTL_LOCK);
	m.session = (uint32_t)q.lock_req_session;
	m.from_idx = q.lock_req_from_idx;
	m.to_idx = q.lock_req_to_idx;
	m.ttl_s = (int64_t)q.lock_req_ttl_s;
	if (q.lock_req_reason_present) {
		size_t n = q.lock_req_reason.lock_req_reason.len;

		if (n >= sizeof(m.text))
			n = sizeof(m.text) - 1;
		memcpy(m.text, q.lock_req_reason.lock_req_reason.value, n);
	}
	return lock_reply(a, c, &m);
}

static enum MHD_Result h_unlock(struct agent *a, struct MHD_Connection *c, const uint8_t *body,
				size_t len)
{
	struct unlock_req q;
	struct rec_ctl_msg m;
	size_t used = 0;

	if (cbor_decode_unlock_req(body, len, &q, &used) != ZCBOR_SUCCESS || used != len)
		return send_error(c, 400, "bad-request", "body must be unlock-req (CBOR, see ICD)");
	rec_ctl_init(&m, REC_CTL_UNLOCK);
	m.lock_id = q.unlock_req_lock_id;
	return lock_reply(a, c, &m);
}

/* ---- request routing ------------------------------------------------------------ */

struct req {
	uint8_t body[NA_MAX_BODY];
	size_t len;
	bool too_big;
};

static enum MHD_Result handler(void *cls, struct MHD_Connection *c, const char *url,
			       const char *method, const char *version, const char *upload,
			       size_t *upload_size, void **ctx)
{
	struct agent *a = cls;
	bool post = !strcmp(method, "POST"), get = !strcmp(method, "GET");

	(void)version;
	if (post) {  /* collect the body (limited), answer on the last call */
		struct req *q = *ctx;

		if (!q) {
			*ctx = q = calloc(1, sizeof(*q));
			return q ? MHD_YES : MHD_NO;
		}
		if (*upload_size) {
			if (q->len + *upload_size > sizeof(q->body))
				q->too_big = true;
			else
				memcpy(q->body + q->len, upload, *upload_size);
			q->len += *upload_size;
			*upload_size = 0;
			return MHD_YES;
		}
		if (q->too_big)
			return send_error(c, 413, "too-large", "request body over 4 KB");
	}

	enum MHD_Result ret;
	struct req *q = *ctx;

	pthread_mutex_lock(&a->lock);
	na_poll_children(a);
	if (get && !strcmp(url, "/v1/identity"))
		ret = h_identity(a, c);
	else if (get && !strcmp(url, "/v1/health"))
		ret = h_health(a, c);
	else if (get && !strcmp(url, "/v1/sessions"))
		ret = h_sessions(a, c);
	else if (get && !strcmp(url, "/v1/segments"))
		ret = h_segments(a, c);
	else if (get && !strcmp(url, "/v1/fetch"))
		ret = h_fetch(a, c);
	else if (post && !strcmp(url, "/v1/arm"))
		ret = h_arm(a, c, q->body, q->len);
	else if (post && !strcmp(url, "/v1/disarm"))
		ret = h_disarm(a, c);
	else if (post && !strcmp(url, "/v1/lock"))
		ret = h_lock(a, c, q->body, q->len);
	else if (post && !strcmp(url, "/v1/unlock"))
		ret = h_unlock(a, c, q->body, q->len);
	else if (!get && !post)
		ret = send_error(c, 405, "bad-method", "GET or POST only");
	else
		ret = send_error(c, 404, "not-found", "unknown path (see the ICD)");
	pthread_mutex_unlock(&a->lock);
	return ret;
}

static void completed(void *cls, struct MHD_Connection *c, void **ctx,
		      enum MHD_RequestTerminationCode why)
{
	(void)cls;
	(void)c;
	(void)why;
	free(*ctx);
	*ctx = NULL;
}

/* ---- options, main ----------------------------------------------------------------- */

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [options]\n"
		"  --port N            HTTP port (default 8080)\n"
		"  --rec DIR           recordings (default /rec)\n"
		"  --trigd PATH        trigd binary (default /tmp/trigd)\n"
		"  --nodecap PATH      nodecap binary (default /tmp/nodecap)\n"
		"  --trigd-args STR    extra trigd options (e.g. \"--clock sys\" on a laptop)\n"
		"  --nodecap-args STR  extra nodecap options (default \"--enc v4l2\")\n"
		"  --ring-max-mb N     ring limit passed to nodecap (default: filesystem)\n"
		"  --ptp DEV           PTP clock for health and default T0 (default /dev/ptp0;\n"
		"                      \"none\" = system clock)\n"
		"  --emmc NAME         block device for wear info (default mmcblk0)\n"
		"  --log-dir DIR       children's logs (default /tmp)\n"
		"  --trigd-sock PATH   (default %s)\n"
		"  --ctl-sock PATH     recorder control socket (default %s)\n",
		prog, "/run/fwtrial/trigd.sock", REC_CTL_SOCK_DEFAULT);
}

int main(int argc, char **argv)
{
	static const struct option lo[] = {
		{ "port", required_argument, NULL, 'p' },
		{ "rec", required_argument, NULL, 'r' },
		{ "trigd", required_argument, NULL, 't' },
		{ "nodecap", required_argument, NULL, 'n' },
		{ "trigd-args", required_argument, NULL, 'T' },
		{ "nodecap-args", required_argument, NULL, 'N' },
		{ "ring-max-mb", required_argument, NULL, 'M' },
		{ "ptp", required_argument, NULL, 'P' },
		{ "emmc", required_argument, NULL, 'e' },
		{ "log-dir", required_argument, NULL, 'l' },
		{ "trigd-sock", required_argument, NULL, 's' },
		{ "ctl-sock", required_argument, NULL, 'c' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	static struct agent a;
	int c;

	a.o = (struct na_opts){ .port = 8080, .rec_dir = "/rec", .trigd_bin = "/tmp/trigd",
				.nodecap_bin = "/tmp/nodecap", .trigd_args = "",
				.nodecap_args = "--enc v4l2", .ptp_dev = "/dev/ptp0",
				.emmc = "mmcblk0", .log_dir = "/tmp",
				.trigd_sock = "/run/fwtrial/trigd.sock",
				.ctl_sock = REC_CTL_SOCK_DEFAULT, .bitrate = 5000000 };
	while ((c = getopt_long(argc, argv, "h", lo, NULL)) != -1) {
		switch (c) {
		case 'p': a.o.port = (unsigned)atoi(optarg); break;
		case 'r': a.o.rec_dir = optarg; break;
		case 't': a.o.trigd_bin = optarg; break;
		case 'n': a.o.nodecap_bin = optarg; break;
		case 'T': a.o.trigd_args = optarg; break;
		case 'N': a.o.nodecap_args = optarg; break;
		case 'M': a.o.ring_max_mb = strtoull(optarg, NULL, 0); break;
		case 'P': a.o.ptp_dev = strcmp(optarg, "none") ? optarg : NULL; break;
		case 'e': a.o.emmc = optarg; break;
		case 'l': a.o.log_dir = optarg; break;
		case 's': a.o.trigd_sock = optarg; break;
		case 'c': a.o.ctl_sock = optarg; break;
		default: usage(argv[0]); return 2;
		}
	}
	pthread_mutex_init(&a.lock, NULL);

	struct sigaction sa = { .sa_handler = on_signal };

	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);

	struct MHD_Daemon *d = MHD_start_daemon(
		MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_THREAD_PER_CONNECTION |
			MHD_USE_ERROR_LOG,
		(uint16_t)a.o.port, NULL, NULL, handler, &a,
		MHD_OPTION_CONNECTION_LIMIT, (unsigned)16,
		MHD_OPTION_CONNECTION_TIMEOUT, (unsigned)30,
		MHD_OPTION_NOTIFY_COMPLETED, completed, NULL,
		MHD_OPTION_END);

	if (!d) {
		fprintf(stderr, "nodeagent: cannot listen on port %u\n", a.o.port);
		return 1;
	}
	fprintf(stderr, "nodeagent %s: listening on port %u, recordings %s\n", FWT_VERSION,
		a.o.port, a.o.rec_dir);

	while (!g_stop) {  /* watch the children between requests */
		usleep(200000);
		pthread_mutex_lock(&a.lock);
		na_poll_children(&a);
		pthread_mutex_unlock(&a.lock);
	}

	pthread_mutex_lock(&a.lock);
	if (a.state == NA_RECORDING) {
		const char *code;
		char msg[160];

		na_disarm(&a, &code, msg, sizeof(msg));
	}
	pthread_mutex_unlock(&a.lock);
	MHD_stop_daemon(d);
	if (a.idx_loaded)
		rec_close(&a.idx);
	fprintf(stderr, "nodeagent: stopped\n");
	return 0;
}
