/* Original synthetic demonstration, no external music or ROM data. */
#include "../src/decoders/retro/atari8/atari8_core.h"
#include "../src/decoders/retro/atari8/atari8_pcm.h"
#include <stdio.h>
#include <stdlib.h>

static bool u16(FILE *f, uint16_t x)
{
    return fputc(x & 255, f) != EOF && fputc(x >> 8, f) != EOF;
}

static bool u32(FILE *f, uint32_t x)
{
    return u16(f, (uint16_t)x) && u16(f, (uint16_t)(x >> 16));
}

static bool tag(FILE *f, const char *s)
{
    return fwrite(s, 1, 4, f) == 4;
}

static bool header(FILE *f, uint32_t rate, uint32_t frames)
{
    uint32_t bytes = frames * 4u;
    return tag(f, "RIFF") && u32(f, 36u + bytes) && tag(f, "WAVE")
        && tag(f, "fmt ") && u32(f, 16) && u16(f, 1) && u16(f, 2)
        && u32(f, rate) && u32(f, rate * 4u) && u16(f, 4) && u16(f, 16)
        && tag(f, "data") && u32(f, bytes);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s OUTPUT.wav\n", argv[0]);
        return EXIT_FAILURE;
    }

    Atari8Core c;
    Atari8Pcm p;
    const uint32_t rate = 48000;
    const unsigned seconds = 4;
    const uint8_t left[] = {71, 59, 47, 71};
    const uint8_t right[] = {95, 89, 63, 95};
    atari8_core_reset(&c, false, true);
    uint32_t clock = c.pokey[0].clock_hz;
    if (!atari8_pcm_init(&p, clock, rate, 2, true))
        return EXIT_FAILURE;
    FILE *f = fopen(argv[1], "wb");
    if (!f) {
        perror(argv[1]);
        return EXIT_FAILURE;
    }
    bool ok = header(f, rate, rate * seconds);
    uint32_t frames = 0;
    for (unsigned s = 0; ok && s < seconds; ++s) {
        ok = atari8_core_write(&c, 0xd200, left[s])
          && atari8_core_write(&c, 0xd201, 0xaf)
          && atari8_core_write(&c, 0xd210, right[s])
          && atari8_core_write(&c, 0xd211, 0xa9)
          && atari8_core_write(&c, 0xd209, 0)
          && atari8_core_write(&c, 0xd219, 0);
        for (uint32_t n = 0; ok && n < clock; ++n) {
            uint8_t levels[2];
            int16_t frame[2];
            atari8_core_levels(&c, levels);
            bool ready = atari8_pcm_feed(&p, levels, frame);
            atari8_core_step(&c, 1);
            if (ready) {
                ok = u16(f, (uint16_t)frame[0]) && u16(f, (uint16_t)frame[1]);
                ++frames;
            }
        }
    }
    if (fclose(f) != 0)
        ok = false;
    if (!ok || frames != seconds * rate) {
        fprintf(stderr, "WAV generation failed\n");
        return EXIT_FAILURE;
    }
    printf("Wrote %u stereo PCM frames at %u Hz\n",
           (unsigned)frames, (unsigned)rate);
    return EXIT_SUCCESS;
}
