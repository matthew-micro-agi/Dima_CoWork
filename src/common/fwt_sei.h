/*
 * Per-frame SEI: binds an encoded H.265 frame to its trigger edge (DL-16).
 *
 * One prefix SEI NAL unit (type 39) per access unit, carrying one
 * "user data unregistered" message (payloadType 5):
 *
 *   uuid[16]  FWT_SEI_UUID, identifies our payload
 *   u16       version (FWT_SEI_VERSION)
 *   u16       reserved, 0
 *   u32       session
 *   u32       flags (FWT_TRIG_F_* of the edge)
 *   i64       index
 *   i64       t_ns, edge time on the PTP clock
 *
 * All fields big-endian. Emulation prevention is applied, so the NAL unit is
 * valid Annex B and ignored by decoders that don't know the UUID.
 */
#ifndef FWT_SEI_H
#define FWT_SEI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FWT_SEI_VERSION      1
#define FWT_SEI_PAYLOAD_LEN  44           /* uuid + fields */
#define FWT_SEI_NAL_MAX      128          /* start code + NAL, escaped, upper bound */
#define FWT_H265_NAL_PREFIX_SEI 39

/* SEI flags: the edge flags (FWT_TRIG_F_*, low bits) plus, from the top: */
#define FWT_SEI_F_NO_EDGE (1u << 31)  /* frame had no matching edge; t_ns = T0 + PTS */

extern const uint8_t FWT_SEI_UUID[16];

struct fwt_sei {
	uint16_t version;
	uint32_t session;
	uint32_t flags;
	int64_t  index;
	int64_t  t_ns;
};

/* One NAL unit found in an Annex B byte stream. */
struct fwt_nal {
	size_t sc_off;  /* offset of its start code (3 or 4 bytes) */
	size_t off;     /* offset of the 2-byte NAL header */
	size_t len;     /* NAL bytes from the header, trailing zero bytes excluded */
	int    type;    /* nal_unit_type */
};

/*
 * Build the SEI NAL unit with a 4-byte start code into out.
 * Returns its length, or 0 if cap is too small.
 */
size_t fwt_sei_build(const struct fwt_sei *s, uint8_t *out, size_t cap);

/*
 * Parse one NAL unit (from its header, without start code). Returns true if it
 * is a prefix SEI containing our message; fills *s.
 */
bool fwt_sei_parse_nal(const uint8_t *nal, size_t len, struct fwt_sei *s);

/*
 * Iterate the NAL units of an Annex B buffer. Start with *pos = 0.
 * Returns false when no further NAL unit exists.
 */
bool fwt_nal_next(const uint8_t *buf, size_t len, size_t *pos, struct fwt_nal *n);

/*
 * Where to insert the SEI in one access unit: the start-code offset of the
 * first picture (VCL) NAL unit, i.e. after VPS/SPS/PPS/AUD. Returns false if
 * the buffer holds no picture data.
 */
bool fwt_sei_insert_offset(const uint8_t *au, size_t len, size_t *off);

/* Emulation prevention: escape RBSP -> EBSP, unescape EBSP -> RBSP.
 * Return the output length, or 0 if cap is too small. */
size_t fwt_ebsp_escape(const uint8_t *in, size_t len, uint8_t *out, size_t cap);
size_t fwt_ebsp_unescape(const uint8_t *in, size_t len, uint8_t *out, size_t cap);

#endif
