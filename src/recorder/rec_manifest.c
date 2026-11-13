#include "rec_manifest.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

uint32_t rec_crc32(const void *buf, size_t len)
{
	const uint8_t *p = buf;
	uint32_t c = 0xffffffffu;

	for (size_t i = 0; i < len; i++) {
		c ^= p[i];
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
	}
	return ~c;
}

void rec_record_init(struct rec_record *r, uint16_t type, uint32_t session)
{
	memset(r, 0, sizeof(*r));
	r->magic = REC_MAGIC;
	r->version = REC_VERSION;
	r->type = type;
	r->session = session;
}

void rec_record_seal(struct rec_record *r)
{
	r->crc = rec_crc32(r, offsetof(struct rec_record, crc));
}

bool rec_record_valid(const struct rec_record *r)
{
	return r->magic == REC_MAGIC && r->version == REC_VERSION &&
	       r->type >= REC_SESSION_START && r->type <= REC_UNLOCK &&
	       r->crc == rec_crc32(r, offsetof(struct rec_record, crc));
}

long rec_manifest_read(int fd, struct rec_record **out, size_t *valid_bytes)
{
	size_t n = 0, cap = 0;
	struct rec_record *v = NULL, r;

	*out = NULL;
	*valid_bytes = 0;
	if (lseek(fd, 0, SEEK_SET) < 0)
		return -errno;
	for (;;) {
		ssize_t got = 0;

		while (got < (ssize_t)sizeof(r)) {
			ssize_t k = read(fd, (uint8_t *)&r + got, sizeof(r) - (size_t)got);

			if (k < 0 && errno == EINTR)
				continue;
			if (k < 0) {
				free(v);
				return -errno;
			}
			if (k == 0)
				break;
			got += k;
		}
		if (got < (ssize_t)sizeof(r) || !rec_record_valid(&r))
			break;  /* end, or a torn / corrupt record: stop here */
		if (n == cap) {
			cap = cap ? cap * 2 : 256;
			struct rec_record *nv = realloc(v, cap * sizeof(*v));

			if (!nv) {
				free(v);
				return -ENOMEM;
			}
			v = nv;
		}
		v[n++] = r;
	}
	*out = v;
	*valid_bytes = n * sizeof(r);
	return (long)n;
}

int rec_manifest_append(int fd, struct rec_record *r)
{
	rec_record_seal(r);

	size_t done = 0;

	while (done < sizeof(*r)) {
		ssize_t k = write(fd, (const uint8_t *)r + done, sizeof(*r) - done);

		if (k < 0 && errno == EINTR)
			continue;
		if (k < 0)
			return -errno;
		done += (size_t)k;
	}
	return fdatasync(fd) == 0 ? 0 : -errno;
}

const char *rec_type_str(uint16_t type)
{
	switch (type) {
	case REC_SESSION_START: return "session-start";
	case REC_SESSION_END:   return "session-end";
	case REC_SEGMENT:       return "segment";
	case REC_DELETE:        return "delete";
	case REC_LOCK:          return "lock";
	case REC_UNLOCK:        return "unlock";
	}
	return "?";
}

const char *rec_status_str(uint32_t status)
{
	switch (status) {
	case REC_ST_RECORDING:   return "recording";
	case REC_ST_COMPLETE:    return "complete";
	case REC_ST_INTERRUPTED: return "interrupted";
	}
	return "?";
}
