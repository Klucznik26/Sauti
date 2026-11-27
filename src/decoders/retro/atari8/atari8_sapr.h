#ifndef ATARI8_SAPR_H
#define ATARI8_SAPR_H
#include "atari8_core.h"
#include "atari8_pcm.h"
#include "atari8_sap_time.h"
#include "atari8_sap_text.h"
#include <stddef.h>

typedef enum {
    ATARI8_SAPR_OK = 0,
    ATARI8_SAPR_BAD_FORMAT,
    ATARI8_SAPR_UNSUPPORTED,
    ATARI8_SAPR_EOF
} Atari8SaprResult;

/* Borrowed immutable input: keep it alive until playback ends. */
typedef struct {
    const uint8_t *data;
    size_t frame_count;
    uint32_t cycles_per_frame;
    bool ntsc, stereo;
    Atari8SapTime time; /* informational; payload determines EOF */
    Atari8SapMetadata metadata;
} Atari8SaprInfo;

typedef struct {
    Atari8SaprInfo info;
    Atari8Core core;
    Atari8Pcm pcm;
    size_t next_frame;
    uint32_t remaining;
    uint64_t position, total_frames; /* PCM frames, not register frames */
    Atari8SaprResult terminal;
    bool nonlinear_mix; /* default true; choose after open, before next */
} Atari8Sapr;

Atari8SaprResult atari8_sapr_parse(const uint8_t *data, size_t size,
                                   Atari8SaprInfo *info);
Atari8SaprResult atari8_sapr_open(Atari8Sapr *s, const uint8_t *data,
                                  size_t size, uint32_t rate);
Atari8SaprResult atari8_sapr_next(Atari8Sapr *s, int16_t frame[2]);
/* Successful open required for these functions. Seek is sample-exact:
 * forward playback discards PCM; backwards replay resets all state.
 * Positions beyond the payload are rejected, state unchanged on error.
 * Time cost is proportional to the replayed audio duration. */
uint64_t atari8_sapr_tell(const Atari8Sapr *s);
uint64_t atari8_sapr_length(const Atari8Sapr *s);
Atari8SaprResult atari8_sapr_seek(Atari8Sapr *s, uint64_t position);
#endif
