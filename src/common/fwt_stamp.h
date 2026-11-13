/*
 * Frame index drawn into test frames (DL-17).
 *
 * A row of square blocks in the luma plane, left to right:
 *   [1][0] marker, 32 bits of the value (most significant first), even parity.
 * 1 = white (Y 235), 0 = black (Y 16); chroma under the row set neutral (128).
 * Read back after encode + decode by sampling the centre of each block.
 * Only the low 32 bits of the index are drawn (2^32 frames = 2.2 years at 60 fps).
 */
#ifndef FWT_STAMP_H
#define FWT_STAMP_H

#include <stdbool.h>
#include <stdint.h>

#define FWT_STAMP_BITS   35  /* 2 marker + 32 value + 1 parity */

struct fwt_stamp_geom {
	int x, y;    /* top-left of the row, pixels */
	int block;   /* block edge, pixels, even */
};

/* Default placement for a w x h frame: block = w/60 (even, >= 8), one block
 * from the top-left corner. Returns false if the row doesn't fit. */
bool fwt_stamp_geom_default(int w, int h, struct fwt_stamp_geom *g);

/*
 * Draw value into an NV12 frame. y/uv are the planes with their strides; uv may
 * be NULL (grey input). Returns false if the row doesn't fit in w x h.
 */
bool fwt_stamp_draw(uint8_t *y, int y_stride, uint8_t *uv, int uv_stride, int w, int h,
		    const struct fwt_stamp_geom *g, uint32_t value);

/*
 * Read the value back from a luma plane. Returns false if the row doesn't fit,
 * the marker is wrong or the parity fails.
 */
bool fwt_stamp_read(const uint8_t *y, int y_stride, int w, int h,
		    const struct fwt_stamp_geom *g, uint32_t *value);

#endif
