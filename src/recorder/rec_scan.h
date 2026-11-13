/*
 * Scan a segment's bytes: pictures, their SEIs and keyframes. Used by
 * recovery (rebuild a segment record from the file) and by rec-tool verify.
 */
#ifndef REC_SCAN_H
#define REC_SCAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fwt_sei.h"

struct rec_pic {
	size_t au_off;       /* first byte of its access unit (params, SEI, slices) */
	bool   key;          /* IRAP picture */
	bool   has_sei;
	struct fwt_sei sei;  /* valid if has_sei */
};

/*
 * Walk the access units of buf. Returns the number of pictures; *pics is
 * malloc'ed (NULL if none). -1 on out of memory.
 */
long rec_scan(const uint8_t *buf, size_t len, struct rec_pic **pics);

/* Read a whole file into memory. NULL on error; *len set. */
uint8_t *rec_read_file(const char *path, size_t *len);

#endif
