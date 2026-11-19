/* MaktabaSauti: wersje, nazwy i opisy kodow bledow, rozmiary formatow. */
#include "mks_internal.h"

uint32_t MAKTABASAUTI_CALL maktabasauti_abi_version(void) { return MAKTABASAUTI_ABI_VERSION; }
void MAKTABASAUTI_CALL maktabasauti_version(uint32_t *major, uint32_t *minor, uint32_t *patch) {
    if (major) *major = MAKTABASAUTI_VERSION_MAJOR;
    if (minor) *minor = MAKTABASAUTI_VERSION_MINOR;
    if (patch) *patch = MAKTABASAUTI_VERSION_PATCH;
}
const char *MAKTABASAUTI_CALL maktabasauti_version_string(void) { return MAKTABASAUTI_VERSION_STRING; }

typedef struct { maktabasauti_result_t code; const char *name; const char *message; } mks_result_text_t;
static const mks_result_text_t result_text[] = {
    { MAKTABASAUTI_EOF, "MAKTABASAUTI_EOF", "end of stream" },
    { MAKTABASAUTI_OK, "MAKTABASAUTI_OK", "success" },
    { MAKTABASAUTI_E_INVALID_ARGUMENT, "MAKTABASAUTI_E_INVALID_ARGUMENT", "invalid argument" },
    { MAKTABASAUTI_E_NO_MEMORY, "MAKTABASAUTI_E_NO_MEMORY", "out of memory" },
    { MAKTABASAUTI_E_IO, "MAKTABASAUTI_E_IO", "input/output error" },
    { MAKTABASAUTI_E_UNSUPPORTED_FORMAT, "MAKTABASAUTI_E_UNSUPPORTED_FORMAT", "no codec recognised the data" },
    { MAKTABASAUTI_E_UNSUPPORTED_FEATURE, "MAKTABASAUTI_E_UNSUPPORTED_FEATURE", "recognised, but this variant or feature is not supported" },
    { MAKTABASAUTI_E_CORRUPT_DATA, "MAKTABASAUTI_E_CORRUPT_DATA", "corrupt data" },
    { MAKTABASAUTI_E_TRUNCATED, "MAKTABASAUTI_E_TRUNCATED", "data ended in the middle of an element" },
    { MAKTABASAUTI_E_NOT_SEEKABLE, "MAKTABASAUTI_E_NOT_SEEKABLE", "not seekable" },
    { MAKTABASAUTI_E_OUT_OF_RANGE, "MAKTABASAUTI_E_OUT_OF_RANGE", "position out of range" },
    { MAKTABASAUTI_E_BAD_STATE, "MAKTABASAUTI_E_BAD_STATE", "call not allowed in this state" },
    { MAKTABASAUTI_E_ABI_MISMATCH, "MAKTABASAUTI_E_ABI_MISMATCH", "header and library ABI versions differ" },
    { MAKTABASAUTI_E_BUFFER_TOO_SMALL, "MAKTABASAUTI_E_BUFFER_TOO_SMALL", "buffer too small" },
    { MAKTABASAUTI_E_WOULD_BLOCK, "MAKTABASAUTI_E_WOULD_BLOCK", "no data available yet, retry later" },
    { MAKTABASAUTI_E_CANCELLED, "MAKTABASAUTI_E_CANCELLED", "cancelled" },
    { MAKTABASAUTI_E_NOT_IMPLEMENTED, "MAKTABASAUTI_E_NOT_IMPLEMENTED", "not implemented" },
    { MAKTABASAUTI_E_INTERNAL, "MAKTABASAUTI_E_INTERNAL", "internal error" },
    { MAKTABASAUTI_E_FORMAT_CHANGED, "MAKTABASAUTI_E_FORMAT_CHANGED", "stream parameters changed, query the stream info" }
};
const char *MAKTABASAUTI_CALL maktabasauti_result_name(maktabasauti_result_t result) {
    size_t i;
    for (i = 0; i < MKS_ARRAY_SIZE(result_text); i++) if (result_text[i].code == result) return result_text[i].name;
    return "MAKTABASAUTI_E_UNKNOWN";
}
const char *MAKTABASAUTI_CALL maktabasauti_result_message(maktabasauti_result_t result) {
    size_t i;
    for (i = 0; i < MKS_ARRAY_SIZE(result_text); i++) if (result_text[i].code == result) return result_text[i].message;
    return "unknown result code";
}

size_t MAKTABASAUTI_CALL maktabasauti_format_bytes(maktabasauti_sample_format_t format) {
    switch (format) {
    case MAKTABASAUTI_FORMAT_U8: return 1;
    case MAKTABASAUTI_FORMAT_S16: return 2;
    case MAKTABASAUTI_FORMAT_S24: return 3;
    case MAKTABASAUTI_FORMAT_S32: return 4;
    case MAKTABASAUTI_FORMAT_F32: return 4;
    case MAKTABASAUTI_FORMAT_F64: return 8;
    default: return 0;
    }
}
