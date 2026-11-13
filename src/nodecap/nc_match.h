/*
 * Frame-to-edge binding by time (DL-27), for camera sources (emulator, V4L2):
 * pure logic, unit-tested on the laptop.
 *
 *   edge:  trigd edge, its time converted to CLOCK_MONOTONIC on arrival
 *   frame: capture timestamp (monotonic) from the source
 *
 * A frame belongs to the edge whose time + expected latency is nearest, if
 * within +- window (default half a period). Several frames for one edge: the
 * nearest wins, the others are spurious. An edge is decided once its window
 * has passed (a later frame arrived, or the clock moved beyond it + grace):
 * then it comes out with its frame, or without one (a gap). The frame index
 * always comes from the edge, never from counting frames.
 *
 * Frames are handed in as opaque tags (the caller's copy of the frame); the
 * matcher only says which tag belongs to which edge and which to throw away.
 */
#ifndef NC_MATCH_H
#define NC_MATCH_H

#include <stdbool.h>
#include <stdint.h>

#define NC_MATCH_EDGES 64  /* undecided edges; a few periods' worth is plenty */

struct nc_match_cfg {
	int64_t period_ns;
	int64_t latency_ns;  /* expected edge -> frame timestamp */
	int64_t window_ns;   /* accept +- this around edge + latency; <= 0: period / 2 */
	int64_t grace_ns;    /* wait this long past a window without frames; <= 0: period */
};

struct nc_medge {
	int64_t  index;
	int64_t  t_ns;       /* trigger clock (PTP), as trigd sent it */
	int64_t  t_mono_ns;  /* same instant, monotonic */
	uint32_t session;
	uint32_t flags;
	/* candidate frame */
	bool     has_frame;
	uint64_t tag;
	int64_t  frame_mono_ns;
	int64_t  dist_ns;    /* |frame - (edge + latency)| */
};

struct nc_match_stats {
	uint64_t edges, frames;
	uint64_t bound;        /* edges decided with a frame */
	uint64_t no_frame;     /* edges decided without one (gap) */
	uint64_t no_edge;      /* frames with no edge in their window */
	uint64_t lost;         /* frames beaten by a nearer one for the same edge */
	uint64_t edge_overflow;
	/* measured latency of bound frames (frame - edge), ns */
	int64_t  lat_min, lat_max;
	int64_t  lat_sum;
};

struct nc_match {
	struct nc_match_cfg cfg;
	struct nc_medge e[NC_MATCH_EDGES];  /* undecided, oldest first */
	int n;
	int64_t last_frame_mono;
	struct nc_match_stats st;
};

enum nc_match_result {
	NC_M_HELD           = 0,  /* frame kept as its edge's candidate */
	NC_M_HELD_DISPLACED = 1,  /* kept; the previous candidate (*displaced) is spurious */
	NC_M_NO_EDGE        = 2,  /* spurious: no edge in the window */
	NC_M_LOST           = 3,  /* spurious: the edge has a nearer frame */
};

void nc_match_init(struct nc_match *m, const struct nc_match_cfg *cfg);
int64_t nc_match_window(const struct nc_match *m);

/* A new edge (in index order). False if the list is full (decide edges first). */
bool nc_match_edge(struct nc_match *m, const struct nc_medge *e);

/* A frame from the source. *index: the edge it was held for (HELD*). */
enum nc_match_result nc_match_frame(struct nc_match *m, int64_t t_mono_ns, uint64_t tag,
				    int64_t *index, uint64_t *displaced);

/*
 * Take the oldest edge if it is decided at now_mono_ns (INT64_MAX: decide
 * everything, at the end). Returns true and fills *out (out->has_frame and
 * out->tag tell whether it got a frame).
 */
bool nc_match_take_decided(struct nc_match *m, int64_t now_mono_ns, struct nc_medge *out);

/* Mean measured latency of bound frames; false if none yet. */
bool nc_match_latency_mean(const struct nc_match *m, int64_t *mean_ns);

#endif
