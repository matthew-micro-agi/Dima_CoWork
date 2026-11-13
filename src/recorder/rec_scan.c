#include "rec_scan.h"

#include <stdio.h>
#include <stdlib.h>

long rec_scan(const uint8_t *buf, size_t len, struct rec_pic **pics)
{
	size_t n = 0, cap = 0, pos = 0;
	struct rec_pic *v = NULL, cur = { 0 };
	struct fwt_nal nal;
	bool in_au = false, prev_vcl = false;

	*pics = NULL;
	while (fwt_nal_next(buf, len, &pos, &nal)) {
		bool vcl = nal.type >= 0 && nal.type < 32;

		/* A non-VCL NAL after picture data starts the next access unit. */
		if (!in_au || (!vcl && prev_vcl)) {
			cur = (struct rec_pic){ .au_off = nal.sc_off };
			in_au = true;
		}
		if (nal.type == FWT_H265_NAL_PREFIX_SEI) {
			struct fwt_sei s;

			if (fwt_sei_parse_nal(buf + nal.off, nal.len, &s)) {
				cur.sei = s;
				cur.has_sei = true;
			}
		}
		if (vcl && nal.len > 2 && (buf[nal.off + 2] & 0x80)) {  /* first slice */
			if (n == cap) {
				cap = cap ? cap * 2 : 256;
				struct rec_pic *nv = realloc(v, cap * sizeof(*v));

				if (!nv) {
					free(v);
					return -1;
				}
				v = nv;
			}
			cur.key = nal.type >= 16 && nal.type <= 23;
			v[n++] = cur;
		}
		prev_vcl = vcl;
	}
	*pics = v;
	return (long)n;
}

uint8_t *rec_read_file(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	uint8_t *buf = NULL;
	long n;

	*len = 0;
	if (!f)
		return NULL;
	if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0) {
		buf = malloc((size_t)n + 1);
		if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
			free(buf);
			buf = NULL;
		} else {
			*len = (size_t)n;
		}
	}
	fclose(f);
	return buf;
}
