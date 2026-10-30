#ifndef SAUTI_YM_ARCHIVE_H
#define SAUTI_YM_ARCHIVE_H
#include <stddef.h>
#include <stdint.h>
/* Return 1 with newly allocated raw YM buffer, 0 if not an LHA archive,
 * and -1 for malformed/unsupported LHA. Caller owns *out on success. */
int sauti_ym_unpack_lha(const uint8_t *data,size_t size,
                        uint8_t **out,size_t *out_size);
#endif
