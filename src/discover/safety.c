// SPDX-License-Identifier: GPL-3.0-or-later
#include "jaglink/discover_safety.h"

#include <stdbool.h>

static JaglinkDiscoverSafetyDecision decision(
    JaglinkDiscoverDisposition disposition,
    JaglinkDiscoverSafetyReason reason,
    uint8_t service)
{
    const JaglinkDiscoverSafetyDecision value = {
        .disposition = disposition,
        .reason = reason,
        .service = service
    };
    return value;
}

static bool supported_pid_block(uint8_t pid)
{
    return pid == 0x00U || pid == 0x20U || pid == 0x40U ||
           pid == 0x60U || pid == 0x80U || pid == 0xa0U ||
           pid == 0xc0U;
}

static bool vehicle_information_pid(uint8_t pid)
{
    return pid == 0x00U || pid == 0x02U || pid == 0x04U ||
           pid == 0x06U || pid == 0x08U || pid == 0x0aU;
}

JaglinkDiscoverSafetyDecision jaglink_discover_classify_request(
    const uint8_t *payload, size_t payload_size)
{
    uint8_t service;

    if (payload == NULL || payload_size == 0U) {
        return decision(JAGLINK_DISCOVER_DENY,
                        JAGLINK_DISCOVER_REASON_EMPTY_REQUEST, 0U);
    }
    service = payload[0];

    if (service == 0x01U && payload_size == 2U &&
        supported_pid_block(payload[1])) {
        return decision(JAGLINK_DISCOVER_ALLOW_READ_ONLY_OBD,
                        JAGLINK_DISCOVER_REASON_ALLOWED_STANDARD_OBD_READ,
                        service);
    }
    if (service == 0x09U && payload_size == 2U &&
        vehicle_information_pid(payload[1])) {
        return decision(JAGLINK_DISCOVER_ALLOW_READ_ONLY_OBD,
                        JAGLINK_DISCOVER_REASON_ALLOWED_STANDARD_OBD_READ,
                        service);
    }
    if ((service == 0x03U || service == 0x07U || service == 0x0aU) &&
        payload_size == 1U) {
        return decision(JAGLINK_DISCOVER_ALLOW_READ_ONLY_OBD,
                        JAGLINK_DISCOVER_REASON_ALLOWED_STANDARD_OBD_READ,
                        service);
    }

    switch (service) {
    case 0x04U:
    case 0x14U:
        return decision(JAGLINK_DISCOVER_DENY,
                        JAGLINK_DISCOVER_REASON_CLEAR_DIAGNOSTIC_INFORMATION,
                        service);
    case 0x11U:
        return decision(JAGLINK_DISCOVER_DENY,
                        JAGLINK_DISCOVER_REASON_ECU_RESET, service);
    case 0x27U:
        return decision(JAGLINK_DISCOVER_DENY,
                        JAGLINK_DISCOVER_REASON_SECURITY_ACCESS, service);
    case 0x31U:
        return decision(JAGLINK_DISCOVER_DENY,
                        JAGLINK_DISCOVER_REASON_ROUTINE_CONTROL, service);
    case 0x34U:
    case 0x35U:
    case 0x36U:
    case 0x37U:
    case 0x38U:
        return decision(JAGLINK_DISCOVER_DENY,
                        JAGLINK_DISCOVER_REASON_PROGRAMMING_OR_TRANSFER,
                        service);
    case 0x08U:
    case 0x10U:
    case 0x28U:
    case 0x2eU:
    case 0x2fU:
    case 0x3dU:
    case 0x85U:
        return decision(JAGLINK_DISCOVER_DENY,
                        JAGLINK_DISCOVER_REASON_WRITE_OR_CONTROL, service);
    default:
        break;
    }

    if ((service == 0x01U || service == 0x03U || service == 0x07U ||
         service == 0x09U || service == 0x0aU) && payload_size > 3U) {
        return decision(JAGLINK_DISCOVER_DENY,
                        JAGLINK_DISCOVER_REASON_MALFORMED_REQUEST, service);
    }
    return decision(JAGLINK_DISCOVER_DENY,
                    JAGLINK_DISCOVER_REASON_NOT_ALLOWLISTED, service);
}

const char *jaglink_discover_safety_reason_name(
    JaglinkDiscoverSafetyReason reason)
{
    switch (reason) {
    case JAGLINK_DISCOVER_REASON_ALLOWED_STANDARD_OBD_READ:
        return "allowed-standard-obd-read";
    case JAGLINK_DISCOVER_REASON_EMPTY_REQUEST: return "empty-request";
    case JAGLINK_DISCOVER_REASON_MALFORMED_REQUEST: return "malformed-request";
    case JAGLINK_DISCOVER_REASON_CLEAR_DIAGNOSTIC_INFORMATION:
        return "clear-diagnostic-information";
    case JAGLINK_DISCOVER_REASON_ECU_RESET: return "ecu-reset";
    case JAGLINK_DISCOVER_REASON_SECURITY_ACCESS: return "security-access";
    case JAGLINK_DISCOVER_REASON_WRITE_OR_CONTROL: return "write-or-control";
    case JAGLINK_DISCOVER_REASON_ROUTINE_CONTROL: return "routine-control";
    case JAGLINK_DISCOVER_REASON_PROGRAMMING_OR_TRANSFER:
        return "programming-or-transfer";
    case JAGLINK_DISCOVER_REASON_NOT_ALLOWLISTED: return "not-allowlisted";
    }
    return "unknown";
}
