#include "fwt_sei.h"

#include <string.h>

/* Random, fixed: "our per-frame trigger SEI". Never change for version 1. */
const uint8_t FWT_SEI_UUID[16] = {
	0x6b, 0x1f, 0x3a, 0x52, 0x9c, 0x0e, 0x4d, 0x77,
	0xa1, 0x58, 0x2e, 0xc4, 0x90, 0x3d, 0x6f, 0x15,
};

#define SEI_TYPE_USER_DATA_UNREGISTERED 5

static void put16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v; }

static void put32(uint8_t *p, uint32_t v)
{
	put16(p, v >> 16);
	put16(p + 2, v);
}

static void put64(uint8_t *p, uint64_t v)
{
	put32(p, v >> 32);
	put32(p + 4, v);
}

static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

static uint32_t get32(const uint8_t *p)
{
	return (uint32_t)get16(p) << 16 | get16(p + 2);
}

static uint64_t get64(const uint8_t *p)
{
	return (uint64_t)get32(p) << 32 | get32(p + 4);
}

size_t fwt_ebsp_escape(const uint8_t *in, size_t len, uint8_t *out, size_t cap)
{
	size_t o = 0;
	int zeros = 0;

	for (size_t i = 0; i < len; i++) {
		if (zeros >= 2 && in[i] <= 3) {
			if (o >= cap)
				return 0;
			out[o++] = 3;
			zeros = 0;
		}
		if (o >= cap)
			return 0;
		out[o++] = in[i];
		zeros = in[i] == 0 ? zeros + 1 : 0;
	}
	return o;
}

size_t fwt_ebsp_unescape(const uint8_t *in, size_t len, uint8_t *out, size_t cap)
{
	size_t o = 0;
	int zeros = 0;

	for (size_t i = 0; i < len; i++) {
		if (zeros >= 2 && in[i] == 3) {
			zeros = 0;
			continue;
		}
		if (o >= cap)
			return 0;
		out[o++] = in[i];
		zeros = in[i] == 0 ? zeros + 1 : 0;
	}
	return o;
}

size_t fwt_sei_build(const struct fwt_sei *s, uint8_t *out, size_t cap)
{
	uint8_t rbsp[2 + FWT_SEI_PAYLOAD_LEN + 1];
	uint8_t *p = rbsp;

	*p++ = SEI_TYPE_USER_DATA_UNREGISTERED;
	*p++ = FWT_SEI_PAYLOAD_LEN;
	memcpy(p, FWT_SEI_UUID, 16);
	p += 16;
	put16(p, s->version);
	put16(p + 2, 0);
	put32(p + 4, s->session);
	put32(p + 8, s->flags);
	put64(p + 12, (uint64_t)s->index);
	put64(p + 20, (uint64_t)s->t_ns);
	p += FWT_SEI_PAYLOAD_LEN - 16;
	*p++ = 0x80;  /* rbsp_trailing_bits */

	if (cap < 6)
		return 0;
	out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 1;
	out[4] = FWT_H265_NAL_PREFIX_SEI << 1;  /* forbidden 0, type, layer id 0 */
	out[5] = 1;                             /* temporal id + 1 */

	size_t n = fwt_ebsp_escape(rbsp, sizeof(rbsp), out + 6, cap - 6);

	return n ? 6 + n : 0;
}

/* Read a ff-extended SEI value (payloadType / payloadSize). */
static bool sei_value(const uint8_t *b, size_t len, size_t *pos, size_t *v)
{
	*v = 0;
	while (*pos < len && b[*pos] == 0xff) {
		*v += 255;
		(*pos)++;
	}
	if (*pos >= len)
		return false;
	*v += b[(*pos)++];
	return true;
}

bool fwt_sei_parse_nal(const uint8_t *nal, size_t len, struct fwt_sei *s)
{
	uint8_t rbsp[1024];

	if (len < 3 || ((nal[0] >> 1) & 0x3f) != FWT_H265_NAL_PREFIX_SEI)
		return false;

	size_t n = fwt_ebsp_unescape(nal + 2, len - 2, rbsp, sizeof(rbsp));
	size_t pos = 0;

	/* Several SEI messages may share one NAL unit; stop at the trailing bits. */
	while (pos < n && !(pos == n - 1 && rbsp[pos] == 0x80)) {
		size_t type, size;

		if (!sei_value(rbsp, n, &pos, &type) || !sei_value(rbsp, n, &pos, &size) ||
		    pos + size > n)
			return false;

		const uint8_t *p = rbsp + pos;

		pos += size;
		if (type != SEI_TYPE_USER_DATA_UNREGISTERED || size < FWT_SEI_PAYLOAD_LEN ||
		    memcmp(p, FWT_SEI_UUID, 16) != 0)
			continue;
		p += 16;
		s->version = get16(p);
		if (s->version != FWT_SEI_VERSION)
			continue;
		s->session = get32(p + 4);
		s->flags = get32(p + 8);
		s->index = (int64_t)get64(p + 12);
		s->t_ns = (int64_t)get64(p + 20);
		return true;
	}
	return false;
}

/* Offset of the next 00 00 01 at or after from, or len. */
static size_t find_sc3(const uint8_t *b, size_t len, size_t from)
{
	for (size_t i = from; i + 2 < len; i++)
		if (b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1)
			return i;
	return len;
}

bool fwt_nal_next(const uint8_t *buf, size_t len, size_t *pos, struct fwt_nal *n)
{
	size_t sc = find_sc3(buf, len, *pos);

	if (sc >= len)
		return false;
	n->off = sc + 3;
	n->sc_off = (sc > *pos && buf[sc - 1] == 0) ? sc - 1 : sc;

	size_t end = find_sc3(buf, len, n->off);

	while (end > n->off && buf[end - 1] == 0)  /* trailing zeros, next start code's zero_byte */
		end--;
	n->len = end - n->off;
	*pos = end;  /* next search sees the zero_byte, so 4-byte start codes are found whole */
	n->type = n->len ? (buf[n->off] >> 1) & 0x3f : -1;
	return true;
}

bool fwt_sei_insert_offset(const uint8_t *au, size_t len, size_t *off)
{
	size_t pos = 0;
	struct fwt_nal n;

	while (fwt_nal_next(au, len, &pos, &n)) {
		if (n.type >= 0 && n.type < 32) {  /* VCL: picture data */
			*off = n.sc_off;
			return true;
		}
	}
	return false;
}
