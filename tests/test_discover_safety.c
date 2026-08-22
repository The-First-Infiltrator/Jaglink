// SPDX-License-Identifier: GPL-3.0-or-later
#include "jaglink/discover_safety.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

static void check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void expect_allowed(const uint8_t *request, size_t size,
                           const char *message)
{
    JaglinkDiscoverSafetyDecision result =
        jaglink_discover_classify_request(request, size);
    check(result.disposition == JAGLINK_DISCOVER_ALLOW_READ_ONLY_OBD &&
          result.reason ==
              JAGLINK_DISCOVER_REASON_ALLOWED_STANDARD_OBD_READ,
          message);
}

static void expect_denied(const uint8_t *request, size_t size,
                          JaglinkDiscoverSafetyReason reason,
                          const char *message)
{
    JaglinkDiscoverSafetyDecision result =
        jaglink_discover_classify_request(request, size);
    check(result.disposition == JAGLINK_DISCOVER_DENY &&
          result.reason == reason, message);
}

int main(void)
{
    static const uint8_t allowed[][2] = {
        {0x01U, 0x00U}, {0x01U, 0x20U}, {0x01U, 0xc0U},
        {0x09U, 0x00U}, {0x09U, 0x02U}, {0x09U, 0x0aU}
    };
    static const uint8_t dtc_stored[] = {0x03U};
    static const uint8_t dtc_pending[] = {0x07U};
    static const uint8_t dtc_permanent[] = {0x0aU};
    static const struct {
        uint8_t request[3];
        size_t size;
        JaglinkDiscoverSafetyReason reason;
    } denied[] = {
        {{0x04U, 0U, 0U}, 1U,
         JAGLINK_DISCOVER_REASON_CLEAR_DIAGNOSTIC_INFORMATION},
        {{0x14U, 0xffU, 0xffU}, 3U,
         JAGLINK_DISCOVER_REASON_CLEAR_DIAGNOSTIC_INFORMATION},
        {{0x11U, 0x01U, 0U}, 2U, JAGLINK_DISCOVER_REASON_ECU_RESET},
        {{0x27U, 0x01U, 0U}, 2U, JAGLINK_DISCOVER_REASON_SECURITY_ACCESS},
        {{0x08U, 0U, 0U}, 1U,
         JAGLINK_DISCOVER_REASON_WRITE_OR_CONTROL},
        {{0x10U, 0x03U, 0U}, 2U,
         JAGLINK_DISCOVER_REASON_WRITE_OR_CONTROL},
        {{0x28U, 0U, 0U}, 1U,
         JAGLINK_DISCOVER_REASON_WRITE_OR_CONTROL},
        {{0x2eU, 0xf1U, 0x90U}, 3U,
         JAGLINK_DISCOVER_REASON_WRITE_OR_CONTROL},
        {{0x2fU, 0xf1U, 0x90U}, 3U,
         JAGLINK_DISCOVER_REASON_WRITE_OR_CONTROL},
        {{0x3dU, 0U, 0U}, 1U,
         JAGLINK_DISCOVER_REASON_WRITE_OR_CONTROL},
        {{0x85U, 0U, 0U}, 1U,
         JAGLINK_DISCOVER_REASON_WRITE_OR_CONTROL},
        {{0x31U, 0x01U, 0U}, 2U,
         JAGLINK_DISCOVER_REASON_ROUTINE_CONTROL},
        {{0x34U, 0U, 0U}, 1U,
         JAGLINK_DISCOVER_REASON_PROGRAMMING_OR_TRANSFER},
        {{0x35U, 0U, 0U}, 1U,
         JAGLINK_DISCOVER_REASON_PROGRAMMING_OR_TRANSFER},
        {{0x36U, 0x01U, 0U}, 2U,
         JAGLINK_DISCOVER_REASON_PROGRAMMING_OR_TRANSFER},
        {{0x37U, 0U, 0U}, 1U,
         JAGLINK_DISCOVER_REASON_PROGRAMMING_OR_TRANSFER},
        {{0x38U, 0U, 0U}, 1U,
         JAGLINK_DISCOVER_REASON_PROGRAMMING_OR_TRANSFER},
        {{0x22U, 0xf1U, 0x90U}, 3U,
         JAGLINK_DISCOVER_REASON_NOT_ALLOWLISTED},
        {{0x01U, 0x0cU, 0U}, 2U,
         JAGLINK_DISCOVER_REASON_NOT_ALLOWLISTED}
    };
    size_t index;

    for (index = 0U; index < sizeof(allowed) / sizeof(allowed[0]); ++index) {
        expect_allowed(allowed[index], sizeof(allowed[index]),
                       "bounded inventory request allowed");
    }
    expect_allowed(dtc_stored, sizeof(dtc_stored), "stored DTC read allowed");
    expect_allowed(dtc_pending, sizeof(dtc_pending), "pending DTC read allowed");
    expect_allowed(dtc_permanent, sizeof(dtc_permanent),
                   "permanent DTC read allowed");
    for (index = 0U; index < sizeof(denied) / sizeof(denied[0]); ++index) {
        expect_denied(denied[index].request, denied[index].size,
                      denied[index].reason, "unsafe request denied");
    }
    expect_denied(NULL, 0U, JAGLINK_DISCOVER_REASON_EMPTY_REQUEST,
                  "empty request denied");
    check(strcmp(jaglink_discover_safety_reason_name(
                     JAGLINK_DISCOVER_REASON_SECURITY_ACCESS),
                 "security-access") == 0,
          "reason name is stable");
    return failures == 0 ? 0 : 1;
}
