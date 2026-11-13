/* Unit tests for the frame index drawn into test frames. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fwt_stamp.h"

static int failures;

#define CHECK(cond)                                                          \
	do {                                                                 \
		if (!(cond)) {                                               \
			fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__,     \
				__LINE__, #cond);                            \
			failures++;                                          \
		}                                                            \
	} while (0)

#define W 1920
#define H 1200

static uint8_t y_plane[W * H], uv_plane[W * H / 2];

/* Busy background: a gradient, so the stamp is not drawn on a flat field. */
static void fill_background(void)
{
	for (int r = 0; r < H; r++)
		for (int c = 0; c < W; c++)
			y_plane[r * W + c] = (uint8_t)((r + c) & 0xff);
	memset(uv_plane, 60, sizeof(uv_plane));
}

static void test_geom(void)
{
	struct fwt_stamp_geom g;

	CHECK(fwt_stamp_geom_default(W, H, &g));
	CHECK(g.block == 32 && g.x == 32 && g.y == 32);
	CHECK(fwt_stamp_geom_default(640, 480, &g));
	CHECK(g.block == 10);
	CHECK(!fwt_stamp_geom_default(200, 100, &g));  /* 35 blocks of 8 don't fit */
}

static void test_roundtrip(void)
{
	const uint32_t vals[] = { 0, 1, 2, 0x80000000u, 0xffffffffu, 0xa5a5a5a5u, 123456 };
	struct fwt_stamp_geom g;

	fwt_stamp_geom_default(W, H, &g);
	for (size_t i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
		uint32_t got = 0xdeadbeef;

		fill_background();
		CHECK(fwt_stamp_draw(y_plane, W, uv_plane, W, W, H, &g, vals[i]));
		CHECK(fwt_stamp_read(y_plane, W, W, H, &g, &got));
		CHECK(got == vals[i]);
	}
	/* chroma under the row is neutral, next to it untouched */
	CHECK(uv_plane[(g.y / 2) * W + g.x] == 128);
	CHECK(uv_plane[(g.y / 2) * W + g.x - 1] == 60);
}

/* Compression noise: +-60 on every pixel still reads correctly. */
static void test_noise(void)
{
	struct fwt_stamp_geom g;
	uint32_t got = 0;

	fwt_stamp_geom_default(W, H, &g);
	fill_background();
	fwt_stamp_draw(y_plane, W, NULL, 0, W, H, &g, 0x12345678u);
	srand(1);
	for (int i = 0; i < W * H; i++) {
		int v = y_plane[i] + rand() % 121 - 60;

		y_plane[i] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
	}
	CHECK(fwt_stamp_read(y_plane, W, W, H, &g, &got));
	CHECK(got == 0x12345678u);
}

/* Paint block i with luma v. */
static void paint(const struct fwt_stamp_geom *g, int i, uint8_t v)
{
	for (int r = 0; r < g->block; r++)
		memset(y_plane + (g->y + r) * W + g->x + i * g->block, v, g->block);
}

static void test_reject(void)
{
	struct fwt_stamp_geom g;
	uint32_t got;

	fwt_stamp_geom_default(W, H, &g);

	fill_background();
	fwt_stamp_draw(y_plane, W, NULL, 0, W, H, &g, 0x0f0f0f0fu);
	paint(&g, 10, 235);                 /* flip one value bit -> parity fails */
	CHECK(!fwt_stamp_read(y_plane, W, W, H, &g, &got));

	fill_background();
	fwt_stamp_draw(y_plane, W, NULL, 0, W, H, &g, 0x0f0f0f0fu);
	paint(&g, 0, 16);                   /* marker broken */
	CHECK(!fwt_stamp_read(y_plane, W, W, H, &g, &got));

	memset(y_plane, 16, sizeof(y_plane));  /* no stamp at all */
	CHECK(!fwt_stamp_read(y_plane, W, W, H, &g, &got));

	struct fwt_stamp_geom bad = { .x = 1900, .y = 0, .block = 32 };

	CHECK(!fwt_stamp_draw(y_plane, W, NULL, 0, W, H, &bad, 1));
	CHECK(!fwt_stamp_read(y_plane, W, W, H, &bad, &got));
}

int main(void)
{
	test_geom();
	test_roundtrip();
	test_noise();
	test_reject();
	if (failures) {
		fprintf(stderr, "test_stamp: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_stamp: all passed\n");
	return 0;
}
