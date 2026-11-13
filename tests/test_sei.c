/* Unit tests for the per-frame SEI (build, parse, emulation prevention, NAL walk). */
#include <stdio.h>
#include <string.h>

#include "fwt_sei.h"

static int failures;

#define CHECK(cond)                                                          \
	do {                                                                 \
		if (!(cond)) {                                               \
			fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__,     \
				__LINE__, #cond);                            \
			failures++;                                          \
		}                                                            \
	} while (0)

static bool sei_eq(const struct fwt_sei *a, const struct fwt_sei *b)
{
	return a->version == b->version && a->session == b->session &&
	       a->flags == b->flags && a->index == b->index && a->t_ns == b->t_ns;
}

/* No 00 00 0x (x <= 3) inside the NAL after the start code. */
static bool no_emulation(const uint8_t *nal, size_t len)
{
	for (size_t i = 0; i + 2 < len; i++)
		if (nal[i] == 0 && nal[i + 1] == 0 && nal[i + 2] <= 2)
			return false;
	return true;
}

static void test_escape(void)
{
	const uint8_t in[] = { 0, 0, 0, 0, 0, 1, 0, 0, 2, 0, 0, 3, 0, 0, 4 };
	const uint8_t want[] = { 0, 0, 3, 0, 0, 3, 0, 1, 0, 0, 3, 2, 0, 0, 3, 3, 0, 0, 4 };
	uint8_t e[64], u[64];
	size_t n = fwt_ebsp_escape(in, sizeof(in), e, sizeof(e));

	CHECK(n == sizeof(want) && memcmp(e, want, n) == 0);
	CHECK(fwt_ebsp_unescape(e, n, u, sizeof(u)) == sizeof(in));
	CHECK(memcmp(u, in, sizeof(in)) == 0);
	CHECK(fwt_ebsp_escape(in, sizeof(in), e, 4) == 0);  /* too small */
}

static void test_roundtrip(void)
{
	const struct fwt_sei cases[] = {
		{ FWT_SEI_VERSION, 7, 3, 0, 0 },                       /* zeros -> escaping */
		{ FWT_SEI_VERSION, 1, 0, 123456, 1731400000123456789LL },
		{ FWT_SEI_VERSION, 0xffffffffu, 0xffffffffu, -5, -1 },
		{ FWT_SEI_VERSION, 0, 0, 0x0000000100000000LL, 0 },
	};

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		uint8_t b[FWT_SEI_NAL_MAX];
		struct fwt_sei got = { 0 };
		size_t n = fwt_sei_build(&cases[i], b, sizeof(b));

		CHECK(n > 6);
		CHECK(b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 1);
		CHECK(((b[4] >> 1) & 0x3f) == FWT_H265_NAL_PREFIX_SEI);
		CHECK(no_emulation(b + 4, n - 4));
		CHECK(fwt_sei_parse_nal(b + 4, n - 4, &got));
		CHECK(sei_eq(&got, &cases[i]));
	}

	uint8_t small[20];

	CHECK(fwt_sei_build(&cases[0], small, sizeof(small)) == 0);
}

static void test_reject(void)
{
	struct fwt_sei s = { FWT_SEI_VERSION, 1, 0, 42, 99 }, got;
	uint8_t b[FWT_SEI_NAL_MAX];
	size_t n = fwt_sei_build(&s, b, sizeof(b));

	CHECK(!fwt_sei_parse_nal(b + 4, n - 10, &got));  /* truncated */

	uint8_t c[FWT_SEI_NAL_MAX];

	memcpy(c, b, n);
	c[4 + 2 + 2] ^= 0xff;                            /* first UUID byte */
	CHECK(!fwt_sei_parse_nal(c + 4, n - 4, &got));

	memcpy(c, b, n);
	c[4] = 40 << 1;                                  /* suffix SEI, not ours */
	CHECK(!fwt_sei_parse_nal(c + 4, n - 4, &got));

	s.version = 2;                                   /* unknown version */
	n = fwt_sei_build(&s, b, sizeof(b));
	CHECK(!fwt_sei_parse_nal(b + 4, n - 4, &got));
}

/* Access unit: VPS, SPS, PPS (non-VCL), then an IDR slice (type 19). */
static const uint8_t au[] = {
	0, 0, 0, 1, 32 << 1, 1, 0xaa, 0xbb,
	0, 0, 0, 1, 33 << 1, 1, 0xcc,
	0, 0, 1, 34 << 1, 1, 0xdd, 0x00,       /* 3-byte start code, trailing zero */
	0, 0, 0, 1, 19 << 1, 1, 0xee, 0xff,
};

static void test_nal_walk(void)
{
	size_t pos = 0, k = 0;
	struct fwt_nal n;
	const int types[] = { 32, 33, 34, 19 };
	const size_t lens[] = { 4, 3, 3, 4 };

	while (fwt_nal_next(au, sizeof(au), &pos, &n)) {
		CHECK(k < 4);
		if (k < 4) {
			CHECK(n.type == types[k]);
			CHECK(n.len == lens[k]);
		}
		k++;
	}
	CHECK(k == 4);

	size_t off = 0;

	CHECK(fwt_sei_insert_offset(au, sizeof(au), &off));
	CHECK(off == 22);  /* the IDR's 4-byte start code */
	CHECK(!fwt_sei_insert_offset(au, 22, &off));   /* parameter sets only */
}

static void test_insert_and_find(void)
{
	struct fwt_sei s = { FWT_SEI_VERSION, 9, 2, 1234, 5678 }, got = { 0 };
	uint8_t sei[FWT_SEI_NAL_MAX], out[256];
	size_t sn = fwt_sei_build(&s, sei, sizeof(sei)), off = 0;

	CHECK(fwt_sei_insert_offset(au, sizeof(au), &off));
	memcpy(out, au, off);
	memcpy(out + off, sei, sn);
	memcpy(out + off + sn, au + off, sizeof(au) - off);

	size_t len = sizeof(au) + sn, pos = 0;
	struct fwt_nal n;
	int found = 0, order = 0, sei_at = -1, idr_at = -1;

	while (fwt_nal_next(out, len, &pos, &n)) {
		if (fwt_sei_parse_nal(out + n.off, n.len, &got)) {
			found++;
			sei_at = order;
		}
		if (n.type == 19)
			idr_at = order;
		order++;
	}
	CHECK(found == 1 && sei_eq(&got, &s));
	CHECK(sei_at == 3 && idr_at == 4);  /* after parameter sets, before the slice */
}

int main(void)
{
	test_escape();
	test_roundtrip();
	test_reject();
	test_nal_walk();
	test_insert_and_find();
	if (failures) {
		fprintf(stderr, "test_sei: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_sei: all passed\n");
	return 0;
}
