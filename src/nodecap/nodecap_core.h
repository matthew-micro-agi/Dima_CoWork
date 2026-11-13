/*
 * nodecap pure logic (no GStreamer, unit-tested on the laptop):
 *   - edge table: trigger edges waiting for their encoded frame
 *   - timestamps: PTS = edge time - T0 (DL-15), index back from PTS
 *   - test frames: pre-made NV12 patterns, so the encoder sees motion
 */
#ifndef NODECAP_CORE_H
#define NODECAP_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NC_TABLE_SIZE 256  /* edges in flight; encoder latency is a few frames */

struct nc_edge {
	int64_t  index;
	int64_t  t_ns;
	uint32_t session;
	uint32_t flags;
	bool     used;
};

struct nc_edge_table {
	struct nc_edge e[NC_TABLE_SIZE];
	uint64_t overwritten;  /* entries replaced before their frame came out */
};

void nc_table_init(struct nc_edge_table *t);
void nc_table_put(struct nc_edge_table *t, const struct nc_edge *e);
/* Find and remove the edge with this index. False if not (or no longer) there. */
bool nc_table_take(struct nc_edge_table *t, int64_t index, struct nc_edge *out);

/* Session start from one edge: T0 = t - index * period. */
int64_t nc_t0_from_edge(int64_t t_ns, int64_t index, int64_t period_ns);
/* PTS of an edge (DL-15). */
int64_t nc_pts_of(int64_t t_ns, int64_t t0_ns);
/* Index of a frame from its PTS: round(pts / period). False if period <= 0. */
bool nc_index_of_pts(int64_t pts_ns, int64_t period_ns, int64_t *index);
/* Frames per second from the period, rounded (caps, keyframe interval). */
int nc_fps_of(int64_t period_ns);

/* NV12 frame size in bytes (w, h even). */
size_t nc_nv12_size(int w, int h);
/* Fill test pattern k into an NV12 frame (y plane then uv plane, stride = w). */
void nc_testframe_fill(uint8_t *frame, int w, int h, int k);

#endif
