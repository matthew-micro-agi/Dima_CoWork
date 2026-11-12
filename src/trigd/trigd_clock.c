#include "trigd_clock.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

/* Dynamic POSIX clock id for a PTP character device (Linux ptp docs). */
#define FD_TO_CLOCKID(fd) ((~(clockid_t)(fd) << 3) | 3)

int64_t trigd_ts_to_ns(const struct timespec *ts)
{
	return (int64_t)ts->tv_sec * 1000000000LL + ts->tv_nsec;
}

struct timespec trigd_ns_to_ts(int64_t ns)
{
	struct timespec ts;

	ts.tv_sec = ns / 1000000000LL;
	ts.tv_nsec = ns % 1000000000LL;
	if (ts.tv_nsec < 0) {
		ts.tv_nsec += 1000000000LL;
		ts.tv_sec -= 1;
	}
	return ts;
}

int trigd_clock_open(struct trigd_clock *c, const char *dev)
{
	c->fd = -1;
	c->is_phc = false;
	c->id = CLOCK_REALTIME;
	if (!dev)
		return 0;

	c->fd = open(dev, O_RDWR);
	if (c->fd < 0)
		return -errno;
	c->id = FD_TO_CLOCKID(c->fd);
	c->is_phc = true;
	return 0;
}

void trigd_clock_close(struct trigd_clock *c)
{
	if (c->fd >= 0)
		close(c->fd);
	c->fd = -1;
}

int trigd_clock_now(const struct trigd_clock *c, int64_t *ns)
{
	struct timespec ts;

	if (clock_gettime(c->id, &ts) != 0)
		return -errno;
	*ns = trigd_ts_to_ns(&ts);
	return 0;
}

int trigd_clock_mono_offset(const struct trigd_clock *c, int64_t *off_ns)
{
	struct timespec m1, m2;
	int64_t t = 0;

	clock_gettime(CLOCK_MONOTONIC, &m1);
	int r = trigd_clock_now(c, &t);
	clock_gettime(CLOCK_MONOTONIC, &m2);
	if (r)
		return r;

	int64_t mono_mid = (trigd_ts_to_ns(&m1) + trigd_ts_to_ns(&m2)) / 2;
	*off_ns = t - mono_mid;
	return 0;
}
