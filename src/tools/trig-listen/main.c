/*
 * trig-listen: test listener for trigd. Subscribes, prints edges and checks
 * them: index continuity (gaps, duplicates) and edge time against the grid
 * T0 + index*period. Stops at trigd's END message, after --count edges, or on
 * Ctrl-C, and prints a summary. Exit code 0 only if no gap/duplicate was seen.
 *
 * Interval statistics (M4, trigger jitter; meaningful with hardware-measured
 * edges, trigd --edges measured/auto): for consecutive indices, interval -
 * period: min/max, RMS and a histogram.
 */
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "fwt_seqcheck.h"
#include "fwt_trig_msg.h"

static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
	(void)sig;
	g_stop = 1;
}

static int send_type(int fd, const struct sockaddr_un *to, uint16_t type)
{
	struct fwt_trig_msg m;

	fwt_trig_msg_init(&m, type);
	return sendto(fd, &m, sizeof(m), 0, (const struct sockaddr *)to, sizeof(*to)) ==
	       (ssize_t)sizeof(m) ? 0 : -1;
}

int main(int argc, char **argv)
{
	static const struct option lo[] = {
		{ "sock", required_argument, NULL, 'S' },
		{ "count", required_argument, NULL, 'n' },
		{ "quiet", no_argument, NULL, 'q' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	const char *sock_path = FWT_TRIGD_SOCK_DEFAULT;
	int64_t count = 0;
	bool quiet = false;
	int c;

	while ((c = getopt_long(argc, argv, "qh", lo, NULL)) != -1) {
		switch (c) {
		case 'S': sock_path = optarg; break;
		case 'n': count = strtoll(optarg, NULL, 0); break;
		case 'q': quiet = true; break;
		default:
			fprintf(stderr, "usage: %s [--sock PATH] [--count N] [-q]\n", argv[0]);
			return 2;
		}
	}

	struct sigaction sa = { .sa_handler = on_signal };

	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	struct sockaddr_un trigd = { .sun_family = AF_UNIX };

	if (strlen(sock_path) >= sizeof(trigd.sun_path)) {
		fprintf(stderr, "trig-listen: socket path too long\n");
		return 2;
	}
	strcpy(trigd.sun_path, sock_path);

	int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	struct sockaddr_un self = { .sun_family = AF_UNIX };

	/* Autobind: the kernel picks a unique abstract address for us. */
	if (fd < 0 || bind(fd, (struct sockaddr *)&self, sizeof(sa_family_t)) != 0) {
		fprintf(stderr, "trig-listen: socket: %s\n", strerror(errno));
		return 1;
	}
	if (send_type(fd, &trigd, FWT_TRIG_SUBSCRIBE) != 0) {
		fprintf(stderr, "trig-listen: subscribe to %s: %s (is trigd running?)\n",
			sock_path, strerror(errno));
		return 1;
	}
	fprintf(stderr, "trig-listen: subscribed to %s\n", sock_path);

	struct fwt_seqcheck chk;
	bool have_grid = false, ended = false;
	int64_t t0 = 0, period = 0, max_dev = 0, edges = 0, last_t = 0;
	double dev_sq = 0;
	/* intervals between consecutive edges: d = interval - period */
	static const int64_t bin_ns[] = { 2, 10, 100, 1000, 10000 };
	uint64_t bins[6] = { 0 }, intervals = 0;
	int64_t d_min = INT64_MAX, d_max = INT64_MIN, prev_index = -1, prev_t = 0;
	double d_sq = 0;
	uint32_t flags_or = 0, session = 0;

	fwt_seqcheck_init(&chk);

	while (!g_stop && !ended && (!count || edges < count)) {
		struct pollfd p = { .fd = fd, .events = POLLIN };

		if (poll(&p, 1, 1000) <= 0)
			continue;  /* timeout or EINTR: re-check stop */

		struct fwt_trig_msg m;
		ssize_t n = recv(fd, &m, sizeof(m), 0);

		if (n < 0 || !fwt_trig_msg_valid(&m, n)) {
			fprintf(stderr, "trig-listen: invalid message (%zd bytes)\n", n);
			continue;
		}
		if (m.type == FWT_TRIG_END) {
			fprintf(stderr, "trig-listen: trigd ended session %" PRIu32
				" at index %" PRId64 "\n", m.session, m.index);
			/* END carries the last index: catches a gap at the very end. */
			if (chk.have && m.session == chk.session && m.index > chk.last_index) {
				int64_t miss = m.index - chk.last_index;

				chk.gaps++;
				chk.missing += miss;
				printf("GAP: %" PRId64 " missing at the end (last index %" PRId64 ")\n",
				       miss, m.index);
			}
			ended = true;
			break;
		}
		if (m.type != FWT_TRIG_EDGE)
			continue;

		int64_t missing = 0;
		enum fwt_seq_result r = fwt_seqcheck_feed(&chk, m.session, m.index, &missing);

		if (r == FWT_SEQ_FIRST || r == FWT_SEQ_NEW_SESSION || !have_grid ||
		    m.period_ns != period) {
			/* (Re)derive the grid from this edge: T0 = t - index*period. */
			t0 = m.t_ns - m.index * m.period_ns;
			period = m.period_ns;
			have_grid = true;
		}
		int64_t dev = m.t_ns - (t0 + m.index * period);

		if (dev < 0)
			dev = -dev;
		if (dev > max_dev)
			max_dev = dev;
		dev_sq += (double)dev * (double)dev;

		if (prev_index >= 0 && m.index == prev_index + 1 && m.period_ns == period) {
			int64_t d = (m.t_ns - prev_t) - period, a = d < 0 ? -d : d;
			int b = 0;

			while (b < 5 && a > bin_ns[b])
				b++;
			bins[b]++;
			intervals++;
			d_sq += (double)d * (double)d;
			d_min = d < d_min ? d : d_min;
			d_max = d > d_max ? d : d_max;
		}
		prev_index = m.index;
		prev_t = m.t_ns;

		edges++;
		flags_or |= m.flags;
		session = m.session;
		last_t = m.t_ns;

		if (r == FWT_SEQ_GAP)
			printf("GAP: %" PRId64 " missing before index %" PRId64 "\n", missing, m.index);
		else if (r == FWT_SEQ_DUP || r == FWT_SEQ_BACKWARD)
			printf("%s: index %" PRId64 "\n", fwt_seq_result_str(r), m.index);
		if (!quiet)
			printf("edge session %" PRIu32 " index %" PRId64 " t %" PRId64
			       " flags 0x%x\n", m.session, m.index, m.t_ns, m.flags);
	}

	if (!ended)
		send_type(fd, &trigd, FWT_TRIG_UNSUBSCRIBE);
	close(fd);

	printf("summary: session %" PRIu32 ", edges %" PRId64 ", gaps %" PRId64
	       " (missing %" PRId64 "), duplicates %" PRId64 ", backward %" PRId64
	       ", max |t - grid| %" PRId64 " ns, last t %" PRId64 ", flags seen 0x%x%s%s\n",
	       session, edges, chk.gaps, chk.missing, chk.dups, chk.backward, max_dev,
	       last_t, flags_or,
	       (flags_or & FWT_TRIG_F_COMPUTED) ? " [computed]" : "",
	       (flags_or & FWT_TRIG_F_TIME_UNVERIFIED) ? " [time not verified]" : "");
	if (intervals) {
		printf("intervals: %" PRIu64 ", interval - period: min %" PRId64 " ns, max %" PRId64
		       " ns, RMS %.1f ns; |t - grid| RMS %.1f ns\n", intervals, d_min, d_max,
		       sqrt(d_sq / (double)intervals), edges ? sqrt(dev_sq / (double)edges) : 0.0);
		printf("histogram |interval - period|: <=2 ns %" PRIu64 ", <=10 ns %" PRIu64
		       ", <=100 ns %" PRIu64 ", <=1 us %" PRIu64 ", <=10 us %" PRIu64 ", >10 us %"
		       PRIu64 "\n", bins[0], bins[1], bins[2], bins[3], bins[4], bins[5]);
	}
	return (chk.gaps || chk.dups || chk.backward) ? 1 : 0;
}
