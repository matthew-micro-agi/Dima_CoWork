#include "ctl_node.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "icd_decode.h"
#include "icd_encode.h"

static void str_copy(char *dst, size_t cap, const struct zcbor_string *s)
{
	size_t n = s->len < cap - 1 ? s->len : cap - 1;

	memcpy(dst, s->value, n);
	dst[n] = '\0';
}

int ctl_enc_arm_req(uint32_t session, uint32_t fps, uint8_t *buf, size_t cap, size_t *len)
{
	struct arm_req r = { .arm_req_session = session, .arm_req_fps = fps };

	return cbor_encode_arm_req(buf, cap, &r, len) == ZCBOR_SUCCESS ? 0 : -ENOBUFS;
}

int ctl_dec_identity(const uint8_t *b, size_t n, struct ctl_identity *out)
{
	struct identity d;

	if (cbor_decode_identity(b, n, &d, NULL) != ZCBOR_SUCCESS)
		return -EBADMSG;
	str_copy(out->node, sizeof(out->node), &d.identity_node);
	str_copy(out->version, sizeof(out->version), &d.identity_version);
	out->fps_min = (uint32_t)d.identity_fps_min;
	out->fps_max = (uint32_t)d.identity_fps_max;
	out->width = (uint32_t)d.identity_width;
	out->height = (uint32_t)d.identity_height;
	return 0;
}

int ctl_dec_health(const uint8_t *b, size_t n, struct ctl_health *out)
{
	struct health d;

	if (cbor_decode_health(b, n, &d, NULL) != ZCBOR_SUCCESS)
		return -EBADMSG;
	str_copy(out->state, sizeof(out->state), &d.health_state);
	out->session = (uint32_t)d.health_session;
	out->ring_used = d.health_ring_used;
	out->ring_locked = d.health_ring_locked;
	out->ring_free = d.health_ring_free;
	out->depth_s = d.health_depth_s;
	out->frames = d.health_frames;
	out->missing = d.health_missing;
	out->temp_mc = d.health_temp_mc;
	out->phc_ns = d.health_phc_ns;
	out->time_verified = d.health_time_verified;
	str_copy(out->message, sizeof(out->message), &d.health_message);
	return 0;
}

int ctl_dec_arm_rep(const uint8_t *b, size_t n, struct ctl_arm *out)
{
	struct arm_rep d;

	if (cbor_decode_arm_rep(b, n, &d, NULL) != ZCBOR_SUCCESS)
		return -EBADMSG;
	out->session = (uint32_t)d.arm_rep_session;
	out->t0 = d.arm_rep_t0;
	out->period = d.arm_rep_period;
	return 0;
}

int ctl_dec_session_rep(const uint8_t *b, size_t n, uint32_t *session)
{
	struct session_rep d;

	if (cbor_decode_session_rep(b, n, &d, NULL) != ZCBOR_SUCCESS)
		return -EBADMSG;
	*session = (uint32_t)d.session_rep_session;
	return 0;
}

int ctl_dec_error(const uint8_t *b, size_t n, struct ctl_error *out)
{
	struct error d;

	if (cbor_decode_error(b, n, &d, NULL) != ZCBOR_SUCCESS)
		return -EBADMSG;
	str_copy(out->code, sizeof(out->code), &d.error_code);
	str_copy(out->message, sizeof(out->message), &d.error_message);
	return 0;
}

int ctl_dec_sessions_find(const uint8_t *b, size_t n, uint32_t session, struct ctl_session *out,
			  int *found, uint32_t *last, bool *more)
{
	struct sessions_rep *d = malloc(sizeof(*d));  /* 512 entries: too big for the stack */
	int r = -EBADMSG;

	if (!d)
		return -ENOMEM;
	*found = 0;
	if (cbor_decode_sessions_rep(b, n, d, NULL) == ZCBOR_SUCCESS) {
		size_t cnt = d->sessions_rep_sessions_session_info_m_count;

		*more = d->sessions_rep_more;
		*last = cnt ? (uint32_t)d->sessions_rep_sessions_session_info_m[cnt - 1]
					.session_info_session : 0;
		for (size_t k = 0; k < cnt; k++) {
			const struct session_info *s = &d->sessions_rep_sessions_session_info_m[k];

			if (s->session_info_session != session)
				continue;
			*out = (struct ctl_session){
				.session = (uint32_t)s->session_info_session,
				.start_wall = s->session_info_start_wall,
				.t0 = s->session_info_t0,
				.period = s->session_info_period,
				.first_idx = s->session_info_first_idx,
				.last_idx = s->session_info_last_idx,
				.frames = s->session_info_frames,
				.missing = s->session_info_missing,
				.segments = s->session_info_segments,
				.bytes = s->session_info_bytes,
				.truncated = s->session_info_truncated,
			};
			str_copy(out->status, sizeof(out->status), &s->session_info_status);
			*found = 1;
		}
		r = 0;
	}
	free(d);
	return r;
}

int ctl_dec_segments(const uint8_t *b, size_t n, struct ctl_segment *out, size_t cap,
		     size_t *count, bool *more)
{
	struct segments_rep *d = malloc(sizeof(*d));
	int r = -EBADMSG;

	if (!d)
		return -ENOMEM;
	*count = 0;
	if (cbor_decode_segments_rep(b, n, d, NULL) == ZCBOR_SUCCESS) {
		size_t cnt = d->segments_rep_segments_segment_info_m_count;

		*more = d->segments_rep_more;
		for (size_t k = 0; k < cnt && *count < cap; k++) {
			const struct segment_info *s = &d->segments_rep_segments_segment_info_m[k];

			out[(*count)++] = (struct ctl_segment){
				.first_idx = s->segment_info_first_idx,
				.last_idx = s->segment_info_last_idx,
				.t_first = s->segment_info_t_first,
				.t_last = s->segment_info_t_last,
				.bytes = s->segment_info_bytes,
				.frames = s->segment_info_frames,
				.missing = s->segment_info_missing,
			};
		}
		if (cnt > cap)
			*more = true;  /* caller's buffer full: ask again from the next one */
		r = 0;
	}
	free(d);
	return r;
}
