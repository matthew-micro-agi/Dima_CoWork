#include "trigd_perout.h"

#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/ptp_clock.h>

static int perout_request(int fd, unsigned int index, int64_t start_ns, int64_t period_ns)
{
	struct ptp_perout_request req;

	memset(&req, 0, sizeof(req));
	req.index = index;
	req.start.sec = start_ns / 1000000000LL;
	req.start.nsec = (uint32_t)(start_ns % 1000000000LL);
	req.period.sec = period_ns / 1000000000LL;
	req.period.nsec = (uint32_t)(period_ns % 1000000000LL);
	req.flags = 0;

	if (ioctl(fd, PTP_PEROUT_REQUEST2, &req) != 0)
		return -errno;
	return 0;
}

int trigd_perout_arm(int fd, unsigned int index, int64_t start_ns, int64_t period_ns)
{
	if (start_ns < 0 || period_ns <= 0)
		return -EINVAL;
	return perout_request(fd, index, start_ns, period_ns);
}

int trigd_perout_disarm(int fd, unsigned int index)
{
	return perout_request(fd, index, 0, 0);  /* period 0 = off */
}
