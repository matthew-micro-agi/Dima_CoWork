/*
 * fwt-rec: one recording on one node, from a laptop or any Linux box.
 *
 *   fwt-rec 10.42.0.132 --duration 30 --out run1.h265 [--fps 60] [--session N]
 *   fwt-rec 10.42.0.132 --info      node identity and health, read-only
 *
 * identity -> health (must not be recording) -> arm (new session) -> wait on
 * the node's own clock until T0 + duration, with progress -> disarm (always,
 * also on Ctrl-C or an error) -> fetch the whole session into --out (split
 * along segments, at most 600 s per fetch) -> <out>.json sidecar -> check the
 * file with sei-dump when it is available.
 *
 * Exit code: 0 recorded, nothing missing, check passed (or no checker here);
 * 1 failed or something missing; 2 usage.
 *
 * Start of the controller code: ctl_node (protocol, zcbor) is transport-free,
 * ctl_http is the only HTTP part.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ctl_http.h"
#include "ctl_node.h"
#include "fwt_version.h"
#include "rec_json.h"

#define MAX_FETCH_S   600   /* node limit per fetch (agent.h NA_MAX_FETCH_S) */
#define MAX_SEGMENTS  8192  /* 9 h of 4 s segments */

static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
	(void)sig;
	g_stop++;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s <node>[:port] --duration S --out FILE [options]\n"
		"       %s <node>[:port] --info   identity + health, read-only\n"
		"  <node>[:port]    node address, port default 8080\n"
		"  --duration S     seconds to record (from the first trigger edge)\n"
		"  --out FILE       H.265 file written on this machine; FILE.json next to it\n"
		"  --fps N          frame rate (default 30; the node says its range)\n"
		"  --session N      session number (default: a new random one, DL-20)\n"
		"  --fetch-s N      longest part per fetch, seconds (default and max 600)\n"
		"  -q               quiet: no progress lines\n",
		prog, prog);
}

static void sleep_ms(long ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

/* Explain a non-200 reply: the ICD error {code, message} if it is one. */
static void print_error(const char *what, long status, const struct ctl_buf *b)
{
	struct ctl_error e;

	if (b->len && ctl_dec_error(b->data, b->len, &e) == 0)
		fprintf(stderr, "fwt-rec: %s: HTTP %ld %s: %s\n", what, status, e.code, e.message);
	else
		fprintf(stderr, "fwt-rec: %s: HTTP %ld\n", what, status);
}

/* GET/POST + 200 check; reply in *b. */
static int call(struct ctl_http *h, const char *what, const char *path, const uint8_t *body,
		size_t len, int post, struct ctl_buf *b)
{
	long status = 0;

	if (ctl_http_call(h, path, body, len, post, b, &status) != 0) {
		fprintf(stderr, "fwt-rec: %s: %s\n", what, h->err);
		return -1;
	}
	if (status != 200) {
		print_error(what, status, b);
		return -1;
	}
	return 0;
}

static int get_health(struct ctl_http *h, struct ctl_buf *b, struct ctl_health *hl)
{
	if (call(h, "health", "/v1/health", NULL, 0, 0, b))
		return -1;
	if (ctl_dec_health(b->data, b->len, hl)) {
		fprintf(stderr, "fwt-rec: health: not an ICD v1 reply\n");
		return -1;
	}
	return 0;
}

static int disarm(struct ctl_http *h, struct ctl_buf *b)
{
	for (int k = 0; k < 3; k++) {
		if (call(h, "disarm", "/v1/disarm", NULL, 0, 1, b) == 0)
			return 0;
		sleep_ms(500);
	}
	return -1;
}

/* Find the session in /v1/sessions (paged, manifest order). 1 found, 0 not, -1 error. */
static int find_session(struct ctl_http *h, struct ctl_buf *b, uint32_t session,
			struct ctl_session *s)
{
	char path[64] = "/v1/sessions";

	for (int page = 0; page < 1000; page++) {
		int found;
		uint32_t last;
		bool more;

		if (call(h, "sessions", path, NULL, 0, 0, b) ||
		    ctl_dec_sessions_find(b->data, b->len, session, s, &found, &last, &more))
			return -1;
		if (found)
			return 1;
		if (!more)
			return 0;
		snprintf(path, sizeof(path), "/v1/sessions?after=%" PRIu32, last);
	}
	return 0;
}

/* All segments of the session (paged). Returns the count, or -1. */
static long list_segments(struct ctl_http *h, struct ctl_buf *b, uint32_t session,
			  struct ctl_segment *seg, size_t cap)
{
	size_t n = 0;
	char path[96];
	bool more = true;

	snprintf(path, sizeof(path), "/v1/segments?session=%" PRIu32, session);
	while (more && n < cap) {
		size_t got;

		if (call(h, "segments", path, NULL, 0, 0, b) ||
		    ctl_dec_segments(b->data, b->len, seg + n, cap - n, &got, &more))
			return -1;
		n += got;
		if (!got)
			break;
		snprintf(path, sizeof(path), "/v1/segments?session=%" PRIu32 "&from_idx=%" PRId64,
			 session, seg[n - 1].first_idx + 1);
	}
	return (long)n;
}

/* Shell-quote a path for popen. */
static void shq(char *dst, size_t cap, const char *s)
{
	size_t k = 0;

	if (cap < 3)
		return;
	dst[k++] = '\'';
	for (; *s && k + 5 < cap; s++) {
		if (*s == '\'') {
			memcpy(dst + k, "'\\''", 4);
			k += 4;
		} else {
			dst[k++] = *s;
		}
	}
	dst[k++] = '\'';
	dst[k] = '\0';
}

/* sei-dump: $FWT_SEI_DUMP, next to us in the build tree, or on PATH. */
static int find_sei_dump(char *path, size_t cap)
{
	const char *env = getenv("FWT_SEI_DUMP");
	char self[PATH_MAX];
	ssize_t n;

	if (env && access(env, X_OK) == 0) {
		snprintf(path, cap, "%s", env);
		return 0;
	}
	n = readlink("/proc/self/exe", self, sizeof(self) - 1);
	if (n > 0) {
		self[n] = '\0';
		snprintf(path, cap, "%s/../sei-dump/sei-dump", dirname(self));
		if (access(path, X_OK) == 0)
			return 0;
	}
	FILE *p = popen("command -v sei-dump 2>/dev/null", "r");

	if (p) {
		bool ok = fgets(path, (int)cap, p) != NULL;

		pclose(p);
		if (ok) {
			path[strcspn(path, "\n")] = '\0';
			return 0;
		}
	}
	return -1;
}

/* Run sei-dump; result "passed"/"failed"/"not run", its summary line. */
static void check_file(struct rec_result *r)
{
	char tool[PATH_MAX], qt[PATH_MAX + 16], qf[PATH_MAX + 16], cmd[3 * PATH_MAX];

	r->check = "not run";
	if (find_sei_dump(tool, sizeof(tool))) {
		snprintf(r->check_summary, sizeof(r->check_summary),
			 "sei-dump not found (laptop build, or set FWT_SEI_DUMP)");
		return;
	}
	shq(qt, sizeof(qt), tool);
	shq(qf, sizeof(qf), r->out_path);
	snprintf(cmd, sizeof(cmd), "%s -q --frames %" PRIu64 " %s 2>&1", qt, r->file_frames, qf);

	FILE *p = popen(cmd, "r");
	char line[512];

	if (!p)
		return;
	while (fgets(line, sizeof(line), p))
		if (!strncmp(line, "summary:", 8)) {
			line[strcspn(line, "\n")] = '\0';
			snprintf(r->check_summary, sizeof(r->check_summary), "%s", line);
		}
	int st = pclose(p);

	r->check = (st == 0) ? "passed" : "failed";
}

/* --info: one line per item, "key: value" (deploy.sh reads "version:"). */
static int print_info(struct ctl_http *h, struct ctl_buf *b, const struct ctl_identity *id)
{
	struct ctl_health hl;

	printf("version: %s\n", id->version);
	if (get_health(h, b, &hl))
		return 1;
	printf("state: %s%s%s\n", hl.state, hl.message[0] ? ": " : "", hl.message);
	printf("session: %" PRIu32 " (%s), frames %" PRIu64 ", missing %" PRIu64 "\n", hl.session,
	       strcmp(hl.state, "recording") ? "last" : "current", hl.frames, hl.missing);
	printf("ring: used %.1f MB, locked %.1f MB, free %.1f MB, depth %" PRIu64 " s\n",
	       hl.ring_used / 1e6, hl.ring_locked / 1e6, hl.ring_free / 1e6, hl.depth_s);
	printf("emmc: life A %" PRIu64 ", life B %" PRIu64 ", pre-EOL %" PRIu64
	       " (1 = 0-10 %% used / normal, 0 = unknown)\n", hl.emmc_life_a, hl.emmc_life_b,
	       hl.emmc_pre_eol);
	printf("temperature: %.1f C\n", hl.temp_mc / 1000.0);
	printf("ptp clock: %" PRId64 " ns, time %s\n", hl.phc_ns,
	       hl.time_verified ? "verified" : "not verified");
	return 0;
}

int main(int argc, char **argv)
{
	static const struct option lo[] = {
		{ "duration", required_argument, NULL, 'd' },
		{ "out", required_argument, NULL, 'o' },
		{ "fps", required_argument, NULL, 'f' },
		{ "session", required_argument, NULL, 's' },
		{ "fetch-s", required_argument, NULL, 'F' },
		{ "info", no_argument, NULL, 'I' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	double duration = 0;
	const char *out_path = NULL;
	unsigned long fps = 30, session = 0, fetch_s = MAX_FETCH_S;
	bool quiet = false, info = false;
	int c;

	while ((c = getopt_long(argc, argv, "qh", lo, NULL)) != -1) {
		switch (c) {
		case 'd': duration = atof(optarg); break;
		case 'o': out_path = optarg; break;
		case 'f': fps = strtoul(optarg, NULL, 0); break;
		case 's': session = strtoul(optarg, NULL, 0); break;
		case 'F': fetch_s = strtoul(optarg, NULL, 0); break;
		case 'q': quiet = true; break;
		case 'I': info = true; break;
		default: usage(argv[0]); return 2;
		}
	}
	if (optind != argc - 1 || (!info && (duration <= 0 || !out_path)) || fps == 0 ||
	    session > UINT32_MAX || fetch_s == 0 || fetch_s > MAX_FETCH_S) {
		usage(argv[0]);
		return 2;
	}

	struct rec_result r = { .tool_version = FWT_VERSION, .addr = argv[optind],
				.out_path = out_path, .requested_s = duration, .fps = (uint32_t)fps,
				.check = "not run" };
	struct ctl_http h;
	struct ctl_buf b = { 0 };
	struct ctl_health hl;
	char path[160];
	uint8_t req[64];
	size_t req_len;

	setvbuf(stdout, NULL, _IOLBF, 0);  /* progress in order with errors, also into a file */
	snprintf(r.missing_ranges, sizeof(r.missing_ranges), "none");
	if (ctl_http_init(&h, r.addr)) {
		fprintf(stderr, "fwt-rec: libcurl init failed\n");
		return 1;
	}

	/* 1. who, and is it free */
	if (call(&h, "identity", "/v1/identity", NULL, 0, 0, &b))
		return 1;
	if (ctl_dec_identity(b.data, b.len, &r.id)) {
		fprintf(stderr, "fwt-rec: %s does not answer as an fwtrial node (ICD v1)\n", h.base);
		return 1;
	}
	printf("node %s (%s), %ux%u, fps %u-%u\n", r.id.node, r.id.version, r.id.width,
	       r.id.height, r.id.fps_min, r.id.fps_max);
	if (info)
		return print_info(&h, &b, &r.id);
	if (fps < r.id.fps_min || fps > r.id.fps_max) {
		fprintf(stderr, "fwt-rec: --fps %lu outside the node's range %u-%u\n", fps,
			r.id.fps_min, r.id.fps_max);
		return 1;
	}
	if (get_health(&h, &b, &hl))
		return 1;
	if (!strcmp(hl.state, "recording")) {
		fprintf(stderr, "fwt-rec: node is recording session %" PRIu32 ": not touching it\n",
			hl.session);
		return 1;
	}

	/* 2. arm */
	if (!session) {
		FILE *u = fopen("/dev/urandom", "rb");
		uint32_t v = 0;

		if (!u || fread(&v, sizeof(v), 1, u) != 1) {
			fprintf(stderr, "fwt-rec: /dev/urandom\n");
			return 1;
		}
		fclose(u);
		session = v % 2147483647u + 1;
	}
	r.session = (uint32_t)session;
	if (ctl_enc_arm_req(r.session, r.fps, req, sizeof(req), &req_len))
		return 1;

	struct sigaction sa = { .sa_handler = on_signal };

	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	time_t now = time(NULL);

	strftime(r.started_utc, sizeof(r.started_utc), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
	if (call(&h, "arm", "/v1/arm", req, req_len, 1, &b) ||
	    ctl_dec_arm_rep(b.data, b.len, &r.arm))
		return 1;
	printf("armed session %" PRIu32 ", %lu fps, T0 %" PRId64 " (node PTP clock)%s\n",
	       r.session, fps, r.arm.t0, hl.time_verified ? "" : ", time not verified");

	/* 3. wait on the node's clock: T0 + duration */
	int64_t end = r.arm.t0 + (int64_t)(duration * 1e9);
	bool failed = false;

	while (!g_stop) {
		if (get_health(&h, &b, &hl)) {
			failed = true;
			break;
		}
		if (!strcmp(hl.state, "failed")) {
			fprintf(stderr, "fwt-rec: node failed: %s\n", hl.message);
			failed = true;
			break;
		}
		if (hl.phc_ns >= end)
			break;
		if (!quiet) {
			double t = (hl.phc_ns - r.arm.t0) / 1e9;

			if (t < 0)
				printf("  T0 in %.1f s\n", -t);
			else
				printf("  %5.1f / %g s  frames %" PRIu64 "  missing %" PRIu64 "\n", t,
				       duration, hl.frames, hl.missing);
			fflush(stdout);
		}
		int64_t left_ms = (end - hl.phc_ns) / 1000000;

		sleep_ms(left_ms < 2000 ? (left_ms > 10 ? left_ms : 10) : 2000);
	}
	r.interrupted = g_stop > 0;
	if (r.interrupted)
		printf("interrupted: disarming\n");

	/* 4. disarm, always */
	if (disarm(&h, &b)) {
		fprintf(stderr, "fwt-rec: DISARM FAILED: session %" PRIu32 " may still record on %s\n",
			r.session, h.base);
		return 1;
	}
	printf("disarmed\n");
	if (g_stop > 1)
		return 1;  /* second Ctrl-C: don't fetch */

	/* 5. the session as recorded (wait until the recorder has closed it) */
	int st = 0;

	for (int k = 0; k < 30; k++) {
		st = find_session(&h, &b, r.session, &r.rec);
		if (st < 0 || (st == 1 && strcmp(r.rec.status, "recording")))
			break;
		sleep_ms(500);
	}
	if (st != 1) {
		fprintf(stderr, "fwt-rec: session %" PRIu32 " not listed on the node%s\n", r.session,
			failed ? " (node failed before recording)" : "");
		return 1;
	}
	printf("recorded: %s, index %" PRId64 "..%" PRId64 ", frames %" PRIu64 ", missing %" PRIu64
	       "%s\n", r.rec.status, r.rec.first_idx, r.rec.last_idx, r.rec.frames, r.rec.missing,
	       r.rec.truncated ? ", TRUNCATED by the ring" : "");

	/* 6. fetch the whole session, segment-aligned parts of <= --fetch-s */
	struct ctl_segment *seg = calloc(MAX_SEGMENTS, sizeof(*seg));
	long nseg = seg ? list_segments(&h, &b, r.session, seg, MAX_SEGMENTS) : -1;
	FILE *out = nseg > 0 ? fopen(out_path, "wb") : NULL;

	if (nseg <= 0 || !out) {
		fprintf(stderr, "fwt-rec: %s\n", nseg == 0 ? "nothing recorded" :
			nseg < 0 ? "segment list failed" : strerror(errno));
		return 1;
	}
	int64_t max_frames = (int64_t)((double)fetch_s * 1e9 / (double)r.arm.period);
	char *mr = r.missing_ranges;

	mr[0] = '\0';
	for (long k = 0; k < nseg && !failed;) {
		long j = k;

		while (j + 1 < nseg && seg[j + 1].last_idx - seg[k].first_idx + 1 <= max_frames)
			j++;

		struct ctl_fetch_info fi;
		long status = 0;

		snprintf(path, sizeof(path), "/v1/fetch?session=%" PRIu32 "&from_idx=%" PRId64
			 "&to_idx=%" PRId64, r.session, seg[k].first_idx, seg[j].last_idx);
		if (ctl_http_fetch(&h, path, out, &fi, &b, &status)) {
			fprintf(stderr, "fwt-rec: fetch: %s\n", h.err);
			failed = true;
			break;
		}
		if (status != 200) {
			print_error("fetch", status, &b);
			failed = true;
			break;
		}
		if (fi.from != seg[k].first_idx)
			fprintf(stderr, "fwt-rec: WARNING: fetch started at %" PRId64 ", asked %" PRId64
				"\n", fi.from, seg[k].first_idx);
		r.fetches++;
		r.file_bytes += fi.bytes;
		r.missing_inside += fi.missing_inside;
		if (strcmp(fi.missing, "none")) {
			size_t used = strlen(mr);

			snprintf(mr + used, sizeof(r.missing_ranges) - used, "%s%s", used ? "," : "",
				 fi.missing);
		}
		for (long q = k; q <= j; q++)
			r.file_frames += seg[q].frames;
		if (!quiet)
			printf("  fetched %" PRId64 "..%" PRId64 ": %.1f MB\n", fi.from, fi.to,
			       fi.bytes / 1e6);
		k = j + 1;
	}
	if (!mr[0])
		snprintf(r.missing_ranges, sizeof(r.missing_ranges), "none");
	if (fclose(out) != 0)
		failed = true;
	free(seg);
	printf("file %s: %.1f MB, %" PRIu64 " frames, %u fetch(es), missing %s\n", out_path,
	       r.file_bytes / 1e6, r.file_frames, r.fetches, r.missing_ranges);

	/* 7. check, sidecar */
	if (!failed)
		check_file(&r);
	printf("check: %s%s%s\n", r.check, r.check_summary[0] ? ": " : "", r.check_summary);

	char jpath[PATH_MAX];

	snprintf(jpath, sizeof(jpath), "%s.json", out_path);
	FILE *j = fopen(jpath, "w");

	if (j) {
		rec_json_write(j, &r);
		fclose(j);
		printf("wrote %s\n", jpath);
	} else {
		fprintf(stderr, "fwt-rec: %s: %s\n", jpath, strerror(errno));
	}

	ctl_buf_free(&b);
	ctl_http_free(&h);
	bool ok = !failed && r.rec.missing == 0 && strcmp(r.check, "failed") != 0;

	printf("%s\n", ok ? "OK" : "PROBLEMS (see above)");
	return ok ? 0 : 1;
}
