/* Unit tests for fwt-rec's protocol layer (ctl_node) and sidecar (rec_json). */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ctl_node.h"
#include "rec_json.h"
#include "test_fwtrec_vectors.h"

static int failures;

#define CHECK(cond)                                                          \
	do {                                                                 \
		if (!(cond)) {                                               \
			fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__,     \
				__LINE__, #cond);                            \
			failures++;                                          \
		}                                                            \
	} while (0)

/*
 * Our request = cbor2's for the same request: same entries in ICD order. zcbor
 * writes a map with optional keys (arm-req's t0) as indefinite length
 * (bf ... ff), cbor2 as definite (a2 ...): both valid CBOR (RFC 8949 3.2.2),
 * both decoded by either side.
 */
static void test_arm_req(void)
{
	uint8_t buf[64];
	size_t len = 0;

	CHECK(ctl_enc_arm_req(12345, 30, buf, sizeof(buf), &len) == 0);
	CHECK(len == sizeof(arm_req_12345_30) + 1);
	CHECK(buf[0] == 0xbf && buf[len - 1] == 0xff && arm_req_12345_30[0] == 0xa2);
	CHECK(!memcmp(buf + 1, arm_req_12345_30 + 1, sizeof(arm_req_12345_30) - 1));
	CHECK(ctl_enc_arm_req(12345, 30, buf, 4, &len) == -ENOBUFS);
}

static void test_identity(void)
{
	struct ctl_identity id;

	CHECK(ctl_dec_identity(identity_ok, sizeof(identity_ok), &id) == 0);
	CHECK(!strcmp(id.node, "j722s-evm") && !strcmp(id.version, "0.1.0-gabc"));
	CHECK(id.fps_min == 1 && id.fps_max == 60 && id.width == 1920 && id.height == 1200);

	/* a name longer than our buffer is cut, never overflows */
	CHECK(ctl_dec_identity(identity_long, sizeof(identity_long), &id) == 0);
	CHECK(strlen(id.node) == sizeof(id.node) - 1 && id.node[0] == 'n');

	/* another message, or cut short: not an identity */
	CHECK(ctl_dec_identity(error_busy, sizeof(error_busy), &id) == -EBADMSG);
	CHECK(ctl_dec_identity(identity_ok, sizeof(identity_ok) - 3, &id) == -EBADMSG);
}

static void test_health_arm_error(void)
{
	struct ctl_health h;
	struct ctl_arm a;
	struct ctl_error e;
	uint32_t s;

	CHECK(ctl_dec_health(health_rec, sizeof(health_rec), &h) == 0);
	CHECK(!strcmp(h.state, "recording") && h.session == 77 && h.frames == 900 &&
	      h.missing == 2 && h.temp_mc == -1500 && h.phc_ns == 1794578203500000000LL &&
	      !h.time_verified && h.ring_free == 5000 && h.message[0] == '\0');
	CHECK(h.emmc_life_a == 1 && h.emmc_life_b == 1 && h.emmc_pre_eol == 1 && h.depth_s == 3600);

	CHECK(ctl_dec_arm_rep(arm_rep_ok, sizeof(arm_rep_ok), &a) == 0);
	CHECK(a.session == 77 && a.t0 == 1794578204000000000LL && a.period == 33333333);

	CHECK(ctl_dec_error(error_busy, sizeof(error_busy), &e) == 0);
	CHECK(!strcmp(e.code, "busy") && !strcmp(e.message, "already recording session 76"));

	CHECK(ctl_dec_session_rep(arm_rep_ok, sizeof(arm_rep_ok), &s) == -EBADMSG);
}

static void test_sessions(void)
{
	struct ctl_session s;
	int found;
	uint32_t last;
	bool more;

	CHECK(ctl_dec_sessions_find(sessions_two, sizeof(sessions_two), 77, &s, &found, &last,
				    &more) == 0);
	CHECK(found == 1 && s.session == 77 && !strcmp(s.status, "recording"));
	CHECK(s.first_idx == 0 && s.last_idx == 299 && s.frames == 300 && s.period == 33333333);
	CHECK(last == 77 && more);

	CHECK(ctl_dec_sessions_find(sessions_two, sizeof(sessions_two), 5, &s, &found, &last,
				    &more) == 0);
	CHECK(found == 0 && last == 77);
}

static void test_segments(void)
{
	struct ctl_segment seg[4];
	size_t n;
	bool more;

	CHECK(ctl_dec_segments(segments_three, sizeof(segments_three), seg, 4, &n, &more) == 0);
	CHECK(n == 3 && !more);
	CHECK(seg[0].first_idx == 0 && seg[0].last_idx == 119 && seg[2].last_idx == 299);
	CHECK(seg[2].frames == 60);

	/* caller's buffer smaller than the page: what fits, and "more" */
	CHECK(ctl_dec_segments(segments_three, sizeof(segments_three), seg, 2, &n, &more) == 0);
	CHECK(n == 2 && more && seg[1].last_idx == 239);
}

/* The sidecar is valid JSON even with odd characters from the node. */
static void test_json(void)
{
	struct rec_result r = { .tool_version = "0.1.0-gx", .addr = "10.0.0.1:8080",
				.session = 77, .fps = 30, .requested_s = 2.5,
				.out_path = "/tmp/a \"b\".h265", .check = "passed",
				.file_frames = 75, .fetches = 1 };
	char *buf = NULL;
	size_t len = 0;
	FILE *f = open_memstream(&buf, &len);

	snprintf(r.id.node, sizeof(r.id.node), "node\"x\\y\n\x01");
	snprintf(r.rec.status, sizeof(r.rec.status), "complete");
	snprintf(r.missing_ranges, sizeof(r.missing_ranges), "none");
	snprintf(r.check_summary, sizeof(r.check_summary), "summary: pictures 75");
	rec_json_write(f, &r);
	fclose(f);

	CHECK(strstr(buf, "\"name\": \"node\\\"x\\\\y\\n\\u0001\"") != NULL);
	CHECK(strstr(buf, "\"path\": \"/tmp/a \\\"b\\\".h265\"") != NULL);
	CHECK(strstr(buf, "\"requested_s\": 2.5,") != NULL);
	CHECK(strstr(buf, "\"interrupted\": false") != NULL);
	CHECK(buf[0] == '{' && buf[len - 2] == '}');
	free(buf);
}

int main(void)
{
	test_arm_req();
	test_identity();
	test_health_arm_error();
	test_sessions();
	test_segments();
	test_json();
	if (failures) {
		fprintf(stderr, "test_fwtrec: %d failure(s)\n", failures);
		return 1;
	}
	printf("test_fwtrec: all passed\n");
	return 0;
}
