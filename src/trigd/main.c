/*
 * trigd: trigger daemon (v1, computed mode).
 *
 * Arms the CPTS periodic output so the hardware fires trigger edges on the PTP
 * clock at T0 + k*period, and publishes one message per edge
 * {session, index, t_ns, flags} to its subscribers over a Unix datagram socket.
 *
 * Decisions: DL-11 (socket, struct, subscription), DL-12 (plan, T0),
 * DL-13 (every edge flagged "time not verified" until the PTP lock check
 * exists). Measured mode (hardware edge timestamps) needs DL-09.
 */
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <libgen.h>

#include "edge_source.h"
#include "fwt_index.h"
#include "fwt_trig_msg.h"
#include "fwt_version.h"
#include "trigd_clock.h"
#include "trigd_perout.h"
#include "trigd_subs.h"
#include "trigd_t0.h"

#define ARM_MARGIN_NS 100000000LL  /* earliest edge we arm: now + 100 ms */

struct opts {
	const char *dev;
	bool        sys_clock;
	unsigned    perout;
	uint32_t    session;
	int64_t     period_ns;
	bool        t0_given;
	int64_t     t0_ns;
	int64_t     t0_offset_ns;
	int64_t     count;
	const char *sock_path;
	int64_t     test_skip;
	bool        verbose;
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
		"usage: %s [options]\n"
		"  --dev PATH          PTP device (default /dev/ptp0)\n"
		"  --clock phc|sys     schedule on the PTP clock (default) or the system\n"
		"                      clock (laptop test: no hardware, no periodic output)\n"
		"  --perout N          CPTS periodic output index (default 1)\n"
		"  --session N         session number (default 1)\n"
		"  --fps N             frame rate (default 30); period = 1e9/N ns\n"
		"  --period-ns N       period in ns (overrides --fps)\n"
		"  --t0 NS             T0 in ns on the clock (default: next whole second + offset)\n"
		"  --t0-offset-ms N    offset for the default T0 (default 1000)\n"
		"  --count N           stop after edge index N-1 (default 0 = run until signal)\n"
		"  --sock PATH         socket path (default %s)\n"
		"  --test-skip N       TEST ONLY: don't publish every Nth edge (gap detection test)\n"
		"  -v                  print every edge\n",
		prog, FWT_TRIGD_SOCK_DEFAULT);
}

static int parse_opts(int argc, char **argv, struct opts *o)
{
	static const struct option lo[] = {
		{ "dev", required_argument, NULL, 'd' },
		{ "clock", required_argument, NULL, 'c' },
		{ "perout", required_argument, NULL, 'p' },
		{ "session", required_argument, NULL, 's' },
		{ "fps", required_argument, NULL, 'f' },
		{ "period-ns", required_argument, NULL, 'P' },
		{ "t0", required_argument, NULL, 't' },
		{ "t0-offset-ms", required_argument, NULL, 'o' },
		{ "count", required_argument, NULL, 'n' },
		{ "sock", required_argument, NULL, 'S' },
		{ "test-skip", required_argument, NULL, 'k' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	int c;

	*o = (struct opts){ .dev = "/dev/ptp0", .perout = 1, .session = 1,
			    .period_ns = trigd_period_from_fps(30),
			    .t0_offset_ns = NSEC_PER_SEC,
			    .sock_path = FWT_TRIGD_SOCK_DEFAULT };

	while ((c = getopt_long(argc, argv, "vh", lo, NULL)) != -1) {
		switch (c) {
		case 'd': o->dev = optarg; break;
		case 'c':
			if (strcmp(optarg, "sys") == 0)
				o->sys_clock = true;
			else if (strcmp(optarg, "phc") != 0)
				return -1;
			break;
		case 'p': o->perout = (unsigned)strtoul(optarg, NULL, 0); break;
		case 's': o->session = (uint32_t)strtoul(optarg, NULL, 0); break;
		case 'f': o->period_ns = trigd_period_from_fps(atoi(optarg)); break;
		case 'P': o->period_ns = strtoll(optarg, NULL, 0); break;
		case 't': o->t0_ns = strtoll(optarg, NULL, 0); o->t0_given = true; break;
		case 'o': o->t0_offset_ns = strtoll(optarg, NULL, 0) * 1000000LL; break;
		case 'n': o->count = strtoll(optarg, NULL, 0); break;
		case 'S': o->sock_path = optarg; break;
		case 'k': o->test_skip = strtoll(optarg, NULL, 0); break;
		case 'v': o->verbose = true; break;
		default: return -1;
		}
	}
	if (o->period_ns <= 0 || o->count < 0 || o->test_skip < 0)
		return -1;
	return 0;
}

/* Create and bind the datagram socket (creates the parent directory). */
static int open_socket(const char *path)
{
	struct sockaddr_un a = { .sun_family = AF_UNIX };
	char dir[sizeof(a.sun_path)];

	if (strlen(path) >= sizeof(a.sun_path)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	strcpy(a.sun_path, path);
	strcpy(dir, path);
	mkdir(dirname(dir), 0755);  /* fine if it exists */
	unlink(path);

	int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

	if (fd < 0)
		return -1;
	if (bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
		int e = errno;

		close(fd);
		errno = e;
		return -1;
	}
	return fd;
}

/* Handle SUBSCRIBE / UNSUBSCRIBE until the socket is empty. */
static void handle_requests(int sfd, struct trigd_subs *subs)
{
	for (;;) {
		struct fwt_trig_msg m;
		struct sockaddr_un from;
		socklen_t flen = sizeof(from);
		ssize_t n = recvfrom(sfd, &m, sizeof(m), 0, (struct sockaddr *)&from, &flen);

		if (n < 0)
			return;  /* EAGAIN: empty */
		if (!fwt_trig_msg_valid(&m, n) || flen <= sizeof(sa_family_t)) {
			fprintf(stderr, "trigd: ignored invalid request (%zd bytes)\n", n);
			continue;
		}
		if (m.type == FWT_TRIG_SUBSCRIBE) {
			int r = trigd_subs_add(subs, &from, flen);

			fprintf(stderr, "trigd: subscribe -> %s (%d subscriber(s))\n",
				r > 0 ? "added" : r == 0 ? "already subscribed" : "REFUSED, list full",
				subs->n);
		} else if (m.type == FWT_TRIG_UNSUBSCRIBE) {
			bool r = trigd_subs_remove(subs, &from, flen);

			fprintf(stderr, "trigd: unsubscribe -> %s (%d subscriber(s))\n",
				r ? "removed" : "not subscribed", subs->n);
		}
	}
}

/* Send one message to every subscriber. Gone subscribers are removed. */
static void publish(int sfd, struct trigd_subs *subs, const struct fwt_trig_msg *m)
{
	for (int i = subs->n - 1; i >= 0; i--) {
		struct trigd_sub *s = &subs->s[i];

		if (sendto(sfd, m, sizeof(*m), MSG_DONTWAIT,
			   (struct sockaddr *)&s->addr, s->len) == (ssize_t)sizeof(*m)) {
			s->sent++;
		} else if (errno == EAGAIN || errno == ENOBUFS) {
			s->dropped++;  /* consumer too slow; it will see a gap */
		} else {
			fprintf(stderr, "trigd: subscriber gone (%s), removed\n", strerror(errno));
			trigd_subs_remove(subs, &s->addr, s->len);
		}
	}
}

int main(int argc, char **argv)
{
	struct opts o;
	struct trigd_clock clk;
	struct trigd_subs subs;
	struct edge_source *src = NULL;
	int sfd = -1, rc = 1;
	bool armed = false;
	int64_t now, published = 0, skipped = 0, last_index = -1;

	if (parse_opts(argc, argv, &o) != 0) {
		usage(argv[0]);
		return 2;
	}

	struct sigaction sa = { .sa_handler = on_signal };

	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);

	int r = trigd_clock_open(&clk, o.sys_clock ? NULL : o.dev);

	if (r) {
		fprintf(stderr, "trigd: cannot open %s: %s\n", o.dev, strerror(-r));
		return 1;
	}

	/* Clear a periodic output left running by an earlier run. */
	if (clk.is_phc)
		trigd_perout_disarm(clk.fd, o.perout);

	trigd_subs_init(&subs);
	sfd = open_socket(o.sock_path);
	if (sfd < 0) {
		fprintf(stderr, "trigd: socket %s: %s\n", o.sock_path, strerror(errno));
		goto out;
	}

	r = trigd_clock_now(&clk, &now);
	if (r) {
		fprintf(stderr, "trigd: reading the clock: %s\n", strerror(-r));
		goto out;
	}

	struct fwt_plan plan = {
		.session = o.session,
		.t0_ns = o.t0_given ? o.t0_ns : trigd_t0_auto(now, o.t0_offset_ns),
		.period_ns = o.period_ns,
	};
	int64_t first = trigd_first_index(plan.t0_ns, plan.period_ns, now, ARM_MARGIN_NS);

	if (o.count && first >= o.count) {
		fprintf(stderr, "trigd: T0 is too far in the past for --count %" PRId64 "\n", o.count);
		goto out;
	}

	fprintf(stderr,
		"trigd %s: session %" PRIu32 ", T0 %" PRId64 " ns, period %" PRId64 " ns, "
		"clock %s, mode computed, socket %s\n",
		FWT_VERSION, plan.session, plan.t0_ns, plan.period_ns,
		clk.is_phc ? o.dev : "system (test, no periodic output)", o.sock_path);
	if (first > 0)
		fprintf(stderr, "trigd: T0 already passed: late join at index %" PRId64 "\n", first);

	if (clk.is_phc) {
		r = trigd_perout_arm(clk.fd, o.perout, fwt_edge_time(&plan, first), plan.period_ns);
		if (r) {
			fprintf(stderr, "trigd: arming periodic output %u: %s\n", o.perout, strerror(-r));
			goto out;
		}
		armed = true;
	}

	src = edge_source_computed_new(&clk, &plan, first);
	if (!src) {
		fprintf(stderr, "trigd: edge source: %s\n", strerror(errno));
		goto out;
	}

	struct pollfd pfd[2] = {
		{ .fd = src->fd(src), .events = POLLIN },
		{ .fd = sfd, .events = POLLIN },
	};

	while (!g_stop) {
		if (poll(pfd, 2, -1) < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "trigd: poll: %s\n", strerror(errno));
			goto out;
		}
		if (pfd[1].revents & POLLIN)
			handle_requests(sfd, &subs);
		if (!(pfd[0].revents & POLLIN))
			continue;

		struct edge e[16];
		int n = src->ready(src, e, 16);

		if (n < 0) {
			fprintf(stderr, "trigd: edge source: %s\n", strerror(-n));
			goto out;
		}
		for (int i = 0; i < n && !g_stop; i++) {
			if (o.count && e[i].index >= o.count) {
				g_stop = 1;
				break;
			}
			last_index = e[i].index;
			if (o.test_skip && e[i].index % o.test_skip == o.test_skip - 1) {
				skipped++;
				continue;
			}

			struct fwt_trig_msg m;

			fwt_trig_msg_init(&m, FWT_TRIG_EDGE);
			m.session = plan.session;
			m.flags = e[i].flags;
			m.index = e[i].index;
			m.t_ns = e[i].t_ns;
			m.period_ns = plan.period_ns;
			publish(sfd, &subs, &m);
			published++;
			if (o.verbose)
				printf("edge session %" PRIu32 " index %" PRId64 " t %" PRId64
				       " flags 0x%x\n", m.session, m.index, m.t_ns, m.flags);
		}
	}
	rc = 0;

out:
	/* Always stop the hardware output: closing the device does not. */
	if (armed) {
		r = trigd_perout_disarm(clk.fd, o.perout);
		if (r)
			fprintf(stderr, "trigd: WARNING: disarm failed: %s\n", strerror(-r));
	}
	if (sfd >= 0) {
		struct fwt_trig_msg m;

		fwt_trig_msg_init(&m, FWT_TRIG_END);
		m.session = o.session;
		m.index = last_index;
		m.period_ns = o.period_ns;
		publish(sfd, &subs, &m);
		close(sfd);
		unlink(o.sock_path);
	}
	fprintf(stderr, "trigd: stopped. last index %" PRId64 ", published %" PRId64
		", test-skipped %" PRId64 ", output %s\n",
		last_index, published, skipped, armed ? "disarmed" : "not armed");
	for (int i = 0; i < subs.n; i++)
		fprintf(stderr, "trigd: subscriber %d: sent %" PRIu64 ", dropped %" PRIu64 "\n",
			i, subs.s[i].sent, subs.s[i].dropped);
	if (src)
		src->destroy(src);
	trigd_clock_close(&clk);
	return rc;
}
