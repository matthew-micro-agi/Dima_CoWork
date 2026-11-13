/*
 * fwt-rec's sidecar file (<out>.json): what a .h265 alone doesn't say: which
 * node and software recorded it, when, with which plan, what was missing and
 * whether the check passed. Pure: writes to a FILE, unit-tested.
 */
#ifndef REC_JSON_H
#define REC_JSON_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ctl_node.h"

struct rec_result {
	const char *tool_version;
	const char *addr;            /* as given: host[:port] */
	struct ctl_identity id;
	uint32_t session;
	uint32_t fps;
	struct ctl_arm arm;
	double   requested_s;
	bool     interrupted;        /* Ctrl-C before the duration */
	char     started_utc[32];    /* laptop's clock, ISO 8601 */
	struct ctl_session rec;      /* the session as the node lists it after disarm */
	const char *out_path;
	uint64_t file_bytes;
	uint64_t file_frames;        /* frames in the fetched segments */
	unsigned fetches;
	char     missing_ranges[600];
	uint64_t missing_inside;
	const char *check;           /* "passed" | "failed" | "not run" */
	char     check_summary[512];
};

void rec_json_write(FILE *f, const struct rec_result *r);

/* JSON string with escapes ("..."), for any bytes the node sent us. */
void rec_json_str(FILE *f, const char *s);

#endif
