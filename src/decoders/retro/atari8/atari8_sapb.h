#ifndef ATARI8_SAPB_H
#define ATARI8_SAPB_H
#include "atari8_machine.h"
#include "atari8_sap_time.h"
#include "atari8_sap_text.h"

typedef enum {
    ATARI8_SAPB_OK = 0, ATARI8_SAPB_BAD_FORMAT,
    ATARI8_SAPB_UNSUPPORTED, ATARI8_SAPB_RUNTIME, ATARI8_SAPB_EOF
} Atari8SapbResult;

typedef struct {
    size_t binary_offset;
    uint16_t init, player, music;
    char type; /* B, C, D or S; PLAYER is ignored for S. */
    unsigned songs, default_song;
    uint32_t interval;
    bool ntsc, stereo, has_player;
    Atari8SapTime time[256]; /* indexed by zero-based subsong */
    Atari8SapMetadata metadata;
} Atari8SapbInfo;

/* Internal host-scheduled decoder; no allocation, do not copy after open. */
typedef struct {
    Atari8Machine machine;
    Atari8SapbInfo info;
    Atari8SapbResult status;
    unsigned selected_song;
    uint64_t intervals, missed_intervals;
    bool frame_restart;
    uint64_t next_deadline, abandoned_calls, continuous_player_calls;
} Atari8Sapb;

Atari8SapbResult atari8_sapb_parse(const uint8_t *data, size_t size,
                                   Atari8SapbInfo *info);
/* song=-1 selects DEFSONG. All data is copied to machine RAM.
 * B/C INIT advances hardware but its PCM output is discarded.
 * D/S INIT runs continuously and contributes to PCM.
 * B/C/D/S default to nonlinear POKEY mixing. */
Atari8SapbResult atari8_sapb_open(Atari8Sapb *s, const uint8_t *data,
                                  size_t size, int song, uint32_t rate,
                                  Atari8MachineSink sink, void *context);
/* Explicit experimental playback policy for nonreturning legacy players.
 * Default open remains non-reentrant. The alternate mode abandons the
 * unfinished execution at a FASTPLAY deadline, retaining guest state. */
Atari8SapbResult atari8_sapb_open_mode(Atari8Sapb *s, const uint8_t *data,
                                  size_t size, int song, uint32_t rate,
                                  Atari8MachineSink sink, void *context,
                                  bool frame_restart);
/* B/C: call PLAYER; D: run INIT with optional periodic PLAYER.
 * S: run INIT with periodic SoftSynth RAM clock.
 * Advance to the next FASTPLAY grid boundary.
 * Overlong calls skip elapsed deadlines, bounded by a watchdog.
 * intervals counts B/C calls or D/S chunks; missed_intervals counts skipped calls.
 * Unlimited by default; optional declared EOF limits delivered PCM. */
Atari8SapbResult atari8_sapb_interval(Atari8Sapb *s);
/* Opt in before the first interval. Missing/invalid TIME returns false.
 * Valid zero duration is immediate EOF. LOOP remains informational.
 * The last interval may generate extra internal samples, but the sink
 * receives at most floor(TIME_ms * rate / 1000) frames. Genuine machine
 * faults remain errors even in the final interval. */
bool atari8_sapb_enable_declared_eof(Atari8Sapb *s);
/* Delivered timeline length, also usable with a NULL sink. */
uint64_t atari8_sapb_output_frames(const Atari8Sapb *s);
#endif
