/*
 * Trigger-clock time to CLOCK_MONOTONIC (DL-27): edge times come from trigd
 * on the PTP hardware clock (board) or the system clock (laptop); frame
 * timestamps are monotonic. Read-only use of the clock (trigd owns it).
 */
#ifndef FWT_CLOCK_H
#define FWT_CLOCK_H

#include <stdint.h>
#include <time.h>

struct fwt_clock {
	clockid_t id;
	int       fd;  /* PTP device fd, -1 for the system clock */
};

/* dev == NULL: system clock (CLOCK_REALTIME). Returns 0 or -errno. */
int  fwt_clock_open(struct fwt_clock *c, const char *dev);
void fwt_clock_close(struct fwt_clock *c);

/*
 * Offset clock - CLOCK_MONOTONIC in ns: best of a few reads (the one with the
 * shortest monotonic bracket). mono = t - offset. Returns 0 or -errno.
 */
int fwt_clock_mono_offset(const struct fwt_clock *c, int64_t *off_ns);

int64_t fwt_mono_now_ns(void);

#endif
