/*
 * fwt-info: smoke test for the toolchain. Prints the version, the system time
 * and the PTP hardware clock time. Runs on the laptop and on the board.
 *
 *   fwt-info [/dev/ptpN]     default /dev/ptp0
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "fwt_version.h"

/* Dynamic POSIX clock id for a PTP character device (see Linux ptp docs). */
#define FD_TO_CLOCKID(fd) ((~(clockid_t)(fd) << 3) | 3)

static void print_ts(const char *label, const struct timespec *ts)
{
	printf("%-12s %lld.%09ld s\n", label, (long long)ts->tv_sec, ts->tv_nsec);
}

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/ptp0";
	struct timespec sys, phc;

	printf("fwt-info %s\n", FWT_VERSION);

	clock_gettime(CLOCK_REALTIME, &sys);
	print_ts("system:", &sys);

	int fd = open(dev, O_RDONLY);
	if (fd < 0) {
		printf("%-12s %s: %s\n", "phc:", dev, strerror(errno));
		return 0;  /* not an error for a smoke test (e.g. no access on the laptop) */
	}
	if (clock_gettime(FD_TO_CLOCKID(fd), &phc) == 0)
		print_ts("phc:", &phc);
	else
		printf("%-12s %s: %s\n", "phc:", dev, strerror(errno));
	close(fd);
	return 0;
}
