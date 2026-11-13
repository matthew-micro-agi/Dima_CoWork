#include "recorder.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

#include "rec_scan.h"

#define NSEC 1000000000LL

static void logf_(const struct recorder *r, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

static void logf_(const struct recorder *r, const char *fmt, ...)
{
	va_list ap;

	if (!r->cfg.log)
		return;
	va_start(ap, fmt);
	fputs("recorder: ", r->cfg.log);
	vfprintf(r->cfg.log, fmt, ap);
	fputc('\n', r->cfg.log);
	va_end(ap);
}

int64_t rec_now_wall_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	return (int64_t)ts.tv_sec * NSEC + ts.tv_nsec;
}

/* ---- in-memory index --------------------------------------------------- */

#define GROW(arr, n, cap)                                                        \
	do {                                                                     \
		if ((n) == (cap)) {                                              \
			size_t nc = (cap) ? (cap) * 2 : 64;                      \
			void *p = realloc((arr), nc * sizeof(*(arr)));           \
			if (!p)                                                  \
				return -ENOMEM;                                  \
			(arr) = p;                                               \
			(cap) = nc;                                              \
		}                                                                \
	} while (0)

static struct rec_sess *find_sess(const struct recorder *r, uint32_t session)
{
	for (size_t i = 0; i < r->nsess; i++)
		if (r->sess[i].session == session)
			return &r->sess[i];
	return NULL;
}

const struct rec_sess *rec_find_session(const struct recorder *r, uint32_t session)
{
	return find_sess(r, session);
}

static struct rec_seg *find_seg(const struct recorder *r, uint32_t session, int64_t first_idx)
{
	for (size_t i = r->nsegs; i-- > 0;)
		if (r->segs[i].session == session && r->segs[i].i.first_idx == first_idx)
			return &r->segs[i];
	return NULL;
}

/* Apply one record to the index (load and append share this). */
static int apply(struct recorder *r, const struct rec_record *x)
{
	struct rec_sess *s = find_sess(r, x->session);
	struct rec_seg *g;

	switch (x->type) {
	case REC_SESSION_START:
		GROW(r->sess, r->nsess, r->capsess);
		r->sess[r->nsess++] = (struct rec_sess){
			.session = x->session, .t0_ns = x->u.start.t0_ns,
			.period_ns = x->u.start.period_ns, .wall_ns = x->u.start.wall_ns,
			.status = REC_ST_RECORDING, .last_index = -1 };
		break;
	case REC_SESSION_END:
		if (s) {
			s->status = x->u.end.status;
			s->last_index = x->u.end.last_index;
			s->frames = x->u.end.frames;
			s->missing = x->u.end.missing;
		}
		break;
	case REC_SEGMENT:
		GROW(r->segs, r->nsegs, r->capsegs);
		r->segs[r->nsegs++] = (struct rec_seg){ .session = x->session, .i = x->u.seg };
		if (s) {
			s->segments++;
			if (x->u.seg.last_idx > s->last_index)
				s->last_index = x->u.seg.last_idx;
		}
		break;
	case REC_DELETE:
		g = find_seg(r, x->session, x->u.del.first_idx);
		if (g && !g->deleted) {
			g->deleted = true;
			if (s)
				s->deleted_segments++;
		}
		break;
	case REC_LOCK:
		GROW(r->locks, r->nlocks, r->caplocks);
		r->locks[r->nlocks++] = (struct rec_lock){ .id = x->seq, .session = x->session,
							   .i = x->u.lock, .active = true };
		break;
	case REC_UNLOCK:
		for (size_t i = 0; i < r->nlocks; i++)
			if (r->locks[i].id == x->u.unlock.lock_seq)
				r->locks[i].active = false;
		break;
	}
	return 0;
}

/* Append a record durably, then apply it (manifest first: it is the truth). */
static int append(struct recorder *r, struct rec_record *x)
{
	if (r->readonly)
		return -EROFS;
	x->seq = r->seq + 1;

	int e = rec_manifest_append(r->mfd, x);

	if (e) {
		logf_(r, "manifest append (%s) failed: %s", rec_type_str(x->type), strerror(-e));
		return e;
	}
	r->seq = x->seq;
	return apply(r, x);
}

/* ---- paths, sizes, locks ------------------------------------------------ */

static void sess_dir(const struct recorder *r, uint32_t session, char *buf, size_t len)
{
	snprintf(buf, len, "%s/s%010" PRIu32, r->dir, session);
}

void rec_seg_path(const struct recorder *r, uint32_t session, int64_t first_idx, char *buf,
		  size_t len)
{
	snprintf(buf, len, "%s/s%010" PRIu32 "/%010" PRId64 ".h265", r->dir, session, first_idx);
}

static void fsync_path(const char *path)
{
	int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);

	if (fd >= 0) {
		fsync(fd);
		close(fd);
	}
}

uint64_t rec_used_bytes(const struct recorder *r)
{
	uint64_t u = r->seg_open ? r->cur.bytes : 0;

	for (size_t i = 0; i < r->nsegs; i++)
		if (!r->segs[i].deleted)
			u += r->segs[i].i.bytes;
	return u;
}

static bool lock_live(const struct rec_lock *l, int64_t now)
{
	return l->active && l->i.until_wall_ns > now;
}

static bool overlaps(int64_t a0, int64_t a1, int64_t b0, int64_t b1)
{
	return a0 <= b1 && b0 <= a1;
}

bool rec_seg_locked(const struct recorder *r, const struct rec_seg *s, int64_t now)
{
	for (size_t i = 0; i < r->nlocks; i++) {
		const struct rec_lock *l = &r->locks[i];

		if (l->session == s->session && lock_live(l, now) &&
		    overlaps(l->i.from_idx, l->i.to_idx, s->i.first_idx, s->i.last_idx))
			return true;
	}
	return false;
}

/* Bytes per frame of the running session (estimate for future lock windows). */
static uint64_t bytes_per_frame(const struct recorder *r)
{
	uint64_t bytes = r->seg_open ? r->cur.bytes : 0, frames = r->seg_open ? r->cur.frames : 0;

	for (size_t i = 0; i < r->nsegs; i++)
		if (r->segs[i].session == r->cur_session) {
			bytes += r->segs[i].i.bytes;
			frames += r->segs[i].i.frames;
		}
	if (frames)
		return bytes / frames;
	return r->seg_frames ? r->cfg.min_seg_bytes / (uint64_t)r->seg_frames : 0;
}

/* Locked bytes now plus the estimated future part of live locks. */
static uint64_t locked_bytes_with(const struct recorder *r, int64_t now,
				  const struct rec_lock *extra)
{
	uint64_t b = 0;

	for (size_t i = 0; i < r->nsegs; i++) {
		const struct rec_seg *s = &r->segs[i];

		if (s->deleted)
			continue;
		if (rec_seg_locked(r, s, now) ||
		    (extra && extra->session == s->session &&
		     overlaps(extra->i.from_idx, extra->i.to_idx, s->i.first_idx, s->i.last_idx)))
			b += s->i.bytes;
	}
	if (r->active) {
		int64_t done = r->have_last ? r->last_idx : -1;
		uint64_t bpf = bytes_per_frame(r);

		for (size_t i = 0; i <= r->nlocks; i++) {
			const struct rec_lock *l = i < r->nlocks ? &r->locks[i] : extra;

			if (!l || l->session != r->cur_session ||
			    (i < r->nlocks && !lock_live(l, now)) || l->i.to_idx <= done)
				continue;

			int64_t from = l->i.from_idx > done ? l->i.from_idx : done + 1;

			b += (uint64_t)(l->i.to_idx - from + 1) * bpf;
		}
	}
	return b;
}

uint64_t rec_locked_bytes(const struct recorder *r, int64_t now)
{
	return locked_bytes_with(r, now, NULL);
}

static int fs_space(const struct recorder *r, uint64_t *free_b, uint64_t *total_b)
{
	struct statvfs v;

	if (statvfs(r->dir, &v) != 0)
		return -errno;
	*free_b = (uint64_t)v.f_bavail * v.f_frsize;
	*total_b = (uint64_t)v.f_blocks * v.f_frsize;
	return 0;
}

static uint64_t ring_capacity(const struct recorder *r)
{
	uint64_t fr = 0, tot = 0, cap;

	fs_space(r, &fr, &tot);
	cap = tot > r->cfg.reserve_bytes ? tot - r->cfg.reserve_bytes : 0;
	if (r->cfg.ring_max_bytes && r->cfg.ring_max_bytes < cap)
		cap = r->cfg.ring_max_bytes;
	return cap;
}

/* ---- ring ---------------------------------------------------------------- */

static int delete_seg(struct recorder *r, struct rec_seg *s)
{
	struct rec_record x;
	char path[512], dir[300];
	uint32_t session = s->session;

	rec_record_init(&x, REC_DELETE, session);
	x.u.del.first_idx = s->i.first_idx;
	/* Record first: a crash after it leaves a stale file, removed at start. */
	int e = append(r, &x);

	if (e)
		return e;
	rec_seg_path(r, session, x.u.del.first_idx, path, sizeof(path));
	if (unlink(path) != 0 && errno != ENOENT)
		logf_(r, "unlink %s: %s", path, strerror(errno));
	r->st.segments_deleted++;

	const struct rec_sess *ss = find_sess(r, session);

	if (ss && ss->deleted_segments == ss->segments && !(r->active && session == r->cur_session)) {
		sess_dir(r, session, dir, sizeof(dir));
		rmdir(dir);  /* fine if not empty */
		r->st.sessions_emptied++;
	}
	return 0;
}

/*
 * Free space by one step (architecture §5.4): the oldest closed session with
 * unlocked segments loses all of them; else the oldest unlocked segment of
 * the running session. False if nothing can be deleted.
 */
static bool reclaim_one(struct recorder *r)
{
	int64_t now = rec_now_wall_ns();

	for (size_t k = 0; k < r->nsess; k++) {
		uint32_t sn = r->sess[k].session;
		bool any = false;

		if (r->active && sn == r->cur_session)
			continue;
		for (size_t i = 0; i < r->nsegs; i++) {
			struct rec_seg *s = &r->segs[i];

			if (s->session == sn && !s->deleted && !rec_seg_locked(r, s, now)) {
				if (delete_seg(r, s) == 0)  /* doesn't grow segs: s stays valid */
					any = true;
			}
		}
		if (any) {
			logf_(r, "ring: freed closed session %" PRIu32, sn);
			return true;
		}
	}
	if (r->active) {
		for (size_t i = 0; i < r->nsegs; i++) {
			struct rec_seg *s = &r->segs[i];

			if (s->session == r->cur_session && !s->deleted &&
			    !rec_seg_locked(r, s, now)) {
				logf_(r, "ring: freed running session %" PRIu32 " segment %" PRId64,
				      s->session, s->i.first_idx);
				return delete_seg(r, s) == 0;
			}
		}
	}
	return false;
}

static int make_room(struct recorder *r, uint64_t need)
{
	for (;;) {
		uint64_t fr = 0, tot = 0, avail, used = rec_used_bytes(r);
		int e = fs_space(r, &fr, &tot);

		if (e)
			return e;
		avail = fr > r->cfg.reserve_bytes ? fr - r->cfg.reserve_bytes : 0;
		if (r->cfg.ring_max_bytes) {
			uint64_t ring_free = r->cfg.ring_max_bytes > used ?
					     r->cfg.ring_max_bytes - used : 0;

			if (ring_free < avail)
				avail = ring_free;
		}
		if (avail >= need)
			return 0;
		if (!reclaim_one(r))
			return -ENOSPC;
	}
}

/* Expected size of the next segment: the last closed one, else the default. */
static uint64_t expected_seg_bytes(const struct recorder *r)
{
	for (size_t i = r->nsegs; i-- > 0;)
		if (r->segs[i].session == r->cur_session)
			return r->segs[i].i.bytes > r->cfg.min_seg_bytes ? r->segs[i].i.bytes
									  : r->cfg.min_seg_bytes;
	return r->cfg.min_seg_bytes;
}

/* ---- segments ------------------------------------------------------------ */

static int64_t mono_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void timed(struct rec_timing *t, int64_t start_ns)
{
	int64_t d = mono_ns() - start_ns;

	t->n++;
	t->total_ns += d;
	if (d > t->max_ns)
		t->max_ns = d;
}

static int close_segment(struct recorder *r)
{
	struct rec_record x;
	char dir[300];

	if (!r->seg_open)
		return 0;
	if (fsync(r->seg_fd) != 0)
		logf_(r, "fsync segment: %s", strerror(errno));
	close(r->seg_fd);
	r->seg_open = false;
	sess_dir(r, r->cur_session, dir, sizeof(dir));
	fsync_path(dir);  /* the new directory entry is durable too */

	rec_record_init(&x, REC_SEGMENT, r->cur_session);
	x.u.seg = r->cur;
	r->st.segments_closed++;
	return append(r, &x);
}

static int open_segment(struct recorder *r, int64_t index, int64_t t_ns)
{
	char path[512];
	int e = make_room(r, expected_seg_bytes(r) * 3 / 2);

	if (e) {
		r->st.no_room++;
		logf_(r, "no room for a new segment (ring full of locked data?)");
		return e;
	}
	rec_seg_path(r, r->cur_session, index, path, sizeof(path));
	r->seg_fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
	if (r->seg_fd < 0) {
		e = -errno;
		logf_(r, "open %s: %s", path, strerror(errno));
		return e;
	}
	r->seg_open = true;
	r->cur = (struct rec_seg_info){ .first_idx = index, .last_idx = index,
					.t_first = t_ns, .t_last = t_ns };
	r->boundary = (index / r->seg_frames + 1) * r->seg_frames;
	return 0;
}

int rec_write_frame(struct recorder *r, const struct iovec *iov, int iovcnt, int64_t index,
		    int64_t t_ns, bool key)
{
	int e;

	if (!r->active || index < 0)
		return -EINVAL;
	/* DL-18: cut at the first keyframe at or after the boundary. */
	int64_t t0;

	if (r->seg_open && key && index >= r->boundary) {
		t0 = mono_ns();
		e = close_segment(r);
		timed(&r->st.t_close, t0);
		if (e)
			return e;
	}
	if (!r->seg_open) {
		if (!key) {
			r->st.skipped_no_key++;
			return 1;
		}
		t0 = mono_ns();
		e = open_segment(r, index, t_ns);
		timed(&r->st.t_open, t0);
		if (e)
			return e;
	}
	/* Keyframe: make the previous complete group durable (crash loses <= 1 group). */
	if (key && r->cur.bytes > 0) {
		t0 = mono_ns();
		if (fdatasync(r->seg_fd) != 0)
			logf_(r, "fdatasync: %s", strerror(errno));
		timed(&r->st.t_keysync, t0);
	}
	if (key) {
		if (r->cur.n_keys < REC_MAX_KEYS) {
			r->cur.key_off[r->cur.n_keys] = (uint32_t)r->cur.bytes;
			r->cur.key_didx[r->cur.n_keys] = (uint16_t)(index - r->cur.first_idx);
			r->cur.n_keys++;
		} else {
			r->cur.flags |= REC_SEG_F_KEYS_FULL;
		}
	}

	struct rec_sess *s = find_sess(r, r->cur_session);
	int64_t miss = (r->have_last && index > r->last_idx + 1) ? index - r->last_idx - 1 : 0;

	ssize_t want = 0, done;

	for (int i = 0; i < iovcnt; i++)
		want += (ssize_t)iov[i].iov_len;
	t0 = mono_ns();
	do {
		done = writev(r->seg_fd, iov, iovcnt);
	} while (done < 0 && errno == EINTR);
	timed(&r->st.t_write, t0);
	if (done != want) {
		e = done < 0 ? -errno : -EIO;
		logf_(r, "write segment: %s", done < 0 ? strerror(errno) : "short write");
		return e;
	}

	r->cur.bytes += (uint64_t)done;
	r->cur.frames++;
	if (r->cur.frames > 1)
		r->cur.missing += (uint16_t)miss;
	r->cur.last_idx = index;
	r->cur.t_last = t_ns;
	r->have_last = true;
	r->last_idx = index;
	if (s) {
		s->frames++;
		s->missing += (uint64_t)miss;
		s->last_index = index;
	}
	r->st.frames++;
	r->st.bytes += (uint64_t)done;
	return 0;
}

/* ---- sessions -------------------------------------------------------------- */

int rec_session_start(struct recorder *r, uint32_t session, int64_t t0_ns, int64_t period_ns)
{
	char dir[300];
	struct stat st;
	struct rec_record x;

	if (r->active || period_ns <= 0 || session == 0)
		return -EINVAL;
	sess_dir(r, session, dir, sizeof(dir));
	if (find_sess(r, session) || stat(dir, &st) == 0) {
		logf_(r, "session %" PRIu32 " already exists on disk: refused (DL-20)", session);
		return -EEXIST;
	}
	if (mkdir(dir, 0755) != 0)
		return -errno;
	fsync_path(r->dir);

	rec_record_init(&x, REC_SESSION_START, session);
	x.u.start.t0_ns = t0_ns;
	x.u.start.period_ns = period_ns;
	x.u.start.wall_ns = rec_now_wall_ns();

	int e = append(r, &x);

	if (e)
		return e;
	r->active = true;
	r->cur_session = session;
	r->seg_frames = (r->cfg.seg_seconds * NSEC + period_ns / 2) / period_ns;
	if (r->seg_frames < 1)
		r->seg_frames = 1;
	r->have_last = false;
	logf_(r, "session %" PRIu32 " started: %" PRId64 " frames per segment", session,
	      r->seg_frames);
	return 0;
}

static int end_session(struct recorder *r, uint32_t status)
{
	struct rec_record x;
	int e = close_segment(r);
	const struct rec_sess *s = find_sess(r, r->cur_session);

	rec_record_init(&x, REC_SESSION_END, r->cur_session);
	x.u.end.status = status;
	x.u.end.last_index = s ? s->last_index : -1;
	x.u.end.frames = s ? s->frames : 0;
	x.u.end.missing = s ? s->missing : 0;
	x.u.end.wall_ns = rec_now_wall_ns();

	int e2 = append(r, &x);

	r->active = false;
	return e ? e : e2;
}

int rec_session_end(struct recorder *r)
{
	return r->active ? end_session(r, REC_ST_COMPLETE) : -EINVAL;
}

/* ---- locks ------------------------------------------------------------------ */

int rec_lock(struct recorder *r, uint32_t session, int64_t from_idx, int64_t to_idx,
	     int64_t ttl_s, const char *reason, uint64_t *id, char *why, size_t why_len)
{
	int64_t now = rec_now_wall_ns();
	struct rec_lock l = { .session = session, .active = true };

	if (!find_sess(r, session)) {
		snprintf(why, why_len, "unknown session %" PRIu32, session);
		return -ENOENT;
	}
	if (from_idx < 0 || to_idx < from_idx || ttl_s <= 0) {
		snprintf(why, why_len, "need 0 <= from <= to and ttl > 0");
		return -EINVAL;
	}
	l.i.from_idx = from_idx;
	l.i.to_idx = to_idx;
	l.i.until_wall_ns = now + ttl_s * NSEC;
	snprintf(l.i.reason, sizeof(l.i.reason), "%s", reason ? reason : "");

	uint64_t cap = ring_capacity(r) / 100 * (uint64_t)r->cfg.lock_cap_pct;
	uint64_t would = locked_bytes_with(r, now, &l);

	if (would > cap) {
		r->st.locks_refused++;
		snprintf(why, why_len, "lock cap: %.1f MB locked would exceed %.1f MB (%d %% of ring)",
			 would / 1048576.0, cap / 1048576.0, r->cfg.lock_cap_pct);
		logf_(r, "lock refused: %s", why);
		return -EDQUOT;
	}

	struct rec_record x;

	rec_record_init(&x, REC_LOCK, session);
	x.u.lock = l.i;

	int e = append(r, &x);

	if (e) {
		snprintf(why, why_len, "manifest write failed");
		return e;
	}
	*id = x.seq;
	logf_(r, "lock %" PRIu64 ": session %" PRIu32 " [%" PRId64 ", %" PRId64 "] for %" PRId64
	      " s (%s)", *id, session, from_idx, to_idx, ttl_s, l.i.reason);
	return 0;
}

int rec_unlock(struct recorder *r, uint64_t id)
{
	for (size_t i = 0; i < r->nlocks; i++) {
		if (r->locks[i].id == id && r->locks[i].active) {
			struct rec_record x;

			rec_record_init(&x, REC_UNLOCK, r->locks[i].session);
			x.u.unlock.lock_seq = id;
			return append(r, &x);
		}
	}
	return -ENOENT;
}

/* ---- open, recovery, close ------------------------------------------------- */

static int load(struct recorder *r, bool cut_tail)
{
	struct rec_record *v;
	size_t valid;
	long n = rec_manifest_read(r->mfd, &v, &valid);

	if (n < 0)
		return (int)n;

	off_t size = lseek(r->mfd, 0, SEEK_END);

	if (size > (off_t)valid) {
		r->st.torn_bytes_cut = (uint64_t)(size - (off_t)valid);
		if (cut_tail) {
			logf_(r, "manifest: cut %" PRIu64 " bytes of a torn or corrupt tail",
			      r->st.torn_bytes_cut);
			if (ftruncate(r->mfd, (off_t)valid) != 0 || fsync(r->mfd) != 0) {
				free(v);
				return -errno;
			}
		}
	}
	for (long i = 0; i < n; i++) {
		int e = apply(r, &v[i]);

		if (e) {
			free(v);
			return e;
		}
		r->seq = v[i].seq;
	}
	free(v);
	return 0;
}

static int cmp_i64(const void *a, const void *b)
{
	int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;

	return (x > y) - (x < y);
}

/*
 * Rebuild a segment record from a file without one: keep complete keyframe
 * groups (everything before the last keyframe), drop the rest.
 */
static int recover_file(struct recorder *r, uint32_t session, int64_t first_idx)
{
	char path[512];
	size_t len;
	uint8_t *buf;
	struct rec_pic *p = NULL;
	long n;

	rec_seg_path(r, session, first_idx, path, sizeof(path));
	buf = rec_read_file(path, &len);
	if (!buf)
		return -errno;
	n = rec_scan(buf, len, &p);
	free(buf);
	if (n < 0)
		return -ENOMEM;

	long last_key = -1, nkeys = 0;

	for (long i = 0; i < n; i++)
		if (p[i].key) {
			last_key = i;
			nkeys++;
		}
	if (nkeys < 2 || !p[0].key || !p[0].has_sei) {  /* no complete group */
		free(p);
		unlink(path);
		r->st.recovery_dropped_files++;
		logf_(r, "recovery: %s had no complete keyframe group, removed", path);
		return 0;
	}

	struct rec_record x;
	struct rec_seg_info *g = &x.u.seg;
	long keep = last_key;  /* pictures [0, keep) */

	rec_record_init(&x, REC_SEGMENT, session);
	g->first_idx = p[0].sei.index;
	g->t_first = p[0].sei.t_ns;
	g->flags = REC_SEG_F_RECOVERED;
	for (long i = 0; i < keep; i++) {
		if (!p[i].has_sei)
			continue;
		if (p[i].key) {
			if (g->n_keys < REC_MAX_KEYS) {
				g->key_off[g->n_keys] = (uint32_t)p[i].au_off;
				g->key_didx[g->n_keys] = (uint16_t)(p[i].sei.index - g->first_idx);
				g->n_keys++;
			} else {
				g->flags |= REC_SEG_F_KEYS_FULL;
			}
		}
		g->last_idx = p[i].sei.index;
		g->t_last = p[i].sei.t_ns;
		g->frames++;
	}
	g->bytes = p[keep].au_off;
	g->missing = (uint16_t)(g->last_idx - g->first_idx + 1 - g->frames);
	free(p);

	int fd = open(path, O_WRONLY | O_CLOEXEC);

	if (fd < 0 || ftruncate(fd, (off_t)g->bytes) != 0 || fsync(fd) != 0) {
		int e = -errno;

		if (fd >= 0)
			close(fd);
		return e;
	}
	close(fd);
	r->st.recovered_segments++;
	logf_(r, "recovery: session %" PRIu32 " segment %" PRId64 ": kept %u frames (to index %"
	      PRId64 "), %" PRIu64 " bytes", session, g->first_idx, g->frames, g->last_idx,
	      g->bytes);
	return append(r, &x);
}

/* A session without SESSION_END: index its unrecorded files, end it interrupted. */
static int recover_session(struct recorder *r, struct rec_sess *s)
{
	char dir[300];
	DIR *d;
	struct dirent *de;
	int64_t *idx = NULL;
	size_t n = 0, cap = 0;
	uint32_t session = s->session;

	sess_dir(r, session, dir, sizeof(dir));
	d = opendir(dir);
	while (d && (de = readdir(d))) {
		char *end;
		long long v = strtoll(de->d_name, &end, 10);

		if (end == de->d_name || strcmp(end, ".h265") != 0)
			continue;
		if (find_seg(r, session, v))
			continue;  /* has its record: complete */
		GROW(idx, n, cap);
		idx[n++] = v;
	}
	if (d)
		closedir(d);
	qsort(idx, n, sizeof(*idx), cmp_i64);
	for (size_t i = 0; i < n; i++) {
		int e = recover_file(r, session, idx[i]);

		if (e)
			logf_(r, "recovery of segment %" PRId64 ": %s", idx[i], strerror(-e));
	}
	free(idx);

	/* Totals from the segments (s may move if sess grew: look it up again). */
	struct rec_record x;
	uint64_t frames = 0, missing = 0;
	int64_t last = -1;

	for (size_t i = 0; i < r->nsegs; i++)
		if (r->segs[i].session == session) {
			frames += r->segs[i].i.frames;
			missing += r->segs[i].i.missing;
			if (r->segs[i].i.last_idx > last)
				last = r->segs[i].i.last_idx;
		}
	rec_record_init(&x, REC_SESSION_END, session);
	x.u.end.status = REC_ST_INTERRUPTED;
	x.u.end.last_index = last;
	x.u.end.frames = frames;
	x.u.end.missing = missing;
	x.u.end.wall_ns = rec_now_wall_ns();
	logf_(r, "recovery: session %" PRIu32 " marked interrupted (last index %" PRId64 ")",
	      session, last);
	return append(r, &x);
}

static void init_struct(struct recorder *r, const char *dir)
{
	memset(r, 0, sizeof(*r));
	r->mfd = -1;
	r->seg_fd = -1;
	snprintf(r->dir, sizeof(r->dir), "%s", dir);
}

int rec_open(struct recorder *r, const struct rec_config *cfg)
{
	char path[300];
	int e;

	init_struct(r, cfg->dir);
	r->cfg = *cfg;
	if (r->cfg.seg_seconds <= 0)
		r->cfg.seg_seconds = 4;
	if (r->cfg.lock_cap_pct <= 0)
		r->cfg.lock_cap_pct = 20;
	if (!r->cfg.min_seg_bytes)
		r->cfg.min_seg_bytes = 3u << 20;

	snprintf(path, sizeof(path), "%s/%s", r->dir, REC_MANIFEST_NAME);
	r->mfd = open(path, O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
	if (r->mfd < 0)
		return -errno;
	if (flock(r->mfd, LOCK_EX | LOCK_NB) != 0) {
		e = errno == EWOULDBLOCK ? -EBUSY : -errno;
		close(r->mfd);
		r->mfd = -1;
		return e;
	}
	if ((e = load(r, true)) != 0)
		return e;

	/* Half-done deletes: record written, file still there. */
	for (size_t i = 0; i < r->nsegs; i++) {
		if (!r->segs[i].deleted)
			continue;
		rec_seg_path(r, r->segs[i].session, r->segs[i].i.first_idx, path, sizeof(path));
		if (unlink(path) == 0)
			r->st.stale_files_removed++;
	}
	/* Sessions without an end: interrupted by a crash or power loss. */
	for (size_t i = 0; i < r->nsess; i++)
		if (r->sess[i].status == REC_ST_RECORDING && (e = recover_session(r, &r->sess[i])))
			return e;
	logf_(r, "%s: %zu sessions, %zu segment records, %" PRIu64 " MB used", r->dir, r->nsess,
	      r->nsegs, rec_used_bytes(r) >> 20);
	return 0;
}

int rec_open_readonly(struct recorder *r, const char *dir)
{
	char path[300];

	init_struct(r, dir);
	r->readonly = true;
	snprintf(path, sizeof(path), "%s/%s", r->dir, REC_MANIFEST_NAME);
	r->mfd = open(path, O_RDONLY | O_CLOEXEC);
	if (r->mfd < 0)
		return -errno;
	return load(r, false);
}

static void free_all(struct recorder *r)
{
	if (r->seg_fd >= 0 && r->seg_open)
		close(r->seg_fd);
	if (r->mfd >= 0)
		close(r->mfd);  /* also releases the flock */
	free(r->segs);
	free(r->sess);
	free(r->locks);
	r->segs = NULL;
	r->sess = NULL;
	r->locks = NULL;
	r->mfd = -1;
	r->seg_open = false;
}

void rec_close(struct recorder *r)
{
	if (r->active && !r->readonly)
		rec_session_end(r);
	free_all(r);
}

void rec_abandon(struct recorder *r)
{
	free_all(r);
}
