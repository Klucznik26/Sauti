#ifndef ATARI8_SAPB_H
#define ATARI8_SAPB_H
#include "atari8_machine.h"

typedef enum {
    ATARI8_SAPB_OK = 0, ATARI8_SAPB_BAD_FORMAT,
    ATARI8_SAPB_UNSUPPORTED, ATARI8_SAPB_RUNTIME
} Atari8SapbResult;

typedef struct {
    size_t binary_offset;
    uint16_t init, player;
    unsigned songs, default_song;
    uint32_t interval;
    bool ntsc, stereo;
} Atari8SapbInfo;

/* Internal host-scheduled decoder; no allocation, do not copy after open. */
typedef struct {
    Atari8Machine machine;
    Atari8SapbInfo info;
    Atari8SapbResult status;
    uint64_t intervals;
} Atari8Sapb;

Atari8SapbResult atari8_sapb_parse(const uint8_t *data, size_t size,
                                   Atari8SapbInfo *info);
/* song=-1 selects DEFSONG. All data is copied to machine RAM.
 * INIT advances hardware but its PCM output is discarded. */
Atari8SapbResult atari8_sapb_open(Atari8Sapb *s, const uint8_t *data,
                                  size_t size, int song, uint32_t rate,
                                  Atari8MachineSink sink, void *context);
/* Call PLAYER and advance exactly one FASTPLAY interval on success.
 * Duration and end-of-song policy are left to the caller. */
Atari8SapbResult atari8_sapb_interval(Atari8Sapb *s);
#endif
