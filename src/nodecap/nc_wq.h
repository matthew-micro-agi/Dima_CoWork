/*
 * Write queue: encoded frames from the encoder-output thread to the writer
 * thread, which does all file I/O (recorder, --out). Storage latency (an eMMC
 * fdatasync measured up to 0.7 s) then never blocks capture or encoding.
 *
 * Bounded by bytes: a frame that doesn't fit is not queued and counted as an
 * overflow (storage stuck for longer than the queue covers); the caller never
 * waits. pop() blocks until a frame is there, or returns NULL once the queue
 * is closed and empty (the writer drains everything before it ends).
 */
#ifndef NC_WQ_H
#define NC_WQ_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/uio.h>

struct nc_wq_item {
	struct nc_wq_item *next;
	size_t   len;
	int64_t  index, t_ns;
	bool     key;
	uint8_t  data[];  /* the frame, contiguous */
};

struct nc_wq {
	pthread_mutex_t m;
	pthread_cond_t  c;
	struct nc_wq_item *head, *tail;
	size_t   bytes, cap, peak_bytes;
	bool     closed;
	uint64_t pushed, overflow;
};

int  nc_wq_init(struct nc_wq *q, size_t cap_bytes);
void nc_wq_destroy(struct nc_wq *q);  /* frees what is left */

/* Copy iov into a new item at the tail. False (nothing queued) if it doesn't
 * fit, if out of memory (both counted as overflow) or if closed. */
bool nc_wq_push(struct nc_wq *q, const struct iovec *iov, int iovcnt, int64_t index, int64_t t_ns,
		bool key);

/* Oldest item (caller frees it), waiting for one; NULL when closed and empty. */
struct nc_wq_item *nc_wq_pop(struct nc_wq *q);

/* No more pushes; pop() drains what is queued, then returns NULL. */
void nc_wq_close(struct nc_wq *q);

#endif
