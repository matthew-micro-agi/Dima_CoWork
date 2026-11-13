#include "fwt_stamp.h"

#define Y_WHITE 235
#define Y_BLACK 16
#define C_NEUTRAL 128

static bool fits(int w, int h, const struct fwt_stamp_geom *g)
{
	return g->block >= 2 && g->block % 2 == 0 && g->x >= 0 && g->y >= 0 &&
	       g->x % 2 == 0 && g->y % 2 == 0 &&
	       g->x + FWT_STAMP_BITS * g->block <= w && g->y + g->block <= h;
}

/* Bit i of the row (0 = leftmost). */
static int row_bit(uint32_t v, int i)
{
	if (i == 0)
		return 1;
	if (i == 1)
		return 0;
	if (i < 34)
		return (v >> (31 - (i - 2))) & 1;
	return __builtin_parity(v);  /* even parity: value bits + parity bit = even */
}

bool fwt_stamp_geom_default(int w, int h, struct fwt_stamp_geom *g)
{
	int b = (w / 60) & ~1;

	if (b < 8)
		b = 8;
	g->block = b;
	g->x = b;
	g->y = b;
	return fits(w, h, g);
}

bool fwt_stamp_draw(uint8_t *y, int y_stride, uint8_t *uv, int uv_stride, int w, int h,
		    const struct fwt_stamp_geom *g, uint32_t value)
{
	if (!fits(w, h, g))
		return false;

	for (int i = 0; i < FWT_STAMP_BITS; i++) {
		uint8_t px = row_bit(value, i) ? Y_WHITE : Y_BLACK;
		int x0 = g->x + i * g->block;

		for (int r = 0; r < g->block; r++) {
			uint8_t *line = y + (g->y + r) * y_stride + x0;

			for (int c = 0; c < g->block; c++)
				line[c] = px;
		}
	}
	if (uv) {  /* NV12: interleaved U/V at half resolution */
		int cw = FWT_STAMP_BITS * g->block;  /* in luma pixels = bytes in a UV line */

		for (int r = 0; r < g->block / 2; r++) {
			uint8_t *line = uv + (g->y / 2 + r) * uv_stride + g->x;

			for (int c = 0; c < cw; c++)
				line[c] = C_NEUTRAL;
		}
	}
	return true;
}

/* Mean luma of the central half of block i. */
static int block_mean(const uint8_t *y, int stride, const struct fwt_stamp_geom *g, int i)
{
	int q = g->block / 4, n = 0, sum = 0;
	int x0 = g->x + i * g->block;

	for (int r = q; r < g->block - q; r++)
		for (int c = q; c < g->block - q; c++, n++)
			sum += y[(g->y + r) * stride + x0 + c];
	return n ? sum / n : 0;
}

bool fwt_stamp_read(const uint8_t *y, int y_stride, int w, int h,
		    const struct fwt_stamp_geom *g, uint32_t *value)
{
	if (!fits(w, h, g))
		return false;

	int bits[FWT_STAMP_BITS];
	const int thr = (Y_WHITE + Y_BLACK) / 2;

	for (int i = 0; i < FWT_STAMP_BITS; i++)
		bits[i] = block_mean(y, y_stride, g, i) > thr;
	if (bits[0] != 1 || bits[1] != 0)
		return false;

	uint32_t v = 0;

	for (int i = 2; i < 34; i++)
		v = v << 1 | (uint32_t)bits[i];
	if (bits[34] != __builtin_parity(v))
		return false;
	*value = v;
	return true;
}
