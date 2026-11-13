/*
 * Generated using zcbor version 0.9.1
 * https://github.com/NordicSemiconductor/zcbor
 * Generated with a --default-max-qty of 512
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "zcbor_encode.h"
#include "icd_encode.h"
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

static bool encode_repeated_arm_req_t0(zcbor_state_t *state, const struct arm_req_t0 *input);
static bool encode_repeated_lock_req_reason(zcbor_state_t *state, const struct lock_req_reason *input);
static bool encode_session_info(zcbor_state_t *state, const struct session_info *input);
static bool encode_segment_info(zcbor_state_t *state, const struct segment_info *input);
static bool encode_unlock_req(zcbor_state_t *state, const struct unlock_req *input);
static bool encode_lock_rep(zcbor_state_t *state, const struct lock_rep *input);
static bool encode_lock_req(zcbor_state_t *state, const struct lock_req *input);
static bool encode_segments_rep(zcbor_state_t *state, const struct segments_rep *input);
static bool encode_sessions_rep(zcbor_state_t *state, const struct sessions_rep *input);
static bool encode_session_rep(zcbor_state_t *state, const struct session_rep *input);
static bool encode_arm_rep(zcbor_state_t *state, const struct arm_rep *input);
static bool encode_arm_req(zcbor_state_t *state, const struct arm_req *input);
static bool encode_health(zcbor_state_t *state, const struct health *input);
static bool encode_identity(zcbor_state_t *state, const struct identity *input);
static bool encode_ok_rep(zcbor_state_t *state, const void *input);
static bool encode_error(zcbor_state_t *state, const struct error *input);


static bool encode_repeated_arm_req_t0(
		zcbor_state_t *state, const struct arm_req_t0 *input)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = ((((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"t0", tmp_str.len = sizeof("t0") - 1, &tmp_str)))))
	&& (zcbor_int64_encode(state, (&(*input).arm_req_t0)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_repeated_lock_req_reason(
		zcbor_state_t *state, const struct lock_req_reason *input)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = ((((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"reason", tmp_str.len = sizeof("reason") - 1, &tmp_str)))))
	&& (zcbor_tstr_encode(state, (&(*input).lock_req_reason)))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_session_info(
		zcbor_state_t *state, const struct session_info *input)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_encode(state, 12) && (((((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"session", tmp_str.len = sizeof("session") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).session_info_session))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"status", tmp_str.len = sizeof("status") - 1, &tmp_str)))))
	&& (zcbor_tstr_encode(state, (&(*input).session_info_status))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"start_wall", tmp_str.len = sizeof("start_wall") - 1, &tmp_str)))))
	&& (zcbor_int64_encode(state, (&(*input).session_info_start_wall))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"t0", tmp_str.len = sizeof("t0") - 1, &tmp_str)))))
	&& (zcbor_int64_encode(state, (&(*input).session_info_t0))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"period", tmp_str.len = sizeof("period") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).session_info_period))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"first_idx", tmp_str.len = sizeof("first_idx") - 1, &tmp_str)))))
	&& (zcbor_int64_encode(state, (&(*input).session_info_first_idx))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"last_idx", tmp_str.len = sizeof("last_idx") - 1, &tmp_str)))))
	&& (zcbor_int64_encode(state, (&(*input).session_info_last_idx))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"frames", tmp_str.len = sizeof("frames") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).session_info_frames))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"missing", tmp_str.len = sizeof("missing") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).session_info_missing))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"segments", tmp_str.len = sizeof("segments") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).session_info_segments))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"bytes", tmp_str.len = sizeof("bytes") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).session_info_bytes))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"truncated", tmp_str.len = sizeof("truncated") - 1, &tmp_str)))))
	&& (zcbor_bool_encode(state, (&(*input).session_info_truncated))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 12))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_segment_info(
		zcbor_state_t *state, const struct segment_info *input)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_encode(state, 10) && (((((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"first_idx", tmp_str.len = sizeof("first_idx") - 1, &tmp_str)))))
	&& (zcbor_int64_encode(state, (&(*input).segment_info_first_idx))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"last_idx", tmp_str.len = sizeof("last_idx") - 1, &tmp_str)))))
	&& (zcbor_int64_encode(state, (&(*input).segment_info_last_idx))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"t_first", tmp_str.len = sizeof("t_first") - 1, &tmp_str)))))
	&& (zcbor_int64_encode(state, (&(*input).segment_info_t_first))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"t_last", tmp_str.len = sizeof("t_last") - 1, &tmp_str)))))
	&& (zcbor_int64_encode(state, (&(*input).segment_info_t_last))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"bytes", tmp_str.len = sizeof("bytes") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).segment_info_bytes))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"frames", tmp_str.len = sizeof("frames") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).segment_info_frames))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"missing", tmp_str.len = sizeof("missing") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).segment_info_missing))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"keys", tmp_str.len = sizeof("keys") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).segment_info_keys))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"locked", tmp_str.len = sizeof("locked") - 1, &tmp_str)))))
	&& (zcbor_bool_encode(state, (&(*input).segment_info_locked))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"recovered", tmp_str.len = sizeof("recovered") - 1, &tmp_str)))))
	&& (zcbor_bool_encode(state, (&(*input).segment_info_recovered))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 10))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_unlock_req(
		zcbor_state_t *state, const struct unlock_req *input)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_encode(state, 1) && (((((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"lock_id", tmp_str.len = sizeof("lock_id") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).unlock_req_lock_id))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 1))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_lock_rep(
		zcbor_state_t *state, const struct lock_rep *input)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_encode(state, 2) && (((((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"icd", tmp_str.len = sizeof("icd") - 1, &tmp_str)))))
	&& (zcbor_uint64_put(state, (1))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"lock_id", tmp_str.len = sizeof("lock_id") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).lock_rep_lock_id))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 2))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_lock_req(
		zcbor_state_t *state, const struct lock_req *input)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_encode(state, 5) && (((((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"session", tmp_str.len = sizeof("session") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).lock_req_session))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"from_idx", tmp_str.len = sizeof("from_idx") - 1, &tmp_str)))))
	&& (zcbor_int64_encode(state, (&(*input).lock_req_from_idx))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"to_idx", tmp_str.len = sizeof("to_idx") - 1, &tmp_str)))))
	&& (zcbor_int64_encode(state, (&(*input).lock_req_to_idx))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"ttl_s", tmp_str.len = sizeof("ttl_s") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).lock_req_ttl_s))))
	&& (!(*input).lock_req_reason_present || encode_repeated_lock_req_reason(state, (&(*input).lock_req_reason)))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 5))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_segments_rep(
		zcbor_state_t *state, const struct segments_rep *input)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"icd", tmp_str.len = sizeof("icd") - 1, &tmp_str)))))
	&& (zcbor_uint64_put(state, (1))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"session", tmp_str.len = sizeof("session") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).segments_rep_session))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"segments", tmp_str.len = sizeof("segments") - 1, &tmp_str)))))
	&& (zcbor_list_start_encode(state, 512) && ((zcbor_multi_encode_minmax(0, 512, &(*input).segments_rep_segments_segment_info_m_count, (zcbor_encoder_t *)encode_segment_info, state, (*&(*input).segments_rep_segments_segment_info_m), sizeof(struct segment_info))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_list_end_encode(state, 512)))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"more", tmp_str.len = sizeof("more") - 1, &tmp_str)))))
	&& (zcbor_bool_encode(state, (&(*input).segments_rep_more))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_sessions_rep(
		zcbor_state_t *state, const struct sessions_rep *input)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_encode(state, 3) && (((((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"icd", tmp_str.len = sizeof("icd") - 1, &tmp_str)))))
	&& (zcbor_uint64_put(state, (1))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"sessions", tmp_str.len = sizeof("sessions") - 1, &tmp_str)))))
	&& (zcbor_list_start_encode(state, 512) && ((zcbor_multi_encode_minmax(0, 512, &(*input).sessions_rep_sessions_session_info_m_count, (zcbor_encoder_t *)encode_session_info, state, (*&(*input).sessions_rep_sessions_session_info_m), sizeof(struct session_info))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_list_end_encode(state, 512)))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"more", tmp_str.len = sizeof("more") - 1, &tmp_str)))))
	&& (zcbor_bool_encode(state, (&(*input).sessions_rep_more))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 3))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_session_rep(
		zcbor_state_t *state, const struct session_rep *input)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_encode(state, 2) && (((((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"icd", tmp_str.len = sizeof("icd") - 1, &tmp_str)))))
	&& (zcbor_uint64_put(state, (1))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"session", tmp_str.len = sizeof("session") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).session_rep_session))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 2))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_arm_rep(
		zcbor_state_t *state, const struct arm_rep *input)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_encode(state, 4) && (((((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"icd", tmp_str.len = sizeof("icd") - 1, &tmp_str)))))
	&& (zcbor_uint64_put(state, (1))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"session", tmp_str.len = sizeof("session") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).arm_rep_session))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"t0", tmp_str.len = sizeof("t0") - 1, &tmp_str)))))
	&& (zcbor_int64_encode(state, (&(*input).arm_rep_t0))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"period", tmp_str.len = sizeof("period") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).arm_rep_period))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 4))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_arm_req(
		zcbor_state_t *state, const struct arm_req *input)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_encode(state, 3) && (((((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"session", tmp_str.len = sizeof("session") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).arm_req_session))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"fps", tmp_str.len = sizeof("fps") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).arm_req_fps))))
	&& (!(*input).arm_req_t0_present || encode_repeated_arm_req_t0(state, (&(*input).arm_req_t0)))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 3))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_health(
		zcbor_state_t *state, const struct health *input)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_encode(state, 16) && (((((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"icd", tmp_str.len = sizeof("icd") - 1, &tmp_str)))))
	&& (zcbor_uint64_put(state, (1))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"state", tmp_str.len = sizeof("state") - 1, &tmp_str)))))
	&& (zcbor_tstr_encode(state, (&(*input).health_state))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"session", tmp_str.len = sizeof("session") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).health_session))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"ring_used", tmp_str.len = sizeof("ring_used") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).health_ring_used))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"ring_locked", tmp_str.len = sizeof("ring_locked") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).health_ring_locked))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"ring_free", tmp_str.len = sizeof("ring_free") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).health_ring_free))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"depth_s", tmp_str.len = sizeof("depth_s") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).health_depth_s))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"emmc_life_a", tmp_str.len = sizeof("emmc_life_a") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).health_emmc_life_a))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"emmc_life_b", tmp_str.len = sizeof("emmc_life_b") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).health_emmc_life_b))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"emmc_pre_eol", tmp_str.len = sizeof("emmc_pre_eol") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).health_emmc_pre_eol))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"frames", tmp_str.len = sizeof("frames") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).health_frames))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"missing", tmp_str.len = sizeof("missing") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).health_missing))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"temp_mc", tmp_str.len = sizeof("temp_mc") - 1, &tmp_str)))))
	&& (zcbor_int64_encode(state, (&(*input).health_temp_mc))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"phc_ns", tmp_str.len = sizeof("phc_ns") - 1, &tmp_str)))))
	&& (zcbor_int64_encode(state, (&(*input).health_phc_ns))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"time_verified", tmp_str.len = sizeof("time_verified") - 1, &tmp_str)))))
	&& (zcbor_bool_encode(state, (&(*input).health_time_verified))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"message", tmp_str.len = sizeof("message") - 1, &tmp_str)))))
	&& (zcbor_tstr_encode(state, (&(*input).health_message))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 16))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_identity(
		zcbor_state_t *state, const struct identity *input)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_encode(state, 7) && (((((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"icd", tmp_str.len = sizeof("icd") - 1, &tmp_str)))))
	&& (zcbor_uint64_put(state, (1))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"node", tmp_str.len = sizeof("node") - 1, &tmp_str)))))
	&& (zcbor_tstr_encode(state, (&(*input).identity_node))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"version", tmp_str.len = sizeof("version") - 1, &tmp_str)))))
	&& (zcbor_tstr_encode(state, (&(*input).identity_version))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"fps_min", tmp_str.len = sizeof("fps_min") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).identity_fps_min))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"fps_max", tmp_str.len = sizeof("fps_max") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).identity_fps_max))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"width", tmp_str.len = sizeof("width") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).identity_width))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"height", tmp_str.len = sizeof("height") - 1, &tmp_str)))))
	&& (zcbor_uint64_encode(state, (&(*input).identity_height))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 7))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_ok_rep(
		zcbor_state_t *state, const void *input)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_encode(state, 1) && (((((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"icd", tmp_str.len = sizeof("icd") - 1, &tmp_str)))))
	&& (zcbor_uint64_put(state, (1))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 1))));

	log_result(state, res, __func__);
	return res;
}

static bool encode_error(
		zcbor_state_t *state, const struct error *input)
{
	zcbor_log("%s\r\n", __func__);
	struct zcbor_string tmp_str;

	bool res = (((zcbor_map_start_encode(state, 3) && (((((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"icd", tmp_str.len = sizeof("icd") - 1, &tmp_str)))))
	&& (zcbor_uint64_put(state, (1))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"code", tmp_str.len = sizeof("code") - 1, &tmp_str)))))
	&& (zcbor_tstr_encode(state, (&(*input).error_code))))
	&& (((zcbor_tstr_encode(state, ((tmp_str.value = (uint8_t *)"message", tmp_str.len = sizeof("message") - 1, &tmp_str)))))
	&& (zcbor_tstr_encode(state, (&(*input).error_message))))) || (zcbor_list_map_end_force_encode(state), false)) && zcbor_map_end_encode(state, 3))));

	log_result(state, res, __func__);
	return res;
}



int cbor_encode_error(
		uint8_t *payload, size_t payload_len,
		const struct error *input,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)input, payload_len_out, states,
		(zcbor_decoder_t *)encode_error, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_encode_ok_rep(
		uint8_t *payload, size_t payload_len,
		const void *input,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)input, payload_len_out, states,
		(zcbor_decoder_t *)encode_ok_rep, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_encode_identity(
		uint8_t *payload, size_t payload_len,
		const struct identity *input,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)input, payload_len_out, states,
		(zcbor_decoder_t *)encode_identity, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_encode_health(
		uint8_t *payload, size_t payload_len,
		const struct health *input,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)input, payload_len_out, states,
		(zcbor_decoder_t *)encode_health, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_encode_arm_req(
		uint8_t *payload, size_t payload_len,
		const struct arm_req *input,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)input, payload_len_out, states,
		(zcbor_decoder_t *)encode_arm_req, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_encode_arm_rep(
		uint8_t *payload, size_t payload_len,
		const struct arm_rep *input,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)input, payload_len_out, states,
		(zcbor_decoder_t *)encode_arm_rep, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_encode_session_rep(
		uint8_t *payload, size_t payload_len,
		const struct session_rep *input,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)input, payload_len_out, states,
		(zcbor_decoder_t *)encode_session_rep, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_encode_sessions_rep(
		uint8_t *payload, size_t payload_len,
		const struct sessions_rep *input,
		size_t *payload_len_out)
{
	zcbor_state_t states[5];

	return zcbor_entry_function(payload, payload_len, (void *)input, payload_len_out, states,
		(zcbor_decoder_t *)encode_sessions_rep, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_encode_segments_rep(
		uint8_t *payload, size_t payload_len,
		const struct segments_rep *input,
		size_t *payload_len_out)
{
	zcbor_state_t states[5];

	return zcbor_entry_function(payload, payload_len, (void *)input, payload_len_out, states,
		(zcbor_decoder_t *)encode_segments_rep, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_encode_lock_req(
		uint8_t *payload, size_t payload_len,
		const struct lock_req *input,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)input, payload_len_out, states,
		(zcbor_decoder_t *)encode_lock_req, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_encode_lock_rep(
		uint8_t *payload, size_t payload_len,
		const struct lock_rep *input,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)input, payload_len_out, states,
		(zcbor_decoder_t *)encode_lock_rep, sizeof(states) / sizeof(zcbor_state_t), 1);
}


int cbor_encode_unlock_req(
		uint8_t *payload, size_t payload_len,
		const struct unlock_req *input,
		size_t *payload_len_out)
{
	zcbor_state_t states[3];

	return zcbor_entry_function(payload, payload_len, (void *)input, payload_len_out, states,
		(zcbor_decoder_t *)encode_unlock_req, sizeof(states) / sizeof(zcbor_state_t), 1);
}
