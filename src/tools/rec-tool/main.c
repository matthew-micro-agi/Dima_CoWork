/*
 * rec-tool: inspect and lock recordings in /rec (laptop or board) until the
 * control interface (nodeagent) exists.
 *
 *   rec-tool [--dir DIR] list [-v]          sessions (and segments with -v), locks
 *   rec-tool [--dir DIR] verify             manifest vs files vs SEIs; exit 0 = clean
 *   rec-tool [--dir DIR] lock --session N --from I --to I --ttl S [--reason TEXT]
 *   rec-tool [--dir DIR] unlock --id N
 *
 * list/verify only read. lock/unlock go to the running recorder over its
 * control socket; if none runs, rec-tool opens DIR itself (same rules).
 */
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "rec_ctl.h"
#include "rec_scan.h"
#include "recorder.h"

static const char *g_dir = "/rec", *g_sock = REC_CTL_SOCK_DEFAULT;

static void usage(void)
{
	fprintf(stderr,
		"usage: rec-tool [--dir DIR] [--sock PATH] COMMAND\n"
		"  list [-v]                         sessions, locks (-v: segments)\n"
		"  verify                            manifest vs files vs SEIs\n"
		"  lock --session N --from I --to I --ttl S [--reason TEXT]\n"
		"  unlock --id N\n");
}

static const char *wall_str(int64_t ns, char *buf, size_t len)
{
	time_t t = (time_t)(ns / 1000000000LL);
	struct tm tm;

	gmtime_r(&t, &tm);
	strftime(buf, len, "%Y-%m-%d %H:%M:%S", &tm);
	return buf;
}

static int cmd_list(bool verbose)
{
	struct recorder r;
	int e = rec_open_readonly(&r, g_dir);
	int64_t now = rec_now_wall_ns();
	char tb[32];

	if (e) {
		fprintf(stderr, "rec-tool: %s: %s\n", g_dir, strerror(-e));
		return 1;
	}
	printf("%-10s %-11s %-19s %4s %11s %9s %7s %6s %9s  %s\n", "session", "status",
	       "started (UTC)", "fps", "index", "frames", "missing", "segs", "MB", "notes");
	for (size_t k = 0; k < r.nsess; k++) {
		const struct rec_sess *s = &r.sess[k];
		uint64_t bytes = 0, live = 0;
		int64_t lo = -1, hi = -1;

		for (size_t i = 0; i < r.nsegs; i++) {
			const struct rec_seg *g = &r.segs[i];

			if (g->session != s->session || g->deleted)
				continue;
			live++;
			bytes += g->i.bytes;
			if (lo < 0 || g->i.first_idx < lo)
				lo = g->i.first_idx;
			if (g->i.last_idx > hi)
				hi = g->i.last_idx;
		}

		char range[32];

		snprintf(range, sizeof(range), live ? "%" PRId64 "-%" PRId64 : "-", lo, hi);
		printf("%-10" PRIu32 " %-11s %-19s %4d %11s %9" PRIu64 " %7" PRIu64 " %3" PRIu64
		       "/%-2" PRIu64 " %9.1f  %s\n",
		       s->session, rec_status_str(s->status), wall_str(s->wall_ns, tb, sizeof(tb)),
		       (int)((1000000000LL + s->period_ns / 2) / s->period_ns), range, s->frames,
		       s->missing, live, s->segments, bytes / 1048576.0,
		       s->deleted_segments ? "truncated by ring" : "");
		if (!verbose)
			continue;
		for (size_t i = 0; i < r.nsegs; i++) {
			const struct rec_seg *g = &r.segs[i];

			if (g->session != s->session)
				continue;
			printf("    segment %10" PRId64 "-%-10" PRId64 " frames %4u missing %3u keys %u"
			       " bytes %9" PRIu64 "%s%s%s\n", g->i.first_idx, g->i.last_idx,
			       g->i.frames, g->i.missing, g->i.n_keys, g->i.bytes,
			       g->deleted ? " DELETED" : "",
			       !g->deleted && rec_seg_locked(&r, g, now) ? " LOCKED" : "",
			       (g->i.flags & REC_SEG_F_RECOVERED) ? " recovered" : "");
		}
	}
	for (size_t i = 0; i < r.nlocks; i++) {
		const struct rec_lock *l = &r.locks[i];
		const char *st = !l->active ? "unlocked" : l->i.until_wall_ns > now ? "active"
										 : "expired";

		printf("lock %" PRIu64 ": session %" PRIu32 " [%" PRId64 ", %" PRId64 "] %s until %s"
		       " UTC (%s)\n", l->id, l->session, l->i.from_idx, l->i.to_idx, st,
		       wall_str(l->i.until_wall_ns, tb, sizeof(tb)), l->i.reason);
	}
	printf("used %.1f MB, locked %.1f MB, manifest records %" PRIu64 "\n",
	       rec_used_bytes(&r) / 1048576.0, rec_locked_bytes(&r, now) / 1048576.0, r.seq);
	rec_close(&r);
	return 0;
}

/* Check one segment file against its record. Returns the number of problems. */
static int verify_seg(const struct recorder *r, const struct rec_seg *g)
{
	char path[512];
	size_t len;
	struct rec_pic *p = NULL;
	int bad = 0;

	rec_seg_path(r, g->session, g->i.first_idx, path, sizeof(path));

	uint8_t *buf = rec_read_file(path, &len);

#define BAD(...)                                                                   \
	do {                                                                       \
		printf("BAD session %" PRIu32 " segment %" PRId64 ": ", g->session,  \
		       g->i.first_idx);                                            \
		printf(__VA_ARGS__);                                               \
		printf("\n");                                                      \
		bad++;                                                             \
	} while (0)

	if (!buf) {
		BAD("file missing (%s)", path);
		return bad;
	}
	if (len != g->i.bytes)
		BAD("size %zu, record says %" PRIu64, len, g->i.bytes);

	long n = rec_scan(buf, len, &p);
	int64_t prev = -1, missing = 0;
	unsigned k = 0;

	if (n != g->i.frames)
		BAD("%ld pictures, record says %u", n, g->i.frames);
	if (n > 0 && (!p[0].key || p[0].au_off != 0))
		BAD("does not start with a keyframe");
	for (long i = 0; i < n; i++) {
		if (!p[i].has_sei) {
			BAD("picture %ld has no SEI", i);
			continue;
		}
		if (p[i].sei.session != g->session)
			BAD("picture %ld SEI session %" PRIu32, i, p[i].sei.session);
		if (prev >= 0 && p[i].sei.index <= prev)
			BAD("index %" PRId64 " after %" PRId64, p[i].sei.index, prev);
		if (prev >= 0 && p[i].sei.index > prev + 1)
			missing += p[i].sei.index - prev - 1;
		prev = p[i].sei.index;
		if (p[i].key && k < g->i.n_keys) {
			if (p[i].au_off != g->i.key_off[k])
				BAD("keyframe %u at byte %zu, record says %u", k, p[i].au_off,
				    g->i.key_off[k]);
			k++;
		}
	}
	if (n > 0 && p[0].has_sei && p[0].sei.index != g->i.first_idx)
		BAD("first index %" PRId64 ", record says %" PRId64, p[0].sei.index, g->i.first_idx);
	if (n > 0 && prev != g->i.last_idx)
		BAD("last index %" PRId64 ", record says %" PRId64, prev, g->i.last_idx);
	if (missing != g->i.missing)
		BAD("%" PRId64 " missing inside, record says %u", missing, g->i.missing);
	free(p);
	free(buf);
	return bad;
}

static int cmd_verify(void)
{
	struct recorder r;
	int e = rec_open_readonly(&r, g_dir), bad = 0;
	uint64_t checked = 0, deleted = 0, orphans = 0;
	char path[512];
	struct stat st;

	if (e) {
		fprintf(stderr, "rec-tool: %s: %s\n", g_dir, strerror(-e));
		return 1;
	}
	if (r.st.torn_bytes_cut)
		printf("NOTE manifest has a %" PRIu64 "-byte torn tail (cut at the next recorder "
		       "start)\n", r.st.torn_bytes_cut);
	for (size_t i = 0; i < r.nsegs; i++) {
		const struct rec_seg *g = &r.segs[i];

		if (g->deleted) {
			deleted++;
			rec_seg_path(&r, g->session, g->i.first_idx, path, sizeof(path));
			if (stat(path, &st) == 0) {
				printf("BAD deleted segment still on disk: %s\n", path);
				bad++;
			}
			continue;
		}
		checked++;
		bad += verify_seg(&r, g);
	}
	/* Files no record knows about (a running session's open segment is one). */
	for (size_t k = 0; k < r.nsess; k++) {
		char dir[300];
		DIR *d;
		struct dirent *de;

		snprintf(dir, sizeof(dir), "%s/s%010" PRIu32, g_dir, r.sess[k].session);
		d = opendir(dir);
		while (d && (de = readdir(d))) {
			char *end;
			long long v = strtoll(de->d_name, &end, 10);
			bool known = false;

			if (end == de->d_name || strcmp(end, ".h265"))
				continue;
			for (size_t i = 0; i < r.nsegs && !known; i++)
				known = r.segs[i].session == r.sess[k].session &&
					r.segs[i].i.first_idx == v;
			if (!known) {
				orphans++;
				printf("%s file without record: %s/%s%s\n",
				       r.sess[k].status == REC_ST_RECORDING ? "NOTE" : "BAD", dir,
				       de->d_name,
				       r.sess[k].status == REC_ST_RECORDING ? " (open segment)" : "");
				bad += r.sess[k].status != REC_ST_RECORDING;
			}
		}
		if (d)
			closedir(d);
	}
	printf("summary: sessions %zu, segments %" PRIu64 " checked, %" PRIu64 " deleted, "
	       "files without record %" PRIu64 ", problems %d\n", r.nsess, checked, deleted,
	       orphans, bad);
	rec_close(&r);
	return bad ? 1 : 0;
}

/* Send a request to the running recorder. 1 = no recorder listening. */
static int ctl_request(struct rec_ctl_msg *req, struct rec_ctl_msg *rep)
{
	struct sockaddr_un to = { .sun_family = AF_UNIX }, self = { .sun_family = AF_UNIX };

	if (strlen(g_sock) >= sizeof(to.sun_path))
		return 1;
	strcpy(to.sun_path, g_sock);

	int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);

	if (fd < 0 || bind(fd, (struct sockaddr *)&self, sizeof(sa_family_t)) != 0 ||
	    sendto(fd, req, sizeof(*req), 0, (struct sockaddr *)&to, sizeof(to)) < 0) {
		if (fd >= 0)
			close(fd);
		return 1;
	}

	struct pollfd p = { .fd = fd, .events = POLLIN };
	ssize_t n = -1;

	if (poll(&p, 1, 3000) == 1)
		n = recv(fd, rep, sizeof(*rep), 0);
	close(fd);
	if (n < 0 || !rec_ctl_valid(rep, n)) {
		fprintf(stderr, "rec-tool: no reply from %s\n", g_sock);
		return -1;
	}
	return 0;
}

static int cmd_lock(int argc, char **argv, bool unlock)
{
	static const struct option lo[] = {
		{ "session", required_argument, NULL, 's' }, { "from", required_argument, NULL, 'f' },
		{ "to", required_argument, NULL, 't' },      { "ttl", required_argument, NULL, 'T' },
		{ "reason", required_argument, NULL, 'r' },  { "id", required_argument, NULL, 'i' },
		{ NULL, 0, NULL, 0 },
	};
	struct rec_ctl_msg req, rep;
	int c;

	rec_ctl_init(&req, unlock ? REC_CTL_UNLOCK : REC_CTL_LOCK);
	req.from_idx = req.to_idx = -1;
	optind = 1;
	while ((c = getopt_long(argc, argv, "", lo, NULL)) != -1) {
		switch (c) {
		case 's': req.session = (uint32_t)strtoul(optarg, NULL, 0); break;
		case 'f': req.from_idx = strtoll(optarg, NULL, 0); break;
		case 't': req.to_idx = strtoll(optarg, NULL, 0); break;
		case 'T': req.ttl_s = strtoll(optarg, NULL, 0); break;
		case 'r': snprintf(req.text, sizeof(req.text), "%s", optarg); break;
		case 'i': req.lock_id = strtoull(optarg, NULL, 0); break;
		default: usage(); return 2;
		}
	}
	if (unlock ? !req.lock_id : (!req.session || req.from_idx < 0 || req.to_idx < 0 ||
				     req.ttl_s <= 0)) {
		usage();
		return 2;
	}

	int via = ctl_request(&req, &rep);

	if (via < 0)
		return 1;
	if (via == 1) {  /* no recorder running: open /rec ourselves */
		struct recorder r;
		struct rec_config cfg = { .dir = g_dir, .log = stderr };
		int e = rec_open(&r, &cfg);

		if (e) {
			fprintf(stderr, "rec-tool: %s: %s%s\n", g_dir, strerror(-e),
				e == -EBUSY ? " (recorder running but its socket is not)" : "");
			return 1;
		}
		rec_ctl_init(&rep, REC_CTL_REPLY);
		if (unlock) {
			rep.result = rec_unlock(&r, req.lock_id);
			rep.lock_id = req.lock_id;
			snprintf(rep.text, sizeof(rep.text), "%s", rep.result ? "no such lock"
								     : "unlocked");
		} else {
			rep.result = rec_lock(&r, req.session, req.from_idx, req.to_idx, req.ttl_s,
					      req.text, &rep.lock_id, rep.text, sizeof(rep.text));
			if (!rep.result)
				snprintf(rep.text, sizeof(rep.text), "locked");
		}
		rec_close(&r);
	}
	printf("%s %s: %s (lock id %" PRIu64 ", %s)\n", unlock ? "unlock" : "lock",
	       rep.result ? "FAILED" : "ok", rep.text, rep.lock_id,
	       via == 1 ? "offline" : "via running recorder");
	return rep.result ? 1 : 0;
}

int main(int argc, char **argv)
{
	int i = 1;

	for (; i < argc - 1 && argv[i][0] == '-' && argv[i][1] == '-'; i += 2) {
		if (!strcmp(argv[i], "--dir"))
			g_dir = argv[i + 1];
		else if (!strcmp(argv[i], "--sock"))
			g_sock = argv[i + 1];
		else
			break;
	}
	if (i >= argc) {
		usage();
		return 2;
	}

	const char *cmd = argv[i];

	if (!strcmp(cmd, "list"))
		return cmd_list(i + 1 < argc && !strcmp(argv[i + 1], "-v"));
	if (!strcmp(cmd, "verify"))
		return cmd_verify();
	if (!strcmp(cmd, "lock"))
		return cmd_lock(argc - i, argv + i, false);
	if (!strcmp(cmd, "unlock"))
		return cmd_lock(argc - i, argv + i, true);
	usage();
	return 2;
}
