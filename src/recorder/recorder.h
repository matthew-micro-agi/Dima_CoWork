/*
 * Recorder: segments, manifest, ring, locks, recovery (architecture §5.2-5.6,
 * DL-06, DL-07, DL-18..21). Plain functions; nodecap calls them from its
 * output thread (frames) and main thread (locks) under one mutex. The recorder
 * is the only owner of /rec and its manifest (flock).
 *
 *   /rec/manifest.bin                     append-only change log (the truth)
 *   /rec/s<session>/<first index>.h265    segment files, 4 s, start with a keyframe
 */
#ifndef RECORDER_H
#define RECORDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/uio.h>

#include "rec_manifest.h"

struct rec_config {
	const char *dir;
	uint64_t ring_max_bytes;  /* 0 = whole filesystem minus reserve */
	uint64_t reserve_bytes;   /* always left free on the filesystem */
	int      seg_seconds;     /* segment length (DL-07: 4) */
	int      lock_cap_pct;    /* max share of the ring that may be locked (20) */
	uint64_t min_seg_bytes;   /* expected segment size before one has closed */
	FILE    *log;             /* messages, or NULL */
};

struct rec_seg {
	uint32_t session;
	struct rec_seg_info i;
	bool deleted;
};

struct rec_sess {
	uint32_t session;
	int64_t  t0_ns, period_ns, wall_ns;
	uint32_t status;          /* enum rec_status */
	int64_t  last_index;
	uint64_t frames, missing;
	uint64_t segments, deleted_segments;
};

struct rec_lock {
	uint64_t id;              /* seq of its LOCK record */
	uint32_t session;
	struct rec_lock_info i;
	bool active;              /* not unlocked (may still have expired) */
};

struct rec_stats {
	uint64_t frames, bytes, segments_closed, segments_deleted, sessions_emptied;
	uint64_t skipped_no_key, no_room, locks_refused;
	uint64_t recovered_segments, recovery_dropped_files, torn_bytes_cut, stale_files_removed;
};

struct recorder {
	struct rec_config cfg;
	char dir[256];
	int  mfd;                 /* manifest */
	bool readonly;
	uint64_t seq;             /* last record seq */

	struct rec_seg  *segs;  size_t nsegs, capsegs;
	struct rec_sess *sess;  size_t nsess, capsess;
	struct rec_lock *locks; size_t nlocks, caplocks;

	/* running session */
	bool     active;
	uint32_t cur_session;
	int64_t  seg_frames;      /* frames per segment */
	bool     have_last;
	int64_t  last_idx;

	/* open segment */
	bool     seg_open;
	int      seg_fd;
	int64_t  boundary;        /* next cut at the first keyframe with index >= this */
	struct rec_seg_info cur;

	struct rec_stats st;
};

/* Open /rec read-write: lock it, load the manifest (cut a torn tail), recover
 * an interrupted session. 0, or -errno (-EBUSY: another recorder owns it). */
int rec_open(struct recorder *r, const struct rec_config *cfg);

/* Load the index only (no lock, no recovery, no changes): for rec-tool. */
int rec_open_readonly(struct recorder *r, const char *dir);

/* Start a session (DL-20: refused if the number is already on disk). */
int rec_session_start(struct recorder *r, uint32_t session, int64_t t0_ns, int64_t period_ns);

/*
 * Write one encoded frame (an access unit, SEI included). Opens/cuts segments
 * (DL-18) and makes room in the ring first. Returns 0 if written, 1 if skipped
 * (no segment open and not a keyframe), -errno on error (-ENOSPC: no room).
 */
int rec_write_frame(struct recorder *r, const struct iovec *iov, int iovcnt, int64_t index,
		    int64_t t_ns, bool key);

/* Close the open segment and end the session as complete. */
int rec_session_end(struct recorder *r);

/*
 * Lock [from_idx, to_idx] of a session for ttl_s seconds; to_idx may be in the
 * future (post-event window). Refused (-EDQUOT) above the lock cap. On
 * success *id = lock id. why gets a short reason on failure.
 */
int rec_lock(struct recorder *r, uint32_t session, int64_t from_idx, int64_t to_idx,
	     int64_t ttl_s, const char *reason, uint64_t *id, char *why, size_t why_len);
int rec_unlock(struct recorder *r, uint64_t id);

/* End a running session, close everything. */
void rec_close(struct recorder *r);

/* Test hook: drop everything without writing records (simulated crash). */
void rec_abandon(struct recorder *r);

/* Queries */
bool     rec_seg_locked(const struct recorder *r, const struct rec_seg *s, int64_t now_wall_ns);
uint64_t rec_used_bytes(const struct recorder *r);
uint64_t rec_locked_bytes(const struct recorder *r, int64_t now_wall_ns);
void     rec_seg_path(const struct recorder *r, uint32_t session, int64_t first_idx, char *buf,
		      size_t len);
const struct rec_sess *rec_find_session(const struct recorder *r, uint32_t session);
int64_t  rec_now_wall_ns(void);

#endif
