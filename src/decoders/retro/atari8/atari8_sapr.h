#ifndef ATARI8_SAPR_H
#define ATARI8_SAPR_H
#include "atari8_core.h"
#include "atari8_pcm.h"
#include "atari8_sap_time.h"
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
} Atari8SaprInfo;

typedef struct {
    Atari8SaprInfo info;
    Atari8Core core;
    Atari8Pcm pcm;
    size_t next_frame;
    uint32_t remaining;
    Atari8SaprResult terminal;
    bool nonlinear_mix; /* default true; choose after open, before next */
} Atari8Sapr;

Atari8SaprResult atari8_sapr_parse(const uint8_t *data, size_t size,
                                   Atari8SaprInfo *info);
Atari8SaprResult atari8_sapr_open(Atari8Sapr *s, const uint8_t *data,
                                  size_t size, uint32_t rate);
Atari8SaprResult atari8_sapr_next(Atari8Sapr *s, int16_t frame[2]);
#endif
