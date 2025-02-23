/**
 *  Copyright Notice:
 *  Copyright 2021-2022 DMTF. All rights reserved.
 *  License: BSD 3-Clause License. For full text see link: https://github.com/DMTF/libspdm/blob/main/LICENSE.md
 **/
#include "internal/libspdm_responder_lib.h"

#if LIBSPDM_ENABLE_CAPABILITY_CERT_CAP

/**
 * Process the SPDM GET_CERTIFICATE request and return the response.
 *
 * @param  spdm_context                  A pointer to the SPDM context.
 * @param  request_size                  size in bytes of the request data.
 * @param  request                      A pointer to the request data.
 * @param  response_size                 size in bytes of the response data.
 *                                     On input, it means the size in bytes of response data buffer.
 *                                     On output, it means the size in bytes of copied response data buffer if RETURN_SUCCESS is returned,
 *                                     and means the size in bytes of desired response data buffer if RETURN_BUFFER_TOO_SMALL is returned.
 * @param  response                     A pointer to the response data.
 *
 * @retval RETURN_SUCCESS               The request is processed and the response is returned.
 * @retval RETURN_BUFFER_TOO_SMALL      The buffer is too small to hold the data.
 * @retval RETURN_DEVICE_ERROR          A device error occurs when communicates with the device.
 * @retval RETURN_SECURITY_VIOLATION    Any verification fails.
 **/
libspdm_return_t libspdm_get_response_certificate(void *context,
                                                  size_t request_size,
                                                  const void *request,
                                                  size_t *response_size,
                                                  void *response)
{
    const spdm_get_certificate_request_t *spdm_request;
    spdm_certificate_response_t *spdm_response;
    uint16_t offset;
    uint16_t length;
    size_t remainder_length;
    uint8_t slot_id;
    libspdm_context_t *spdm_context;
    libspdm_return_t status;
    size_t response_capacity;
    libspdm_session_info_t *session_info;
    libspdm_session_state_t session_state;

    spdm_context = context;
    spdm_request = request;

    if (spdm_request->header.spdm_version != libspdm_get_connection_version(spdm_context)) {
        return libspdm_generate_error_response(spdm_context,
                                               SPDM_ERROR_CODE_VERSION_MISMATCH, 0,
                                               response_size, response);
    }
    if (spdm_context->response_state != LIBSPDM_RESPONSE_STATE_NORMAL) {
        return libspdm_responder_handle_response_state(
            spdm_context,
            spdm_request->header.request_response_code,
            response_size, response);
    }
    if (spdm_context->connection_info.connection_state < LIBSPDM_CONNECTION_STATE_NEGOTIATED) {
        return libspdm_generate_error_response(spdm_context,
                                               SPDM_ERROR_CODE_UNEXPECTED_REQUEST,
                                               0, response_size, response);
    }
    session_info = NULL;
    if (spdm_context->last_spdm_request_session_id_valid) {
        session_info = libspdm_get_session_info_via_session_id(
            spdm_context,
            spdm_context->last_spdm_request_session_id);
        if (session_info == NULL) {
            return libspdm_generate_error_response(
                spdm_context,
                SPDM_ERROR_CODE_UNEXPECTED_REQUEST, 0,
                response_size, response);
        }
        session_state = libspdm_secured_message_get_session_state(
            session_info->secured_message_context);
        if (session_state != LIBSPDM_SESSION_STATE_ESTABLISHED) {
            return libspdm_generate_error_response(
                spdm_context,
                SPDM_ERROR_CODE_UNEXPECTED_REQUEST, 0,
                response_size, response);
        }
    }
    if (!libspdm_is_capabilities_flag_supported(
            spdm_context, false, 0,
            SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CERT_CAP)) {
        return libspdm_generate_error_response(
            spdm_context, SPDM_ERROR_CODE_UNSUPPORTED_REQUEST,
            SPDM_GET_CERTIFICATE, response_size, response);
    }

    if (request_size != sizeof(spdm_get_certificate_request_t)) {
        return libspdm_generate_error_response(spdm_context,
                                               SPDM_ERROR_CODE_INVALID_REQUEST, 0,
                                               response_size, response);
    }

    slot_id = spdm_request->header.param1 & SPDM_GET_CERTIFICATE_REQUEST_SLOT_ID_MASK;

    if (slot_id >= SPDM_MAX_SLOT_COUNT) {
        return libspdm_generate_error_response(spdm_context,
                                               SPDM_ERROR_CODE_INVALID_REQUEST, 0,
                                               response_size, response);
    }

    if (spdm_context->local_context.local_cert_chain_provision[slot_id] == NULL) {
        return libspdm_generate_error_response(
            spdm_context, SPDM_ERROR_CODE_INVALID_REQUEST,
            0, response_size, response);
    }

    offset = spdm_request->offset;
    length = spdm_request->length;

    if (length == 0) {
        return libspdm_generate_error_response(spdm_context,
                                               SPDM_ERROR_CODE_INVALID_REQUEST, 0,
                                               response_size, response);
    }
    if (offset >= spdm_context->local_context.local_cert_chain_provision_size[slot_id]) {
        return libspdm_generate_error_response(spdm_context,
                                               SPDM_ERROR_CODE_INVALID_REQUEST, 0,
                                               response_size, response);
    }

    if (!libspdm_is_capabilities_flag_supported(spdm_context, false,
                                                SPDM_GET_CAPABILITIES_REQUEST_FLAGS_CHUNK_CAP,
                                                SPDM_GET_CAPABILITIES_RESPONSE_FLAGS_CHUNK_CAP)) {
        if (length > LIBSPDM_MAX_CERT_CHAIN_BLOCK_LEN) {
            length = LIBSPDM_MAX_CERT_CHAIN_BLOCK_LEN;
        }
    }
    if ((size_t)(offset + length) > spdm_context->local_context.
        local_cert_chain_provision_size[slot_id]) {
        length = (uint16_t)(spdm_context->local_context.local_cert_chain_provision_size[slot_id] -
                            offset);
    }
    remainder_length = spdm_context->local_context.local_cert_chain_provision_size[slot_id] -
                       (length + offset);

    libspdm_reset_message_buffer_via_request_code(spdm_context, session_info,
                                                  spdm_request->header.request_response_code);

    LIBSPDM_ASSERT(*response_size >= sizeof(spdm_certificate_response_t) + length);
    response_capacity = *response_size;
    *response_size = sizeof(spdm_certificate_response_t) + length;
    libspdm_zero_mem(response, *response_size);
    spdm_response = response;

    spdm_response->header.spdm_version = spdm_request->header.spdm_version;
    spdm_response->header.request_response_code = SPDM_CERTIFICATE;
    spdm_response->header.param1 = slot_id;
    spdm_response->header.param2 = 0;
    spdm_response->portion_length = length;
    spdm_response->remainder_length = (uint16_t)remainder_length;
    libspdm_copy_mem(spdm_response + 1, response_capacity - sizeof(spdm_certificate_response_t),
                     (const uint8_t *)spdm_context->local_context
                     .local_cert_chain_provision[slot_id] + offset, length);

    /* Cache*/

    if (session_info == NULL) {
        status = libspdm_append_message_b(spdm_context, spdm_request, request_size);
        if (LIBSPDM_STATUS_IS_ERROR(status)) {
            return libspdm_generate_error_response(spdm_context,
                                                   SPDM_ERROR_CODE_UNSPECIFIED, 0,
                                                   response_size, response);
        }

        status = libspdm_append_message_b(spdm_context, spdm_response, *response_size);
        if (LIBSPDM_STATUS_IS_ERROR(status)) {
            return libspdm_generate_error_response(spdm_context,
                                                   SPDM_ERROR_CODE_UNSPECIFIED, 0,
                                                   response_size, response);
        }
    }

    if (spdm_context->connection_info.connection_state <
        LIBSPDM_CONNECTION_STATE_AFTER_CERTIFICATE) {
        libspdm_set_connection_state(spdm_context, LIBSPDM_CONNECTION_STATE_AFTER_CERTIFICATE);
    }

    return LIBSPDM_STATUS_SUCCESS;
}

#endif /* LIBSPDM_ENABLE_CAPABILITY_CERT_CAP*/
