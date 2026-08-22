// SPDX-License-Identifier: GPL-3.0-or-later
#include "jaglink/evidence.h"

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

int main(void)
{
    const char *path = "jaglink-evidence-test.jsonl";
    const uint8_t frame[] = {0x02U, 0x09U, 0x02U, 0x55U};
    JaglinkEvidenceWriter writer = JAGLINK_EVIDENCE_WRITER_INIT;
    char content[2048];
    FILE *stream;
    size_t size;

    (void)remove(path);
    check(jaglink_evidence_open(&writer, path) == JAGLINK_EVIDENCE_OK,
          "evidence file opens");
    check(jaglink_evidence_write_frame(
              &writer, UINT64_C(123456789), JAGLINK_EVIDENCE_RX,
              UINT32_C(0x7e8), false, frame, sizeof(frame),
              "key on \"engine off\"") == JAGLINK_EVIDENCE_OK,
          "frame record writes");
    check(jaglink_evidence_write_frame(
              &writer, 0U, JAGLINK_EVIDENCE_RX, 0U, false, frame,
              JAGLINK_EVIDENCE_MAX_FRAME_BYTES + 1U, "") ==
              JAGLINK_EVIDENCE_FRAME_TOO_LARGE,
          "oversized frame is rejected before data access");
    check(jaglink_evidence_write_annotation(
              &writer, UINT64_C(123456999), "operator\nannotation") ==
              JAGLINK_EVIDENCE_OK,
          "annotation record writes");
    jaglink_evidence_close(&writer);

    stream = fopen(path, "rb");
    check(stream != NULL, "evidence file reopens");
    if (stream != NULL) {
        size = fread(content, 1U, sizeof(content) - 1U, stream);
        content[size] = '\0';
        check(fclose(stream) == 0, "evidence file closes");
        check(strstr(content, "\"timestamp_us\":123456789") != NULL,
              "timestamp is exact");
        check(strstr(content, "\"arbitration_id\":\"0x000007e8\"") != NULL,
              "arbitration ID is exact");
        check(strstr(content, "\"data\":\"02090255\"") != NULL,
              "raw bytes are preserved");
        check(strstr(content, "key on \\\"engine off\\\"") != NULL,
              "quotes are escaped");
        check(strstr(content, "operator\\nannotation") != NULL,
              "annotation controls are escaped");
        check(strchr(content, '\n') != strrchr(content, '\n'),
              "records are JSON Lines");
    }
    check(remove(path) == 0, "test evidence removed");
    return failures == 0 ? 0 : 1;
}
