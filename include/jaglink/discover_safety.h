// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file discover_safety.h
 * @brief Deny-by-default transmission policy for JAGLINK Discover.
 */
#ifndef JAGLINK_DISCOVER_SAFETY_H
#define JAGLINK_DISCOVER_SAFETY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JAGLINK_DISCOVER_DENY = 0,
    JAGLINK_DISCOVER_ALLOW_READ_ONLY_OBD
} JaglinkDiscoverDisposition;

typedef enum {
    JAGLINK_DISCOVER_REASON_ALLOWED_STANDARD_OBD_READ = 0,
    JAGLINK_DISCOVER_REASON_EMPTY_REQUEST,
    JAGLINK_DISCOVER_REASON_MALFORMED_REQUEST,
    JAGLINK_DISCOVER_REASON_CLEAR_DIAGNOSTIC_INFORMATION,
    JAGLINK_DISCOVER_REASON_ECU_RESET,
    JAGLINK_DISCOVER_REASON_SECURITY_ACCESS,
    JAGLINK_DISCOVER_REASON_WRITE_OR_CONTROL,
    JAGLINK_DISCOVER_REASON_ROUTINE_CONTROL,
    JAGLINK_DISCOVER_REASON_PROGRAMMING_OR_TRANSFER,
    JAGLINK_DISCOVER_REASON_NOT_ALLOWLISTED
} JaglinkDiscoverSafetyReason;

typedef struct {
    JaglinkDiscoverDisposition disposition;
    JaglinkDiscoverSafetyReason reason;
    uint8_t service;
} JaglinkDiscoverSafetyDecision;

/**
 * Classify a decoded diagnostic payload before any transport write.
 *
 * Only the bounded SAE OBD inventory requests used by jaglink-discover are
 * allowed. Unknown, malformed, manufacturer-specific and state-changing
 * requests are denied.
 */
JaglinkDiscoverSafetyDecision jaglink_discover_classify_request(
    const uint8_t *payload, size_t payload_size);

const char *jaglink_discover_safety_reason_name(
    JaglinkDiscoverSafetyReason reason);

#ifdef __cplusplus
}
#endif

#endif
