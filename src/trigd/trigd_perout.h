/*
 * CPTS periodic output (trigger edge generator) via the Linux PTP API.
 *
 * The output keeps running after the device is closed (the kernel does not
 * stop it on close), so trigd always disarms explicitly, including at start
 * to clear a leftover from a previous run.
 */
#ifndef TRIGD_PEROUT_H
#define TRIGD_PEROUT_H

#include <stdint.h>

/* Start periodic output `index` at start_ns (PTP time) with period_ns. 0 or -errno. */
int trigd_perout_arm(int fd, unsigned int index, int64_t start_ns, int64_t period_ns);

/* Stop periodic output `index`. 0 or -errno. */
int trigd_perout_disarm(int fd, unsigned int index);

#endif
