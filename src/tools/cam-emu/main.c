/*
 * cam-emu: camera emulator for tests (DL-26). Not part of the node bundle.
 *
 * Plays a sensor wired to the trigger: subscribes to trigd and, per edge,
 * delivers one frame at edge time + latency +- jitter, timestamped on
 * CLOCK_MONOTONIC with its own sequence counter, into a buffer the client
 * queued (protocol: fwt_cam_proto.h). No buffer queued -> the frame is lost,
 * the sequence still advances. Faults on request: drop, extra, late, sequence
 * jump. Ground truth: the true edge index is drawn into every frame (DL-17
 * stamp; FWT_CAM_STAMP_SPURIOUS for extra frames), and --truth writes one CSV
 * line per frame.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/un.h>
#include <unistd.h>

#include "camemu_core.h"
#include "fwt_cam_proto.h"
#include "fwt_clock.h"
#include "fwt_stamp.h"
#include "fwt_trig_msg.h"

#define RESUBSCRIBE_NS 250000000LL  /* no edge for 250 ms: subscribe again */

struct opts {
	const char *sock_path;
	const char *trigd_path;
	const char *dev;          /* NULL: system clock */
	struct ce_faults f;
	int         noise;
	uint64_t    seed;
	const char *truth_path;
	bool        once;
	bool        verbose;
};

struct emu {
	struct opts o;
	struct fwt_clock clk;
	struct ce_rng rng;
	struct ce_sched sched;
	struct ce_stream st;
	struct ce_bufq q;
	struct ce_pattern pat;
	struct fwt_stamp_geom geom;
	FILE *truth;

	int listen_fd, client_fd, trig_fd, timer_fd;
	struct sockaddr_un trigd;

	/* client state */
	uint32_t w, h, buf_size;
	int      nbufs;
	int      mem_fd;
	uint8_t *mem;

	/* trigger state */
	uint32_t session;
	int64_t  last_edge_mono, last_sub_mono;
	bool     ended;

	/* counters (whole run) */
	uint64_t edges, dropped, extra, late, not_streaming, send_fail, clients;
};

static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
	(void)sig;
	g_stop = 1;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [options]   (test tool, DL-26)\n"
		"  --sock PATH        listen socket (default %s)\n"
		"  --trigd PATH       trigd socket (default %s)\n"
		"  --clock phc|sys    trigd's clock: PTP device (default) or system clock\n"
		"  --dev PATH         PTP device (default /dev/ptp0)\n"
		"  --latency-ms X     edge -> frame timestamp (default 25)\n"
		"  --jitter-ms X      uniform +- jitter on the latency (default 0.5)\n"
		"  --drop PCT         edges without a frame (default 0)\n"
		"  --extra PCT        extra frames between edges (default 0)\n"
		"  --late PCT         frames 3/4 period late (default 0)\n"
		"  --seq-jump PCT     sequence skips 1-3 without a lost frame (default 0)\n"
		"  --noise N          noise amplitude, Y levels 0-100 (default 12)\n"
		"  --seed N           random seed (default 1)\n"
		"  --truth FILE       CSV per frame: sequence,t_mono_ns,index,kind\n"
		"  --once             exit when trigd ends its session\n"
		"  -v                 print every frame\n",
		prog, FWT_CAMEMU_SOCK_DEFAULT, FWT_TRIGD_SOCK_DEFAULT);
}

static int parse_opts(int argc, char **argv, struct opts *o)
{
	static const struct option lo[] = {
		{ "sock", required_argument, NULL, 'S' },
		{ "trigd", required_argument, NULL, 'T' },
		{ "clock", required_argument, NULL, 'c' },
		{ "dev", required_argument, NULL, 'd' },
		{ "latency-ms", required_argument, NULL, 'l' },
		{ "jitter-ms", required_argument, NULL, 'j' },
		{ "drop", required_argument, NULL, 'D' },
		{ "extra", required_argument, NULL, 'E' },
		{ "late", required_argument, NULL, 'L' },
		{ "seq-jump", required_argument, NULL, 'J' },
		{ "noise", required_argument, NULL, 'N' },
		{ "seed", required_argument, NULL, 'r' },
		{ "truth", required_argument, NULL, 't' },
		{ "once", no_argument, NULL, '1' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	const char *dev = "/dev/ptp0";
	bool sys = false;
	int c;

	*o = (struct opts){ .sock_path = FWT_CAMEMU_SOCK_DEFAULT,
			    .trigd_path = FWT_TRIGD_SOCK_DEFAULT,
			    .f = { .latency_ns = 25000000, .jitter_ns = 500000 },
			    .noise = 12, .seed = 1 };

	while ((c = getopt_long(argc, argv, "vh", lo, NULL)) != -1) {
		switch (c) {
		case 'S': o->sock_path = optarg; break;
		case 'T': o->trigd_path = optarg; break;
		case 'c':
			if (strcmp(optarg, "sys") == 0)
				sys = true;
			else if (strcmp(optarg, "phc") != 0)
				return -1;
			break;
		case 'd': dev = optarg; break;
		case 'l': o->f.latency_ns = (int64_t)(atof(optarg) * 1e6); break;
		case 'j': o->f.jitter_ns = (int64_t)(atof(optarg) * 1e6); break;
		case 'D': o->f.drop_pct = atof(optarg); break;
		case 'E': o->f.extra_pct = atof(optarg); break;
		case 'L': o->f.late_pct = atof(optarg); break;
		case 'J': o->f.seqjump_pct = atof(optarg); break;
		case 'N': o->noise = atoi(optarg); break;
		case 'r': o->seed = strtoull(optarg, NULL, 0); break;
		case 't': o->truth_path = optarg; break;
		case '1': o->once = true; break;
		case 'v': o->verbose = true; break;
		default: return -1;
		}
	}
	o->dev = sys ? NULL : dev;
	if (o->f.latency_ns < 0 || o->f.jitter_ns < 0 || o->noise < 0 || o->noise > 100)
		return -1;
	return 0;
}

/* ---- sockets ---- */

static int listen_on(const char *path)
{
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	char dir[sizeof(a.sun_path)];

	if (strlen(path) >= sizeof(a.sun_path))
		return -ENAMETOOLONG;
	strcpy(a.sun_path, path);
	strcpy(dir, path);
	char *slash = strrchr(dir, '/');

	if (slash && slash != dir) {
		*slash = '\0';
		mkdir(dir, 0755);  /* /run/fwtrial may not exist yet */
	}
	unlink(path);

	int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);

	if (fd < 0)
		return -errno;
	if (bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(fd, 2) != 0) {
		int e = -errno;

		close(fd);
		return e;
	}
	return fd;
}

static int trig_socket(const char *path, struct sockaddr_un *trigd)
{
	struct sockaddr_un self = { .sun_family = AF_UNIX };

	*trigd = (struct sockaddr_un){ .sun_family = AF_UNIX };
	if (strlen(path) >= sizeof(trigd->sun_path))
		return -ENAMETOOLONG;
	strcpy(trigd->sun_path, path);

	int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);

	/* Autobind: the kernel picks a unique abstract address. */
	if (fd < 0 || bind(fd, (struct sockaddr *)&self, sizeof(sa_family_t)) != 0)
		return -errno;
	return fd;
}

static void trig_send(struct emu *e, uint16_t type)
{
	struct fwt_trig_msg m;

	fwt_trig_msg_init(&m, type);
	/* fails while trigd isn't running: retried */
	sendto(e->trig_fd, &m, sizeof(m), 0, (struct sockaddr *)&e->trigd, sizeof(e->trigd));
}

static void arm_timer(struct emu *e)
{
	int64_t next = ce_sched_next(&e->sched);
	struct itimerspec it = { 0 };

	if (next != INT64_MAX) {
		if (next <= 0)
			next = 1;  /* 0 would disarm */
		it.it_value.tv_sec = next / 1000000000LL;
		it.it_value.tv_nsec = next % 1000000000LL;
	}
	timerfd_settime(e->timer_fd, TFD_TIMER_ABSTIME, &it, NULL);
}

/* ---- client ---- */

static void free_bufs(struct emu *e)
{
	if (e->mem)
		munmap(e->mem, (size_t)e->nbufs * e->buf_size);
	if (e->mem_fd >= 0)
		close(e->mem_fd);
	e->mem = NULL;
	e->mem_fd = -1;
	e->nbufs = 0;
	ce_bufq_init(&e->q, 0);
}

static void drop_client(struct emu *e, const char *why)
{
	if (e->client_fd < 0)
		return;
	fprintf(stderr, "cam-emu: client gone (%s)\n", why);
	close(e->client_fd);
	e->client_fd = -1;
	e->st.on = false;
	free_bufs(e);
	ce_pattern_free(&e->pat);
	e->w = e->h = 0;
}

static void reply(struct emu *e, uint16_t type, int status, const struct fwt_cam_msg *extra,
		  int pass_fd)
{
	struct fwt_cam_msg r;

	if (extra)
		r = *extra;
	else
		fwt_cam_msg_init(&r, type);
	r.magic = FWT_CAM_MAGIC;
	r.version = FWT_CAM_VERSION;
	r.type = type;
	r.status = status;
	int rc = fwt_cam_send(e->client_fd, &r, pass_fd);

	if (rc)
		fprintf(stderr, "cam-emu: reply %s: %s\n", fwt_cam_type_str(type), strerror(-rc));
}

static int do_format(struct emu *e, const struct fwt_cam_msg *m)
{
	if (e->nbufs)
		return -EBUSY;
	if (m->fourcc != FWT_CAM_FOURCC_NV12)
		return -EINVAL;
	if (m->width < 64 || m->height < 64 || m->width > 8192 || m->height > 8192 ||
	    (m->width & 7) || (m->height & 7))
		return -EINVAL;
	ce_pattern_free(&e->pat);
	int r = ce_pattern_init(&e->pat, (int)m->width, (int)m->height, e->o.noise, &e->rng);

	if (r)
		return r;
	if (!fwt_stamp_geom_default((int)m->width, (int)m->height, &e->geom)) {
		ce_pattern_free(&e->pat);
		return -EINVAL;
	}
	e->w = m->width;
	e->h = m->height;
	e->buf_size = fwt_cam_buf_size(e->w, e->h);
	fprintf(stderr, "cam-emu: format %ux%u NV12, buffer %u bytes\n", e->w, e->h, e->buf_size);
	return 0;
}

static void do_reqbufs(struct emu *e, const struct fwt_cam_msg *m)
{
	struct fwt_cam_msg r;
	int err = 0;

	fwt_cam_msg_init(&r, FWT_CAM_REQBUFS);
	if (e->st.on)
		err = -EBUSY;
	else if (!e->w)
		err = -EINVAL;  /* no format yet */
	else if (m->count > FWT_CAM_MAX_BUFS)
		err = -EINVAL;
	if (err) {
		reply(e, FWT_CAM_REQBUFS, err, NULL, -1);
		return;
	}
	free_bufs(e);
	if (m->count == 0) {
		reply(e, FWT_CAM_REQBUFS, 0, NULL, -1);
		return;
	}

	size_t len = (size_t)m->count * e->buf_size;

	e->mem_fd = memfd_create("cam-emu-bufs", MFD_CLOEXEC);
	if (e->mem_fd < 0 || ftruncate(e->mem_fd, (off_t)len) != 0) {
		err = -errno;
		free_bufs(e);
		reply(e, FWT_CAM_REQBUFS, err, NULL, -1);
		return;
	}
	e->mem = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, e->mem_fd, 0);
	if (e->mem == MAP_FAILED) {
		err = -errno;
		e->mem = NULL;
		free_bufs(e);
		reply(e, FWT_CAM_REQBUFS, err, NULL, -1);
		return;
	}
	e->nbufs = (int)m->count;
	ce_bufq_init(&e->q, e->nbufs);
	r.count = m->count;
	r.buf_size = e->buf_size;
	fprintf(stderr, "cam-emu: %u buffers (%zu bytes shared)\n", m->count, len);
	reply(e, FWT_CAM_REQBUFS, 0, &r, e->mem_fd);
}

static void handle_client(struct emu *e)
{
	struct fwt_cam_msg m;
	int r = fwt_cam_recv(e->client_fd, &m, NULL);

	if (r == -EAGAIN || r == -EINTR)
		return;
	if (r == 0) {
		drop_client(e, "closed");
		return;
	}
	if (r < 0) {
		drop_client(e, r == -EBADMSG ? "bad message" : strerror(-r));
		return;
	}
	switch (m.type) {
	case FWT_CAM_FORMAT:
		reply(e, FWT_CAM_FORMAT, do_format(e, &m), NULL, -1);
		break;
	case FWT_CAM_REQBUFS:
		do_reqbufs(e, &m);
		break;
	case FWT_CAM_QBUF: {
		int q = ce_bufq_queue(&e->q, (int)m.slot);

		if (q) {
			struct fwt_cam_msg er;

			fwt_cam_msg_init(&er, FWT_CAM_ERROR);
			er.slot = m.slot;
			reply(e, FWT_CAM_ERROR, q, &er, -1);
		}
		break;
	}
	case FWT_CAM_STREAMON:
		if (!e->nbufs) {
			reply(e, FWT_CAM_STREAMON, -EINVAL, NULL, -1);
			break;
		}
		ce_stream_on(&e->st);
		fprintf(stderr, "cam-emu: stream on\n");
		reply(e, FWT_CAM_STREAMON, 0, NULL, -1);
		break;
	case FWT_CAM_STREAMOFF:
		e->st.on = false;
		ce_bufq_clear(&e->q);
		fprintf(stderr, "cam-emu: stream off\n");
		reply(e, FWT_CAM_STREAMOFF, 0, NULL, -1);
		break;
	default:
		drop_client(e, "unexpected message type");
		break;
	}
}

static void accept_client(struct emu *e)
{
	int fd = accept4(e->listen_fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);

	if (fd < 0)
		return;
	if (e->client_fd >= 0) {
		fprintf(stderr, "cam-emu: second client refused (one camera, one client)\n");
		close(fd);
		return;
	}
	e->client_fd = fd;
	e->clients++;
	fprintf(stderr, "cam-emu: client connected\n");
}

/* ---- frames ---- */

static void truth_line(struct emu *e, const char *seq, int64_t t, int64_t index, const char *kind)
{
	if (e->truth)
		fprintf(e->truth, "%s,%" PRId64 ",%" PRId64 ",%s\n", seq, t, index, kind);
}

static void deliver(struct emu *e, const struct ce_event *ev)
{
	char seqs[16];

	if (e->client_fd < 0 || !e->st.on) {
		e->not_streaming++;  /* sensor not started: no frame, no sequence */
		return;
	}
	uint32_t seq;
	int slot = ce_deliver(&e->st, &e->q, &e->o.f, &e->rng, &seq);

	snprintf(seqs, sizeof(seqs), "%" PRIu32, seq);
	if (slot < 0) {
		truth_line(e, seqs, ev->due_ns, ev->index, "lost_nobuf");
		if (e->o.verbose)
			fprintf(stderr, "cam-emu: seq %u index %" PRId64 " lost: no buffer\n",
				seq, ev->index);
		return;
	}

	uint8_t *buf = e->mem + (size_t)slot * e->buf_size;
	uint32_t stamp = ev->kind == CE_SPURIOUS ? FWT_CAM_STAMP_SPURIOUS : (uint32_t)ev->index;

	ce_pattern_fill(&e->pat, buf, &e->rng);
	fwt_stamp_draw(buf, (int)e->w, buf + (size_t)e->w * e->h, (int)e->w, (int)e->w,
		       (int)e->h, &e->geom, stamp);

	struct fwt_cam_msg m;

	fwt_cam_msg_init(&m, FWT_CAM_FILLED);
	m.slot = (uint32_t)slot;
	m.sequence = seq;
	m.bytesused = fwt_cam_nv12_size(e->w, e->h);
	m.t_mono_ns = ev->due_ns;
	int r = fwt_cam_send(e->client_fd, &m, -1);

	if (r) {
		/* client not reading: the buffer stays ours, the frame is gone */
		e->send_fail++;
		e->st.delivered--;
		ce_bufq_queue(&e->q, slot);
		truth_line(e, seqs, ev->due_ns, ev->index, "lost_send");
		if (r != -EAGAIN)
			drop_client(e, strerror(-r));
		return;
	}
	truth_line(e, seqs, ev->due_ns, ev->index, ce_kind_str(ev->kind));
	if (e->o.verbose)
		fprintf(stderr, "cam-emu: seq %u slot %d index %" PRId64 " %s t %" PRId64 "\n",
			seq, slot, ev->index, ce_kind_str(ev->kind), ev->due_ns);
}

static void run_due(struct emu *e)
{
	uint64_t ticks;
	struct ce_event ev;

	if (read(e->timer_fd, &ticks, sizeof(ticks)) < 0 && errno != EAGAIN)
		fprintf(stderr, "cam-emu: timer: %s\n", strerror(errno));
	while (ce_sched_pop_due(&e->sched, fwt_mono_now_ns(), &ev))
		deliver(e, &ev);
}

static void on_edge(struct emu *e, const struct fwt_trig_msg *m)
{
	int64_t off = 0;
	int r = fwt_clock_mono_offset(&e->clk, &off);

	if (r) {
		fprintf(stderr, "cam-emu: clock: %s\n", strerror(-r));
		return;
	}
	if (m->session != e->session) {
		fprintf(stderr, "cam-emu: session %u, period %" PRId64 " ns\n", m->session,
			m->period_ns);
		e->session = m->session;
	}

	int64_t edge_mono = m->t_ns - off;
	struct ce_event ev[2];
	bool dropped;
	int n = ce_plan_edge(&e->o.f, &e->rng, edge_mono, m->index, m->period_ns, ev, &dropped);

	e->edges++;
	if (dropped) {
		e->dropped++;
		truth_line(e, "", edge_mono, m->index, "dropped");
	}
	for (int k = 0; k < n; k++) {
		if (ev[k].kind == CE_SPURIOUS)
			e->extra++;
		else if (ev[k].kind == CE_LATE)
			e->late++;
		ce_sched_add(&e->sched, &ev[k]);
	}
	e->last_edge_mono = fwt_mono_now_ns();
}

static void handle_trig(struct emu *e)
{
	struct fwt_trig_msg m;
	ssize_t n;

	while ((n = recv(e->trig_fd, &m, sizeof(m), 0)) > 0) {
		if (!fwt_trig_msg_valid(&m, n))
			continue;
		if (m.type == FWT_TRIG_EDGE) {
			e->ended = false;
			on_edge(e, &m);
		} else if (m.type == FWT_TRIG_END) {
			fprintf(stderr, "cam-emu: trigd ended session %u at index %" PRId64 "\n",
				m.session, m.index);
			e->ended = true;
			e->last_edge_mono = 0;
		}
	}
}

int main(int argc, char **argv)
{
	struct emu e = { .listen_fd = -1, .client_fd = -1, .trig_fd = -1, .timer_fd = -1,
			 .mem_fd = -1 };
	int r;

	if (parse_opts(argc, argv, &e.o) != 0) {
		usage(argv[0]);
		return 2;
	}

	struct sigaction sa = { .sa_handler = on_signal };

	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	if ((r = fwt_clock_open(&e.clk, e.o.dev)) != 0) {
		fprintf(stderr, "cam-emu: open %s: %s\n", e.o.dev, strerror(-r));
		return 1;
	}
	ce_rng_seed(&e.rng, e.o.seed);
	ce_sched_init(&e.sched);
	ce_bufq_init(&e.q, 0);
	if (e.o.truth_path) {
		e.truth = fopen(e.o.truth_path, "w");
		if (!e.truth) {
			fprintf(stderr, "cam-emu: %s: %s\n", e.o.truth_path, strerror(errno));
			return 1;
		}
		setvbuf(e.truth, NULL, _IOLBF, 0);
		fprintf(e.truth, "sequence,t_mono_ns,index,kind\n");
	}
	e.listen_fd = listen_on(e.o.sock_path);
	if (e.listen_fd < 0) {
		fprintf(stderr, "cam-emu: listen %s: %s\n", e.o.sock_path, strerror(-e.listen_fd));
		return 1;
	}
	e.trig_fd = trig_socket(e.o.trigd_path, &e.trigd);
	e.timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	if (e.trig_fd < 0 || e.timer_fd < 0) {
		fprintf(stderr, "cam-emu: socket/timer: %s\n", strerror(errno));
		return 1;
	}
	fprintf(stderr, "cam-emu: listening on %s, trigger from %s (%s clock), latency %.2f "
		"+- %.2f ms, drop %g%%, extra %g%%, late %g%%, seq-jump %g%%, noise %d, seed %"
		PRIu64 "\n", e.o.sock_path, e.o.trigd_path, e.o.dev ? e.o.dev : "system",
		e.o.f.latency_ns / 1e6, e.o.f.jitter_ns / 1e6, e.o.f.drop_pct, e.o.f.extra_pct,
		e.o.f.late_pct, e.o.f.seqjump_pct, e.o.noise, e.o.seed);

	while (!g_stop) {
		int64_t now = fwt_mono_now_ns();

		if (e.ended && e.o.once && !e.sched.n)
			break;
		/* Subscribe while no edges come: trigd may start later or restart
		 * (trigd ignores a second SUBSCRIBE from the same address). */
		if (now - e.last_edge_mono > RESUBSCRIBE_NS &&
		    now - e.last_sub_mono > RESUBSCRIBE_NS && !(e.ended && e.o.once)) {
			trig_send(&e, FWT_TRIG_SUBSCRIBE);
			e.last_sub_mono = now;
		}
		arm_timer(&e);

		struct pollfd p[4] = {
			{ .fd = e.listen_fd, .events = POLLIN },
			{ .fd = e.trig_fd, .events = POLLIN },
			{ .fd = e.timer_fd, .events = POLLIN },
			{ .fd = e.client_fd, .events = POLLIN },
		};

		if (poll(p, e.client_fd >= 0 ? 4 : 3, 200) < 0)
			continue;  /* EINTR: re-check stop */
		if (p[2].revents & POLLIN)
			run_due(&e);
		if (p[1].revents & POLLIN)
			handle_trig(&e);
		if (p[0].revents & POLLIN)
			accept_client(&e);
		if (e.client_fd >= 0 && (p[3].revents & (POLLIN | POLLHUP | POLLERR)))
			handle_client(&e);
	}

	if (!e.ended)
		trig_send(&e, FWT_TRIG_UNSUBSCRIBE);
	drop_client(&e, "emulator stopping");
	close(e.listen_fd);
	unlink(e.o.sock_path);
	close(e.trig_fd);
	close(e.timer_fd);
	fwt_clock_close(&e.clk);
	if (e.truth)
		fclose(e.truth);

	/* planned: decided per edge; delivered/lost: what reached a streaming
	 * client (--truth has the per-frame detail) */
	printf("summary: edges %" PRIu64 ", planned: dropped %" PRIu64 " extra %" PRIu64
	       " late %" PRIu64 "; delivered %" PRIu64 ", seq_jumps %" PRIu64 ", lost_nobuf %"
	       PRIu64 ", lost_send %" PRIu64 ", not_streaming %" PRIu64 ", clients %" PRIu64
	       ", sched_overflow %" PRIu64 "\n",
	       e.edges, e.dropped, e.extra, e.late, e.st.delivered, e.st.seq_jumps,
	       e.st.lost_nobuf, e.send_fail, e.not_streaming, e.clients, e.sched.overflow);
	return 0;
}
