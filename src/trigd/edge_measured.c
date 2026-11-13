/*
 * Measured edge source (DL-09, gap G1): the CPTS timestamps every trigger
 * edge in hardware (periodic output looped into a timestamp input by the
 * timesync router, device-tree overlay, setup.md section 7); trigd reads the
 * kernel's event queue on /dev/ptpN and announces each edge with its captured
 * time. Bookkeeping in edge_track.c.
 *
 * One fd for trigd's loop: an epoll set over the PTP device (events) and a
 * timer (edges overdue without an event, or the computed schedule after a
 * fallback). Every wake-up drains the queued events first, then handles
 * overdue edges, so a late wake-up never reports a false miss.
 *
 * auto:      no timestamp input, or no event for the first PROBE_EDGES edges
 *            -> WARNING, fall back to computed times for the rest of the session
 * measured:  never falls back; edges without an event carry TS_MISSING
 */
#include <errno.h>
#include <inttypes.h>
#include <linux/ptp_clock.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "edge_source.h"
#include "edge_track.h"
#include "fwt_trig_msg.h"

#define PROBE_EDGES  3          /* auto: this many edges without any event -> fallback */
#define WARN_MISSING 3          /* warn once at this many consecutive missing */
#define WAKE_LAG_NS  1000000LL  /* computed fallback: wake 1 ms after an edge */
#define EXTRA_MARGIN_NS 5000000LL

struct measured {
	struct edge_source base;
	const struct trigd_clock *clk;
	unsigned extts;
	bool auto_mode, fallback, warned, extts_on;
	int epfd, tfd;
	int64_t margin_ns;        /* overdue after edge time + this */
	struct edge_track track;
};

static int m_fd(struct edge_source *src)
{
	return ((struct measured *)src)->epfd;
}

static int extts_enable(struct measured *m, bool on)
{
	struct ptp_extts_request req = { .index = m->extts, .flags = on ? PTP_ENABLE_FEATURE : 0 };

	if (ioctl(m->clk->fd, PTP_EXTTS_REQUEST, &req) != 0)
		return -errno;
	m->extts_on = on;
	return 0;
}

/* Wake at the next decision: the next edge + margin (measured), or + 1 ms (fallback). */
static int arm_timer(struct measured *m)
{
	int64_t off;
	int r = trigd_clock_mono_offset(m->clk, &off);

	if (r)
		return r;

	int64_t at = fwt_edge_time(&m->track.plan, m->track.next) +
		     (m->fallback ? WAKE_LAG_NS : m->margin_ns + 1);
	struct itimerspec its = { .it_value = trigd_ns_to_ts(at - off) };

	if (its.it_value.tv_sec == 0 && its.it_value.tv_nsec == 0)
		its.it_value.tv_nsec = 1;
	return timerfd_settime(m->tfd, TFD_TIMER_ABSTIME, &its, NULL) ? -errno : 0;
}

static void fall_back(struct measured *m, const char *why)
{
	fprintf(stderr, "trigd: WARNING: %s; using computed edge times from index %" PRId64
		" (hardware timestamps need the CPTS loop overlay, setup.md section 7)\n", why,
		m->track.next);
	if (m->extts_on)
		extts_enable(m, false);
	m->fallback = true;
	m->base.name = "computed (fallback)";
}

/* Queued hardware events, without blocking. */
static int drain_events(struct measured *m, struct edge *out, int max)
{
	int n = 0;

	while (m->extts_on && n < max) {
		struct pollfd p = { .fd = m->clk->fd, .events = POLLIN };

		if (poll(&p, 1, 0) <= 0 || !(p.revents & POLLIN))
			break;

		struct ptp_extts_event ev[8];
		ssize_t got = read(m->clk->fd, ev, sizeof(ev));

		if (got < 0)
			return errno == EAGAIN || errno == EINTR ? n : -errno;
		for (size_t i = 0; i < (size_t)got / sizeof(ev[0]); i++) {
			if (ev[i].index != m->extts)
				continue;
			int64_t t = (int64_t)ev[i].t.sec * 1000000000LL + ev[i].t.nsec;

			n += edge_track_event(&m->track, t, out + n, max - n);
			if (n >= max)
				break;
		}
	}
	return n;
}

static int m_ready(struct edge_source *src, struct edge *out, int max)
{
	struct measured *m = (struct measured *)src;
	uint64_t expirations;
	int64_t now;
	int n, r;

	if (read(m->tfd, &expirations, sizeof(expirations)) < 0 && errno != EAGAIN)
		return -errno;
	r = trigd_clock_now(m->clk, &now);
	if (r)
		return r;

	if (m->fallback) {  /* computed: every edge whose time has passed */
		n = 0;
		while (n < max && fwt_edge_time(&m->track.plan, m->track.next) <= now) {
			out[n].index = m->track.next;
			out[n].t_ns = fwt_edge_time(&m->track.plan, m->track.next);
			out[n].flags = FWT_TRIG_F_COMPUTED | FWT_TRIG_F_TIME_UNVERIFIED;
			m->track.next++;
			n++;
		}
	} else {
		/* events first (a late wake-up finds them queued), then overdue edges */
		n = drain_events(m, out, max);
		if (n < 0)
			return n;
		n += edge_track_overdue(&m->track, now, m->margin_ns, out + n, max - n);

		if (m->track.consecutive_missing >= WARN_MISSING) {
			if (m->auto_mode && m->track.measured == 0 &&
			    m->track.missing >= PROBE_EDGES) {
				fall_back(m, "no hardware timestamp event for the first edges");
			} else if (!m->warned) {
				fprintf(stderr, "trigd: WARNING: %" PRId64 " edges in a row without a "
					"hardware timestamp (announced with TS_MISSING)\n",
					m->track.consecutive_missing);
				m->warned = true;
			}
		}
		if (m->track.consecutive_missing == 0)
			m->warned = false;
	}
	r = arm_timer(m);
	return r ? r : n;
}

static void m_destroy(struct edge_source *src)
{
	struct measured *m = (struct measured *)src;

	if (m->extts_on)
		extts_enable(m, false);
	if (m->tfd >= 0)
		close(m->tfd);
	if (m->epfd >= 0)
		close(m->epfd);
	free(m);
}

uint64_t edge_source_measured_ignored(struct edge_source *src)
{
	return ((struct measured *)src)->track.ignored;
}

struct edge_source *edge_source_measured_new(const struct trigd_clock *clk,
					     const struct fwt_plan *plan, int64_t first_index,
					     unsigned extts_index, bool auto_mode)
{
	struct measured *m = calloc(1, sizeof(*m));
	struct epoll_event ev = { .events = EPOLLIN };

	if (!m)
		return NULL;
	m->base = (struct edge_source){ .name = "measured", .fd = m_fd, .ready = m_ready,
					.destroy = m_destroy };
	m->clk = clk;
	m->extts = extts_index;
	m->auto_mode = auto_mode;
	m->epfd = m->tfd = -1;
	m->margin_ns = plan->period_ns + EXTRA_MARGIN_NS;
	edge_track_init(&m->track, plan, first_index, 0);

	m->epfd = epoll_create1(EPOLL_CLOEXEC);
	m->tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
	if (m->epfd < 0 || m->tfd < 0 || epoll_ctl(m->epfd, EPOLL_CTL_ADD, m->tfd, &ev) != 0)
		goto fail;

	int r = clk->is_phc ? extts_enable(m, true) : -ENODEV;

	if (r == 0) {
		if (epoll_ctl(m->epfd, EPOLL_CTL_ADD, clk->fd, &ev) != 0)
			goto fail;
	} else if (auto_mode) {
		char why[96];

		snprintf(why, sizeof(why), "timestamp input %u not available (%s)", extts_index,
			 strerror(-r));
		fall_back(m, why);
	} else {
		errno = -r;
		goto fail;
	}
	if (arm_timer(m) != 0)
		goto fail;
	return &m->base;

fail:;
	int e = errno ? errno : EINVAL;

	m_destroy(&m->base);
	errno = e;
	return NULL;
}
