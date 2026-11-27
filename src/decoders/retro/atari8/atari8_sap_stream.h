#ifndef ATARI8_SAP_STREAM_H
#define ATARI8_SAP_STREAM_H

#include "atari8_sapb_stream.h"
#include "atari8_sapr.h"

/* Common internal pull decoder for all five SAP types. Owns input.
 * Results use the executable SAP status enum for both backends.
 * Each output frame contains two int16_t values, right=0 for mono. */
typedef struct Atari8SapStream Atari8SapStream;
typedef struct {
    char type;
    unsigned channels, songs, default_song, selected_song;
    uint32_t rate;
    Atari8SapTime time;
    Atari8SapMetadata metadata;
} Atari8SapStreamInfo;

Atari8SapbResult atari8_sap_stream_open(Atari8SapStream **out,
    const uint8_t *data, size_t size, int song, uint32_t rate,
    bool declared_eof, bool frame_restart);
void atari8_sap_stream_close(Atari8SapStream *s);
const Atari8SapStreamInfo *atari8_sap_stream_info(const Atari8SapStream *s);
bool atari8_sap_stream_length(const Atari8SapStream *s, uint64_t *frames);
uint64_t atari8_sap_stream_tell(const Atari8SapStream *s);
Atari8SapbResult atari8_sap_stream_seek(Atari8SapStream *s,uint64_t position);
Atari8SapbResult atari8_sap_stream_reset(Atari8SapStream *s);
/* Repeat means restart the entire selected song, not a native loop
 * point. Enabled only for finite, nonempty streams. TIME LOOP alone
 * does not enable repeat. tell is position in the current repetition.
 * Errors are never treated as EOF. */
bool atari8_sap_stream_set_repeat(Atari8SapStream *s,bool repeat);
/* Same prefix/status contract as atari8_sapb_stream_read. */
Atari8SapbResult atari8_sap_stream_read(Atari8SapStream *s,
    int16_t (*out)[2], size_t capacity, size_t *produced);
#endif
