/*
 * Generated using zcbor version 0.9.1
 * https://github.com/NordicSemiconductor/zcbor
 * Generated with a --default-max-qty of 512
 */

#ifndef ICD_ENCODE_H__
#define ICD_ENCODE_H__

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


int cbor_encode_error(
		uint8_t *payload, size_t payload_len,
		const struct error *input,
		size_t *payload_len_out);


int cbor_encode_ok_rep(
		uint8_t *payload, size_t payload_len,
		const void *input,
		size_t *payload_len_out);


int cbor_encode_identity(
		uint8_t *payload, size_t payload_len,
		const struct identity *input,
		size_t *payload_len_out);


int cbor_encode_health(
		uint8_t *payload, size_t payload_len,
		const struct health *input,
		size_t *payload_len_out);


int cbor_encode_arm_req(
		uint8_t *payload, size_t payload_len,
		const struct arm_req *input,
		size_t *payload_len_out);


int cbor_encode_arm_rep(
		uint8_t *payload, size_t payload_len,
		const struct arm_rep *input,
		size_t *payload_len_out);


int cbor_encode_session_rep(
		uint8_t *payload, size_t payload_len,
		const struct session_rep *input,
		size_t *payload_len_out);


int cbor_encode_sessions_rep(
		uint8_t *payload, size_t payload_len,
		const struct sessions_rep *input,
		size_t *payload_len_out);


int cbor_encode_segments_rep(
		uint8_t *payload, size_t payload_len,
		const struct segments_rep *input,
		size_t *payload_len_out);


int cbor_encode_lock_req(
		uint8_t *payload, size_t payload_len,
		const struct lock_req *input,
		size_t *payload_len_out);


int cbor_encode_lock_rep(
		uint8_t *payload, size_t payload_len,
		const struct lock_rep *input,
		size_t *payload_len_out);


int cbor_encode_unlock_req(
		uint8_t *payload, size_t payload_len,
		const struct unlock_req *input,
		size_t *payload_len_out);


#ifdef __cplusplus
}
#endif

#endif /* ICD_ENCODE_H__ */
