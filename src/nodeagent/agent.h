/*
 * nodeagent: the node's control interface (architecture §6, DL-03, DL-04,
 * DL-22, DL-23). HTTP + CBOR per icd/fwt-icd-v1.cddl.
 *
 * The only long-running process: reads /rec read-only for queries and fetch,
 * starts trigd + nodecap per armed session, forwards locks to the recorder.
 */
#ifndef AGENT_H
#define AGENT_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "recorder.h"

#define NA_MAX_FETCHES   4
#define NA_MAX_FETCH_S   600     /* longest fetch per request: 10 min */
#define NA_MAX_BODY      4096
#define NA_RESERVE_BYTES (64ull << 20)

struct na_opts {
	unsigned port;
	const char *rec_dir, *trigd_bin, *nodecap_bin, *trigd_args, *nodecap_args;
	const char *trigd_sock, *ctl_sock, *log_dir, *ptp_dev, *emmc;
	uint64_t ring_max_mb;
	unsigned bitrate;
};

enum na_state { NA_IDLE, NA_RECORDING, NA_FAILED };

struct agent {
	struct na_opts o;
	pthread_mutex_t lock;

	/* read-only view of /rec, reloaded when the manifest grows (DL-22) */
	struct recorder idx;
	bool idx_loaded;
	long long idx_size;

	/* session control (DL-23) */
	enum na_state state;
	bool disarming;
	uint32_t session;
	int64_t t0_ns, period_ns;
	pid_t trigd_pid, nodecap_pid;
	char message[128];

	int fetches;
};

/* Reload the index if the manifest changed. Call with a->lock held. */
void na_index_refresh(struct agent *a);

/* Session control (na_session.c). Call with a->lock held.
 * Return 0, or an HTTP status with *code / msg set. */
int na_arm(struct agent *a, uint32_t session, unsigned fps, bool t0_given, int64_t t0,
	   const char **code, char *msg, size_t msg_len);
int na_disarm(struct agent *a, const char **code, char *msg, size_t msg_len);
/* Reap children; a child that died during a session fails it. */
void na_poll_children(struct agent *a);
/* PTP clock now (falls back to CLOCK_REALTIME without a PTP device). */
int64_t na_ptp_now(const struct agent *a);

#endif
