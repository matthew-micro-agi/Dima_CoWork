/*
 * Generated using zcbor version 0.9.1
 * https://github.com/NordicSemiconductor/zcbor
 * Generated with a --default-max-qty of 512
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "zcbor_decode.h"
#include "icd_decode.h"
#include "zcbor_print.h"

#if DEFAULT_MAX_QTY != 512
#error "The type file was generated with a different default_max_qty than this file"
#endif

#define log_result(state, result, func) do { \
	if (!result) { \
		zcbor_trace_file(state); \
		zcbor_log("%s error: %s\r\n", func, zcbor_error_str(zcbor_peek_error(state))); \
	} else { \
		zcbor_log("%s success\r\n", func); \
	} \
} while(0)

static bool decode_repeated_arm_req_t0(zcbor_state_t *state, struct arm_req_t0 *result);
static bool decode_repeated_lock_req_reason(zcbor_state_t *state, struct lock_req_reason *result);
static bool decode_session_info(zcbor_state_t *state, struct session_info *result);
static bool decode_segment_info(zcbor_state_t *state, struct segment_info *result);
static bool decode_unlock_req(zcbor_state_t *state, struct unlock_req *result);
static bool decode_lock_rep(zcbor_state_t *state, struct lock_rep *result);
static bool decode_lock_req(zcbor_state_t *state, struct lock_req *result);
static bool decode_segments_rep(zcbor_state_t *state, struct segments_rep *result);
static bool decode_sessions_rep(zcbor_state_t *state, struct sessions_rep *result);
static bool decode_session_rep(zcbor_state_t *state, struct session_rep *result);
static bool decode_arm_rep(zcbor_state_t *state, struct arm_rep *result);
static bool decode_arm_req(zcbor_state_t *state, struct arm_req *result);
static bool decode_health(zcbor_state_t *state, struct health *result);
static bool decode_identity(zcbor_state_t *state, struct identity *result);
static bool decode_ok_rep(zcbor_state_t *state, void *result);
static bool decode_error(zcbor_state_t *state, struct error *result);


static bool decode_repeated_arm_req_t0(
		zcbor_state_t *state, struct arm_req_t0 *result)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = ((((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"t0", tmp_str.len = sizeof("t0") - 1, &tmp_str)))))
	&& (zcbor_int64_decode(state, (&(*result).arm_req_t0)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_repeated_lock_req_reason(
		zcbor_state_t *state, struct lock_req_reason *result)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = ((((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"reason", tmp_str.len = sizeof("reason") - 1, &tmp_str)))))
	&& (zcbor_tstr_decode(state, (&(*result).lock_req_reason)))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_session_info(
		zcbor_state_t *state, struct session_info *result)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"session", tmp_str.len = sizeof("session") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).session_info_session))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"status", tmp_str.len = sizeof("status") - 1, &tmp_str)))))
	&& (zcbor_tstr_decode(state, (&(*result).session_info_status))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"start_wall", tmp_str.len = sizeof("start_wall") - 1, &tmp_str)))))
	&& (zcbor_int64_decode(state, (&(*result).session_info_start_wall))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"t0", tmp_str.len = sizeof("t0") - 1, &tmp_str)))))
	&& (zcbor_int64_decode(state, (&(*result).session_info_t0))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"period", tmp_str.len = sizeof("period") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).session_info_period))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"first_idx", tmp_str.len = sizeof("first_idx") - 1, &tmp_str)))))
	&& (zcbor_int64_decode(state, (&(*result).session_info_first_idx))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"last_idx", tmp_str.len = sizeof("last_idx") - 1, &tmp_str)))))
	&& (zcbor_int64_decode(state, (&(*result).session_info_last_idx))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"frames", tmp_str.len = sizeof("frames") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).session_info_frames))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"missing", tmp_str.len = sizeof("missing") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).session_info_missing))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"segments", tmp_str.len = sizeof("segments") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).session_info_segments))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"bytes", tmp_str.len = sizeof("bytes") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).session_info_bytes))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"truncated", tmp_str.len = sizeof("truncated") - 1, &tmp_str)))))
	&& (zcbor_bool_decode(state, (&(*result).session_info_truncated))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_segment_info(
		zcbor_state_t *state, struct segment_info *result)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"first_idx", tmp_str.len = sizeof("first_idx") - 1, &tmp_str)))))
	&& (zcbor_int64_decode(state, (&(*result).segment_info_first_idx))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"last_idx", tmp_str.len = sizeof("last_idx") - 1, &tmp_str)))))
	&& (zcbor_int64_decode(state, (&(*result).segment_info_last_idx))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"t_first", tmp_str.len = sizeof("t_first") - 1, &tmp_str)))))
	&& (zcbor_int64_decode(state, (&(*result).segment_info_t_first))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"t_last", tmp_str.len = sizeof("t_last") - 1, &tmp_str)))))
	&& (zcbor_int64_decode(state, (&(*result).segment_info_t_last))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"bytes", tmp_str.len = sizeof("bytes") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).segment_info_bytes))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"frames", tmp_str.len = sizeof("frames") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).segment_info_frames))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"missing", tmp_str.len = sizeof("missing") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).segment_info_missing))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"keys", tmp_str.len = sizeof("keys") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).segment_info_keys))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"locked", tmp_str.len = sizeof("locked") - 1, &tmp_str)))))
	&& (zcbor_bool_decode(state, (&(*result).segment_info_locked))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"recovered", tmp_str.len = sizeof("recovered") - 1, &tmp_str)))))
	&& (zcbor_bool_decode(state, (&(*result).segment_info_recovered))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_unlock_req(
		zcbor_state_t *state, struct unlock_req *result)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"lock_id", tmp_str.len = sizeof("lock_id") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).unlock_req_lock_id))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_lock_rep(
		zcbor_state_t *state, struct lock_rep *result)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"icd", tmp_str.len = sizeof("icd") - 1, &tmp_str)))))
	&& (zcbor_uint64_expect(state, (1))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"lock_id", tmp_str.len = sizeof("lock_id") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).lock_rep_lock_id))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_lock_req(
		zcbor_state_t *state, struct lock_req *result)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"session", tmp_str.len = sizeof("session") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).lock_req_session))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"from_idx", tmp_str.len = sizeof("from_idx") - 1, &tmp_str)))))
	&& (zcbor_int64_decode(state, (&(*result).lock_req_from_idx))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"to_idx", tmp_str.len = sizeof("to_idx") - 1, &tmp_str)))))
	&& (zcbor_int64_decode(state, (&(*result).lock_req_to_idx))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"ttl_s", tmp_str.len = sizeof("ttl_s") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).lock_req_ttl_s))))
	&& zcbor_present_decode(&((*result).lock_req_reason_present), (zcbor_decoder_t *)decode_repeated_lock_req_reason, state, (&(*result).lock_req_reason))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_lock_req_reason(state, (&(*result).lock_req_reason));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_segments_rep(
		zcbor_state_t *state, struct segments_rep *result)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"icd", tmp_str.len = sizeof("icd") - 1, &tmp_str)))))
	&& (zcbor_uint64_expect(state, (1))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"session", tmp_str.len = sizeof("session") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).segments_rep_session))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"segments", tmp_str.len = sizeof("segments") - 1, &tmp_str)))))
	&& (zcbor_list_start_decode(state) && ((zcbor_multi_decode(0, 512, &(*result).segments_rep_segments_segment_info_m_count, (zcbor_decoder_t *)decode_segment_info, state, (*&(*result).segments_rep_segments_segment_info_m), sizeof(struct segment_info))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state)))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"more", tmp_str.len = sizeof("more") - 1, &tmp_str)))))
	&& (zcbor_bool_decode(state, (&(*result).segments_rep_more))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_segment_info(state, (*&(*result).segments_rep_segments_segment_info_m));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_sessions_rep(
		zcbor_state_t *state, struct sessions_rep *result)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"icd", tmp_str.len = sizeof("icd") - 1, &tmp_str)))))
	&& (zcbor_uint64_expect(state, (1))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"sessions", tmp_str.len = sizeof("sessions") - 1, &tmp_str)))))
	&& (zcbor_list_start_decode(state) && ((zcbor_multi_decode(0, 512, &(*result).sessions_rep_sessions_session_info_m_count, (zcbor_decoder_t *)decode_session_info, state, (*&(*result).sessions_rep_sessions_session_info_m), sizeof(struct session_info))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_list_end_decode(state)))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"more", tmp_str.len = sizeof("more") - 1, &tmp_str)))))
	&& (zcbor_bool_decode(state, (&(*result).sessions_rep_more))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_session_info(state, (*&(*result).sessions_rep_sessions_session_info_m));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_session_rep(
		zcbor_state_t *state, struct session_rep *result)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"icd", tmp_str.len = sizeof("icd") - 1, &tmp_str)))))
	&& (zcbor_uint64_expect(state, (1))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"session", tmp_str.len = sizeof("session") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).session_rep_session))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_arm_rep(
		zcbor_state_t *state, struct arm_rep *result)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"icd", tmp_str.len = sizeof("icd") - 1, &tmp_str)))))
	&& (zcbor_uint64_expect(state, (1))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"session", tmp_str.len = sizeof("session") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).arm_rep_session))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"t0", tmp_str.len = sizeof("t0") - 1, &tmp_str)))))
	&& (zcbor_int64_decode(state, (&(*result).arm_rep_t0))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"period", tmp_str.len = sizeof("period") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).arm_rep_period))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_arm_req(
		zcbor_state_t *state, struct arm_req *result)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"session", tmp_str.len = sizeof("session") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).arm_req_session))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"fps", tmp_str.len = sizeof("fps") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).arm_req_fps))))
	&& zcbor_present_decode(&((*result).arm_req_t0_present), (zcbor_decoder_t *)decode_repeated_arm_req_t0, state, (&(*result).arm_req_t0))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	if (false) {
		/* For testing that the types of the arguments are correct.
		 * A compiler error here means a bug in zcbor.
		 */
		decode_repeated_arm_req_t0(state, (&(*result).arm_req_t0));
	}

	log_result(state, res, __func__);
	return res;
}

static bool decode_health(
		zcbor_state_t *state, struct health *result)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"icd", tmp_str.len = sizeof("icd") - 1, &tmp_str)))))
	&& (zcbor_uint64_expect(state, (1))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"state", tmp_str.len = sizeof("state") - 1, &tmp_str)))))
	&& (zcbor_tstr_decode(state, (&(*result).health_state))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"session", tmp_str.len = sizeof("session") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).health_session))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"ring_used", tmp_str.len = sizeof("ring_used") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).health_ring_used))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"ring_locked", tmp_str.len = sizeof("ring_locked") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).health_ring_locked))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"ring_free", tmp_str.len = sizeof("ring_free") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).health_ring_free))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"depth_s", tmp_str.len = sizeof("depth_s") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).health_depth_s))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"emmc_life_a", tmp_str.len = sizeof("emmc_life_a") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).health_emmc_life_a))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"emmc_life_b", tmp_str.len = sizeof("emmc_life_b") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).health_emmc_life_b))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"emmc_pre_eol", tmp_str.len = sizeof("emmc_pre_eol") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).health_emmc_pre_eol))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"frames", tmp_str.len = sizeof("frames") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).health_frames))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"missing", tmp_str.len = sizeof("missing") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).health_missing))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"temp_mc", tmp_str.len = sizeof("temp_mc") - 1, &tmp_str)))))
	&& (zcbor_int64_decode(state, (&(*result).health_temp_mc))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"phc_ns", tmp_str.len = sizeof("phc_ns") - 1, &tmp_str)))))
	&& (zcbor_int64_decode(state, (&(*result).health_phc_ns))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"time_verified", tmp_str.len = sizeof("time_verified") - 1, &tmp_str)))))
	&& (zcbor_bool_decode(state, (&(*result).health_time_verified))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"message", tmp_str.len = sizeof("message") - 1, &tmp_str)))))
	&& (zcbor_tstr_decode(state, (&(*result).health_message))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_identity(
		zcbor_state_t *state, struct identity *result)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"icd", tmp_str.len = sizeof("icd") - 1, &tmp_str)))))
	&& (zcbor_uint64_expect(state, (1))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"node", tmp_str.len = sizeof("node") - 1, &tmp_str)))))
	&& (zcbor_tstr_decode(state, (&(*result).identity_node))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"version", tmp_str.len = sizeof("version") - 1, &tmp_str)))))
	&& (zcbor_tstr_decode(state, (&(*result).identity_version))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"fps_min", tmp_str.len = sizeof("fps_min") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).identity_fps_min))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"fps_max", tmp_str.len = sizeof("fps_max") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).identity_fps_max))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"width", tmp_str.len = sizeof("width") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).identity_width))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"height", tmp_str.len = sizeof("height") - 1, &tmp_str)))))
	&& (zcbor_uint64_decode(state, (&(*result).identity_height))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_ok_rep(
		zcbor_state_t *state, void *result)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"icd", tmp_str.len = sizeof("icd") - 1, &tmp_str)))))
	&& (zcbor_uint64_expect(state, (1))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}

static bool decode_error(
		zcbor_state_t *state, struct error *result)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_decode(state) && (((((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"icd", tmp_str.len = sizeof("icd") - 1, &tmp_str)))))
	&& (zcbor_uint64_expect(state, (1))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"code", tmp_str.len = sizeof("code") - 1, &tmp_str)))))
	&& (zcbor_tstr_decode(state, (&(*result).error_code))))
	&& (((zcbor_tstr_expect(state, ((tmp_str.value = (uint8_t *)"message", tmp_str.len = sizeof("message") - 1, &tmp_str)))))
	&& (zcbor_tstr_decode(state, (&(*result).error_message))))) || (zcbor_list_map_end_force_decode(state), false)) && zcbor_map_end_decode(state))));

	log_result(state, res, __func__);
	return res;
}



int cbor_decode_error(
		const uint8_t *payload, size_t payload_len,
		struct error *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_error, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_ok_rep(
		const uint8_t *payload, size_t payload_len,
		void *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_ok_rep, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_identity(
		const uint8_t *payload, size_t payload_len,
		struct identity *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_identity, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_health(
		const uint8_t *payload, size_t payload_len,
		struct health *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_health, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_arm_req(
		const uint8_t *payload, size_t payload_len,
		struct arm_req *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_arm_req, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_arm_rep(
		const uint8_t *payload, size_t payload_len,
		struct arm_rep *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_arm_rep, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_session_rep(
		const uint8_t *payload, size_t payload_len,
		struct session_rep *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_session_rep, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_sessions_rep(
		const uint8_t *payload, size_t payload_len,
		struct sessions_rep *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[5];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_sessions_rep, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_segments_rep(
		const uint8_t *payload, size_t payload_len,
		struct segments_rep *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[5];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_segments_rep, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_lock_req(
		const uint8_t *payload, size_t payload_len,
		struct lock_req *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_lock_req, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_lock_rep(
		const uint8_t *payload, size_t payload_len,
		struct lock_rep *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_lock_rep, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_decode_unlock_req(
		const uint8_t *payload, size_t payload_len,
		struct unlock_req *result,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)result, payload_len_out, states,
		(zcbor_decoder_t *)decode_unlock_req, sizeof(states) / sizeof(zcbor_state_t), 1);
}
