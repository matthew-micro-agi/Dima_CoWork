/*
 * The clock trigd schedules on: the PTP hardware clock (/dev/ptpN) on the
 * board, or the system clock (CLOCK_REALTIME) for laptop tests.
 */
#ifndef TRIGD_CLOCK_H
#define TRIGD_CLOCK_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

struct trigd_clock {
	clockid_t id;
	int       fd;      /* PTP device fd, -1 for the system clock */
	bool      is_phc;
};

/* dev == NULL: system clock. Returns 0 or -errno. */
int  trigd_clock_open(struct trigd_clock *c, const char *dev);
void trigd_clock_close(struct trigd_clock *c);

/* Current time in ns on this clock. Returns 0 or -errno. */
int trigd_clock_now(const struct trigd_clock *c, int64_t *ns);

/*
 * Offset clock - CLOCK_MONOTONIC in ns, measured by reading monotonic before
 * and after the clock and taking the midpoint. Used to wake up (timers run on
 * CLOCK_MONOTONIC) near a time given on this clock.
 */
int trigd_clock_mono_offset(const struct trigd_clock *c, int64_t *off_ns);

int64_t trigd_ts_to_ns(const struct timespec *ts);
struct timespec trigd_ns_to_ts(int64_t ns);

#endif
