// SPDX-License-Identifier: GPL-3.0-or-later
#include "jaglink/evidence.h"

static bool write_json_string(FILE *stream, const char *text)
{
    const unsigned char *cursor = (const unsigned char *)text;

    if (fputc('"', stream) == EOF) return false;
    while (*cursor != 0U) {
        unsigned char value = *cursor++;
        if (value == '"' || value == '\\') {
            if (fputc('\\', stream) == EOF || fputc(value, stream) == EOF) {
                return false;
            }
        } else if (value == '\b') {
            if (fputs("\\b", stream) == EOF) return false;
        } else if (value == '\f') {
            if (fputs("\\f", stream) == EOF) return false;
        } else if (value == '\n') {
            if (fputs("\\n", stream) == EOF) return false;
        } else if (value == '\r') {
            if (fputs("\\r", stream) == EOF) return false;
        } else if (value == '\t') {
            if (fputs("\\t", stream) == EOF) return false;
        } else if (value < 0x20U) {
            if (fprintf(stream, "\\u%04x", (unsigned int)value) < 0) {
                return false;
            }
        } else if (fputc(value, stream) == EOF) {
            return false;
        }
    }
    return fputc('"', stream) != EOF;
}

static JaglinkEvidenceResult finish_record(JaglinkEvidenceWriter *writer)
{
    if (fputc('\n', writer->stream) == EOF || fflush(writer->stream) != 0) {
        writer->failed = true;
        return JAGLINK_EVIDENCE_IO_ERROR;
    }
    return JAGLINK_EVIDENCE_OK;
}

JaglinkEvidenceResult jaglink_evidence_open(
    JaglinkEvidenceWriter *writer, const char *path)
{
    if (writer == NULL || path == NULL || path[0] == '\0') {
        return JAGLINK_EVIDENCE_INVALID_ARGUMENT;
    }
    writer->stream = fopen(path, "ab");
    writer->failed = writer->stream == NULL;
    return writer->failed ? JAGLINK_EVIDENCE_IO_ERROR : JAGLINK_EVIDENCE_OK;
}

void jaglink_evidence_close(JaglinkEvidenceWriter *writer)
{
    if (writer == NULL) return;
    if (writer->stream != NULL) {
        if (fclose(writer->stream) != 0) writer->failed = true;
    }
    writer->stream = NULL;
}

JaglinkEvidenceResult jaglink_evidence_write_frame(
    JaglinkEvidenceWriter *writer,
    uint64_t timestamp_us,
    JaglinkEvidenceDirection direction,
    uint32_t arbitration_id,
    bool extended_id,
    const uint8_t *data,
    size_t data_size,
    const char *operator_annotation)
{
    size_t index;

    if (writer == NULL || writer->stream == NULL ||
        (data == NULL && data_size != 0U) ||
        (direction != JAGLINK_EVIDENCE_RX && direction != JAGLINK_EVIDENCE_TX)) {
        return JAGLINK_EVIDENCE_INVALID_ARGUMENT;
    }
    if (data_size > JAGLINK_EVIDENCE_MAX_FRAME_BYTES) {
        return JAGLINK_EVIDENCE_FRAME_TOO_LARGE;
    }
    if (writer->failed) return JAGLINK_EVIDENCE_IO_ERROR;

    if (fprintf(writer->stream,
                "{\"type\":\"traffic\",\"timestamp_us\":%llu,"
                "\"direction\":\"%s\",\"channel\":\"can-500k\","
                "\"arbitration_id\":\"0x%08x\",\"extended\":%s,"
                "\"data\":\"",
                (unsigned long long)timestamp_us,
                direction == JAGLINK_EVIDENCE_RX ? "rx" : "tx",
                (unsigned int)arbitration_id,
                extended_id ? "true" : "false") < 0) {
        writer->failed = true;
        return JAGLINK_EVIDENCE_IO_ERROR;
    }
    for (index = 0U; index < data_size; ++index) {
        if (fprintf(writer->stream, "%02x", (unsigned int)data[index]) < 0) {
            writer->failed = true;
            return JAGLINK_EVIDENCE_IO_ERROR;
        }
    }
    if (fputs("\",\"annotation\":", writer->stream) == EOF ||
        !write_json_string(writer->stream,
                           operator_annotation == NULL ? "" :
                           operator_annotation) ||
        fputc('}', writer->stream) == EOF) {
        writer->failed = true;
        return JAGLINK_EVIDENCE_IO_ERROR;
    }
    return finish_record(writer);
}

JaglinkEvidenceResult jaglink_evidence_write_annotation(
    JaglinkEvidenceWriter *writer,
    uint64_t timestamp_us,
    const char *operator_annotation)
{
    if (writer == NULL || writer->stream == NULL ||
        operator_annotation == NULL || operator_annotation[0] == '\0') {
        return JAGLINK_EVIDENCE_INVALID_ARGUMENT;
    }
    if (writer->failed) return JAGLINK_EVIDENCE_IO_ERROR;
    if (fprintf(writer->stream,
                "{\"type\":\"annotation\",\"timestamp_us\":%llu,"
                "\"text\":",
                (unsigned long long)timestamp_us) < 0 ||
        !write_json_string(writer->stream, operator_annotation) ||
        fputc('}', writer->stream) == EOF) {
        writer->failed = true;
        return JAGLINK_EVIDENCE_IO_ERROR;
    }
    return finish_record(writer);
}

const char *jaglink_evidence_result_name(JaglinkEvidenceResult result)
{
    switch (result) {
    case JAGLINK_EVIDENCE_OK: return "ok";
    case JAGLINK_EVIDENCE_INVALID_ARGUMENT: return "invalid-argument";
    case JAGLINK_EVIDENCE_IO_ERROR: return "io-error";
    case JAGLINK_EVIDENCE_FRAME_TOO_LARGE: return "frame-too-large";
    }
    return "unknown";
}
