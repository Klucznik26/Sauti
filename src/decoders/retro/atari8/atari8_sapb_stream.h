#ifndef ATARI8_SAPB_STREAM_H
#define ATARI8_SAPB_STREAM_H

#include "atari8_sapb.h"

/* Internal buffered pull interface. Owns a copy of the input and an
 * immovable emulator. Output frames always contain two int16_t values;
 * mono uses the first and zeroes the second. No external dependencies.
 * Not yet a registered MaktabaSauti decoder. */
typedef struct Atari8SapbStream Atari8SapbStream;

/* Set *out to NULL on failure. Valid TIME is an optional EOF limit;
 * absent/invalid TIME leaves the stream unbounded. LOOP is metadata.
 * frame_restart selects the existing explicit alternate scheduler. */
Atari8SapbResult atari8_sapb_stream_open(Atari8SapbStream **out,
    const uint8_t *data, size_t size, int song, uint32_t rate,
    bool declared_eof, bool frame_restart);
void atari8_sapb_stream_close(Atari8SapbStream *s);

/* Read up to capacity frames. If less than requested, return EOF or an
 * error with *produced set to the valid prefix. A full read returns OK;
 * the next read reports a pending terminal condition after draining.
 * Allocation failure is RUNTIME. A zero-capacity read is a no-op. */
Atari8SapbResult atari8_sapb_stream_read(Atari8SapbStream *s,
    int16_t (*out)[2], size_t capacity, size_t *produced);
uint64_t atari8_sapb_stream_tell(const Atari8SapbStream *s);
bool atari8_sapb_stream_length(const Atari8SapbStream *s, uint64_t *frames);
const Atari8SapbInfo *atari8_sapb_stream_info(const Atari8SapbStream *s);

/* Replay privately from the start, discard the prefix and retain PCM
 * already generated beyond the requested position. On failure the
 * old stream is unchanged. Cost is proportional to target position. */
Atari8SapbResult atari8_sapb_stream_seek(Atari8SapbStream *s, uint64_t position);

#endif
