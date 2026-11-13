#include "fwt_clock.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

/* Dynamic POSIX clock id for a PTP character device (Linux ptp docs). */
#define FD_TO_CLOCKID(fd) ((~(clockid_t)(fd) << 3) | 3)

#define OFFSET_READS 5

static int64_t ts_ns(const struct timespec *ts)
{
	return (int64_t)ts->tv_sec * 1000000000LL + ts->tv_nsec;
}

int fwt_clock_open(struct fwt_clock *c, const char *dev)
{
	c->fd = -1;
	c->id = CLOCK_REALTIME;
	if (!dev)
		return 0;
	c->fd = open(dev, O_RDONLY | O_CLOEXEC);
	if (c->fd < 0)
		return -errno;
	c->id = FD_TO_CLOCKID(c->fd);
	return 0;
}

void fwt_clock_close(struct fwt_clock *c)
{
	if (c->fd >= 0)
		close(c->fd);
	c->fd = -1;
}

int64_t fwt_mono_now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts_ns(&ts);
}

int fwt_clock_mono_offset(const struct fwt_clock *c, int64_t *off_ns)
{
	int64_t best_span = INT64_MAX;

	for (int k = 0; k < OFFSET_READS; k++) {
		struct timespec m1, t, m2;

		clock_gettime(CLOCK_MONOTONIC, &m1);
		if (clock_gettime(c->id, &t) != 0)
			return -errno;
		clock_gettime(CLOCK_MONOTONIC, &m2);

		int64_t span = ts_ns(&m2) - ts_ns(&m1);

		if (span < best_span) {
			best_span = span;
			*off_ns = ts_ns(&t) - (ts_ns(&m1) + span / 2);
		}
	}
	return 0;
}
