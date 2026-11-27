#ifndef ATARI8_PCM_H
#define ATARI8_PCM_H

#include <stdbool.h>
#include <stdint.h>

/* Independent streaming integrator. Feed one master cycle at a time,
 * including cycles spent running CPU instructions or applying writes.
 * No allocation, global state, or external dependencies. */
typedef struct {
    uint32_t clock_hz, rate, phase;
    uint64_t area[2];
    double previous[2], dc_state[2], alpha;
    unsigned channels;
    bool dc_block;
} Atari8Pcm;

/* PAL/NTSC master clocks, 8000..192000 Hz, one or two channels. */
bool atari8_pcm_init(Atari8Pcm *p, uint32_t clock_hz, uint32_t rate,
                     unsigned channels, bool dc_block);
/* Supply valid digital levels 0..60, held for the entire master cycle.
 * Returns true and writes a frame when its sample boundary is reached.
 * At most one frame per cycle; frame remains unchanged otherwise.
 * With dc_block=false, output is unipolar diagnostic PCM (0..32767).
 * With dc_block=true, signed DC-blocked PCM is suitable for listening.
 * In mono the second input is ignored and second output is zero. */
bool atari8_pcm_feed(Atari8Pcm *p, const uint8_t levels[2], int16_t frame[2]);

/* Same integrator with unsigned 16-bit normalized input. Do not switch
 * input scale midstream; select a single feed function after init. */
bool atari8_pcm_feed_u16(Atari8Pcm *p, const uint16_t levels[2], int16_t frame[2]);

#endif
