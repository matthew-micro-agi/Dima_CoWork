/*
 * Manifest: the recorder's append-only change log on eMMC (DL-06, DL-19).
 *
 * Fixed 128-byte records: 24-byte header, 96-byte type-specific body,
 * 4 reserved, CRC-32 over the first 124 bytes. A record half-written at power
 * loss fails its CRC; loading stops there and the tail is cut off.
 * Host byte order (both laptop and board are little-endian; asserted).
 */
#ifndef REC_MANIFEST_H
#define REC_MANIFEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define REC_MAGIC    0x4d525746u  /* "FWRM" in memory */
#define REC_VERSION  1
#define REC_SIZE     128
#define REC_MAX_KEYS 8
#define REC_MANIFEST_NAME "manifest.bin"

enum rec_type {
	REC_SESSION_START = 1,
	REC_SESSION_END   = 2,
	REC_SEGMENT       = 3,  /* a segment file was closed (complete) */
	REC_DELETE        = 4,  /* a segment file is being removed (ring) */
	REC_LOCK          = 5,
	REC_UNLOCK        = 6,
};

enum rec_status {
	REC_ST_RECORDING   = 0,  /* no SESSION_END yet */
	REC_ST_COMPLETE    = 1,
	REC_ST_INTERRUPTED = 2,  /* ended by recovery after a crash or power loss */
};

/* Segment flags */
#define REC_SEG_F_RECOVERED (1u << 0)  /* rebuilt from its SEIs at start */
#define REC_SEG_F_KEYS_FULL (1u << 1)  /* more keyframes than REC_MAX_KEYS */

struct rec_seg_info {
	int64_t  first_idx, last_idx;  /* trigger indices of first/last frame */
	int64_t  t_first, t_last;      /* their edge times, ns */
	uint64_t bytes;
	uint16_t frames, missing;      /* frames in the file, indices missing inside */
	uint16_t n_keys, flags;
	uint32_t key_off[REC_MAX_KEYS];   /* byte offset of each keyframe access unit */
	uint16_t key_didx[REC_MAX_KEYS];  /* its index - first_idx */
};

struct rec_lock_info {
	int64_t from_idx, to_idx;  /* inclusive; may reach into the future */
	int64_t until_wall_ns;     /* expiry, CLOCK_REALTIME */
	char    reason[72];
};

_Static_assert(sizeof(struct rec_seg_info) == 96, "segment body must be 96 bytes");
_Static_assert(sizeof(struct rec_lock_info) == 96, "lock body must be 96 bytes");

struct rec_record {
	uint32_t magic;
	uint16_t version;
	uint16_t type;
	uint64_t seq;       /* record number in the manifest, from 1 */
	uint32_t session;
	uint32_t reserved;
	union {
		struct {
			int64_t t0_ns, period_ns;
			int64_t wall_ns;  /* CLOCK_REALTIME at start */
		} start;
		struct {
			uint32_t status, reserved;
			int64_t  last_index;
			uint64_t frames, missing;
			int64_t  wall_ns;
		} end;
		struct rec_seg_info seg;
		struct {
			int64_t first_idx;  /* identifies the segment within the session */
		} del;
		struct rec_lock_info lock;
		struct {
			uint64_t lock_seq;  /* seq of the LOCK record */
		} unlock;
		uint8_t raw[96];
	} u;
	uint32_t reserved2;
	uint32_t crc;
};

_Static_assert(sizeof(struct rec_record) == REC_SIZE, "manifest record must be 128 bytes");
_Static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "manifest is little-endian");

uint32_t rec_crc32(const void *buf, size_t len);

/* Initialise a record of a type (magic, version, type, session). */
void rec_record_init(struct rec_record *r, uint16_t type, uint32_t session);

/* Set the CRC; check magic, version and CRC. */
void rec_record_seal(struct rec_record *r);
bool rec_record_valid(const struct rec_record *r);

/*
 * Read all valid records of an open manifest fd from the start. Stops at the
 * first short or invalid record. Returns the number of valid records (array
 * malloc'ed in *out, may be NULL if 0), or -errno. *valid_bytes = their length.
 */
long rec_manifest_read(int fd, struct rec_record **out, size_t *valid_bytes);

/* Append one sealed record and fdatasync. 0 or -errno. */
int rec_manifest_append(int fd, struct rec_record *r);

const char *rec_type_str(uint16_t type);
const char *rec_status_str(uint32_t status);

#endif
