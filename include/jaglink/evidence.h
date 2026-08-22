// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file evidence.h
 * @brief Append-only JSON Lines evidence output for raw CAN traffic.
 */
#ifndef JAGLINK_EVIDENCE_H
#define JAGLINK_EVIDENCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JAGLINK_EVIDENCE_MAX_FRAME_BYTES 4096U

typedef enum {
    JAGLINK_EVIDENCE_RX = 0,
    JAGLINK_EVIDENCE_TX
} JaglinkEvidenceDirection;

typedef enum {
    JAGLINK_EVIDENCE_OK = 0,
    JAGLINK_EVIDENCE_INVALID_ARGUMENT,
    JAGLINK_EVIDENCE_IO_ERROR,
    JAGLINK_EVIDENCE_FRAME_TOO_LARGE
} JaglinkEvidenceResult;

typedef struct {
    FILE *stream;
    bool failed;
} JaglinkEvidenceWriter;

#define JAGLINK_EVIDENCE_WRITER_INIT { .stream = NULL, .failed = false }

JaglinkEvidenceResult jaglink_evidence_open(
    JaglinkEvidenceWriter *writer, const char *path);
void jaglink_evidence_close(JaglinkEvidenceWriter *writer);

JaglinkEvidenceResult jaglink_evidence_write_frame(
    JaglinkEvidenceWriter *writer,
    uint64_t timestamp_us,
    JaglinkEvidenceDirection direction,
    uint32_t arbitration_id,
    bool extended_id,
    const uint8_t *data,
    size_t data_size,
    const char *operator_annotation);

JaglinkEvidenceResult jaglink_evidence_write_annotation(
    JaglinkEvidenceWriter *writer,
    uint64_t timestamp_us,
    const char *operator_annotation);

const char *jaglink_evidence_result_name(JaglinkEvidenceResult result);

#ifdef __cplusplus
}
#endif

#endif
