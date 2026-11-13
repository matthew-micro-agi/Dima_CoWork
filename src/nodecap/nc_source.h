/*
 * Frame sources of nodecap (DL-26): where raw frames come from.
 *
 *   test   built-in: one frame per trigger edge, made on the spot (binding by
 *          construction); the index is drawn into the frame (DL-17)
 *   emu    cam-emu process over a V4L2-like buffer protocol (fwt_cam_proto.h);
 *          frames bound to edges by time (nc_match.h, DL-27)
 *   v4l2   a real camera, /dev/videoN (later; same binding as emu)
 *
 * Same shape as a V4L2 capture device: an fd to poll, dequeue a filled
 * buffer (data, monotonic timestamp, sequence counter), release it.
 */
#ifndef NC_SOURCE_H
#define NC_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fwt_trig_msg.h"

struct nc_frame {
	const uint8_t *data;   /* NV12, w x h, stride = w */
	size_t   len;
	int64_t  t_mono_ns;    /* capture time, CLOCK_MONOTONIC (0 if not known) */
	uint32_t sequence;     /* frame counter of the source */
	int      slot;         /* source buffer, for release */

	/* test source: the edge this frame was made for (no matching needed) */
	bool     has_edge;
	struct fwt_trig_msg edge;
	/* test source: draw this value into the frame after copying (DL-17) */
	bool     draw_stamp;
	uint32_t stamp;
};

struct nc_source;

struct nc_source_ops {
	/* fd to poll for frames, or -1 if frames only follow on_edge() */
	int  (*fd)(struct nc_source *s);
	/* a trigger edge was received (the test source makes its frame here) */
	void (*on_edge)(struct nc_source *s, const struct fwt_trig_msg *edge);
	/* 1: *f filled, 0: no frame ready, < 0: -errno (source failed) */
	int  (*dequeue)(struct nc_source *s, struct nc_frame *f);
	/* the frame's data is no longer used */
	void (*release)(struct nc_source *s, const struct nc_frame *f);
	void (*destroy)(struct nc_source *s);
};

struct nc_source {
	const struct nc_source_ops *ops;
	const char *name;
};

/* Built-in test source: w x h NV12 patterns, one frame per edge. */
struct nc_source *nc_source_test_new(int w, int h);

/* cam-emu client: connect to path, w x h NV12, nbufs buffers, stream on.
 * NULL on failure (reason printed). */
struct nc_source *nc_source_emu_new(const char *path, int w, int h, int nbufs);

#endif
