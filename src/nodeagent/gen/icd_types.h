/*
 * Generated using zcbor version 0.9.1
 * https://github.com/NordicSemiconductor/zcbor
 * Generated with a --default-max-qty of 512
 */

#ifndef ICD_TYPES_H__
#define ICD_TYPES_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <zcbor_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Which value for --default-max-qty this file was created with.
 *
 *  The define is used in the other generated file to do a build-time
 *  compatibility check.
 *
 *  See `zcbor --help` for more information about --default-max-qty
 */
#define DEFAULT_MAX_QTY 512

struct arm_req_t0 {
	int64_t arm_req_t0;
};

struct arm_req {
	uint64_t arm_req_session;
	uint64_t arm_req_fps;
	struct arm_req_t0 arm_req_t0;
	bool arm_req_t0_present;
};

struct lock_req_reason {
	struct zcbor_string lock_req_reason;
};

struct lock_req {
	uint64_t lock_req_session;
	int64_t lock_req_from_idx;
	int64_t lock_req_to_idx;
	uint64_t lock_req_ttl_s;
	struct lock_req_reason lock_req_reason;
	bool lock_req_reason_present;
};

struct unlock_req {
	uint64_t unlock_req_lock_id;
};

struct error {
	struct zcbor_string error_code;
	struct zcbor_string error_message;
};

struct identity {
	struct zcbor_string identity_node;
	struct zcbor_string identity_version;
	uint64_t identity_fps_min;
	uint64_t identity_fps_max;
	uint64_t identity_width;
	uint64_t identity_height;
};

struct health {
	struct zcbor_string health_state;
	uint64_t health_session;
	uint64_t health_ring_used;
	uint64_t health_ring_locked;
	uint64_t health_ring_free;
	uint64_t health_depth_s;
	uint64_t health_emmc_life_a;
	uint64_t health_emmc_life_b;
	uint64_t health_emmc_pre_eol;
	uint64_t health_frames;
	uint64_t health_missing;
	int64_t health_temp_mc;
	int64_t health_phc_ns;
	bool health_time_verified;
	struct zcbor_string health_message;
};

struct arm_rep {
	uint64_t arm_rep_session;
	int64_t arm_rep_t0;
	uint64_t arm_rep_period;
};

struct session_rep {
	uint64_t session_rep_session;
};

struct session_info {
	uint64_t session_info_session;
	struct zcbor_string session_info_status;
	int64_t session_info_start_wall;
	int64_t session_info_t0;
	uint64_t session_info_period;
	int64_t session_info_first_idx;
	int64_t session_info_last_idx;
	uint64_t session_info_frames;
	uint64_t session_info_missing;
	uint64_t session_info_segments;
	uint64_t session_info_bytes;
	bool session_info_truncated;
};

struct sessions_rep {
	struct session_info sessions_rep_sessions_session_info_m[512];
	size_t sessions_rep_sessions_session_info_m_count;
	bool sessions_rep_more;
};

struct segment_info {
	int64_t segment_info_first_idx;
	int64_t segment_info_last_idx;
	int64_t segment_info_t_first;
	int64_t segment_info_t_last;
	uint64_t segment_info_bytes;
	uint64_t segment_info_frames;
	uint64_t segment_info_missing;
	uint64_t segment_info_keys;
	bool segment_info_locked;
	bool segment_info_recovered;
};

struct segments_rep {
	uint64_t segments_rep_session;
	struct segment_info segments_rep_segments_segment_info_m[512];
	size_t segments_rep_segments_segment_info_m_count;
	bool segments_rep_more;
};

struct lock_rep {
	uint64_t lock_rep_lock_id;
};

#ifdef __cplusplus
}
#endif

#endif /* ICD_TYPES_H__ */
