/*
 * Generated using zcbor version 0.9.1
 * https://github.com/NordicSemiconductor/zcbor
 * Generated with a --default-max-qty of 512
 */

#ifndef ICD_DECODE_H__
#define ICD_DECODE_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "icd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#if DEFAULT_MAX_QTY != 512
#error "The type file was generated with a different default_max_qty than this file"
#endif


int cbor_decode_error(
		const uint8_t *payload, size_t payload_len,
		struct error *result,
		size_t *payload_len_out);


int cbor_decode_ok_rep(
		const uint8_t *payload, size_t payload_len,
		void *result,
		size_t *payload_len_out);


int cbor_decode_identity(
		const uint8_t *payload, size_t payload_len,
		struct identity *result,
		size_t *payload_len_out);


int cbor_decode_health(
		const uint8_t *payload, size_t payload_len,
		struct health *result,
		size_t *payload_len_out);


int cbor_decode_arm_req(
		const uint8_t *payload, size_t payload_len,
		struct arm_req *result,
		size_t *payload_len_out);


int cbor_decode_arm_rep(
		const uint8_t *payload, size_t payload_len,
		struct arm_rep *result,
		size_t *payload_len_out);


int cbor_decode_session_rep(
		const uint8_t *payload, size_t payload_len,
		struct session_rep *result,
		size_t *payload_len_out);


int cbor_decode_sessions_rep(
		const uint8_t *payload, size_t payload_len,
		struct sessions_rep *result,
		size_t *payload_len_out);


int cbor_decode_segments_rep(
		const uint8_t *payload, size_t payload_len,
		struct segments_rep *result,
		size_t *payload_len_out);


int cbor_decode_lock_req(
		const uint8_t *payload, size_t payload_len,
		struct lock_req *result,
		size_t *payload_len_out);


int cbor_decode_lock_rep(
		const uint8_t *payload, size_t payload_len,
		struct lock_rep *result,
		size_t *payload_len_out);


int cbor_decode_unlock_req(
		const uint8_t *payload, size_t payload_len,
		struct unlock_req *result,
		size_t *payload_len_out);


#ifdef __cplusplus
}
#endif

#endif /* ICD_DECODE_H__ */
