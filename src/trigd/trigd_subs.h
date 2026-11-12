/* Subscriber list for trigd (decision log DL-11). Pure bookkeeping, no I/O. */
#ifndef TRIGD_SUBS_H
#define TRIGD_SUBS_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/un.h>

#define TRIGD_MAX_SUBS 8

struct trigd_sub {
	struct sockaddr_un addr;
	socklen_t          len;
	uint64_t           sent;
	uint64_t           dropped;  /* consumer too slow (socket full) */
};

struct trigd_subs {
	struct trigd_sub s[TRIGD_MAX_SUBS];
	int              n;
};

void trigd_subs_init(struct trigd_subs *l);

/* Add if not present. Returns 1 added, 0 already present, -1 list full. */
int trigd_subs_add(struct trigd_subs *l, const struct sockaddr_un *a, socklen_t len);

/* Remove if present. Returns true if removed. */
bool trigd_subs_remove(struct trigd_subs *l, const struct sockaddr_un *a, socklen_t len);

#endif
