#include "../src/decoders/retro/atari8/atari8_pcm.h"
#include "../src/decoders/retro/atari8/atari8_core.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

static void test_constant(uint32_t clock, uint32_t rate)
{
    Atari8Pcm p;
    CHECK(atari8_pcm_init(&p, clock, rate, 2, false));
    const uint8_t level[2] = {60, 30};
    int16_t frame[2] = {-1, -1};
    unsigned frames = 0;
    for (uint32_t n = 0; n < clock; ++n) {
        if (atari8_pcm_feed(&p, level, frame)) {
            CHECK(frame[0] == 32767 && frame[1] == 16384);
            ++frames;
        }
    }
    CHECK(frames == rate);
    CHECK(p.phase == 0 && p.area[0] == 0 && p.area[1] == 0);
}

static void test_fractional_boundary(void)
{
    Atari8Pcm p;
    const uint32_t clock = 1773447u, rate = 48000u;
    CHECK(atari8_pcm_init(&p, clock, rate, 1, false));
    const uint8_t silence[2] = {0, 60};
    const uint8_t high[2] = {60, 0};
    int16_t frame[2] = {123, 456};
    unsigned before = clock / rate;
    for (unsigned i = 0; i < before; ++i) {
        CHECK(!atari8_pcm_feed(&p, silence, frame));
        CHECK(frame[0] == 123 && frame[1] == 456);
    }
    CHECK(atari8_pcm_feed(&p, high, frame));
    uint32_t remainder = clock % rate;
    CHECK(frame[0] == (int16_t)(((uint64_t)remainder * 32767u
                               + clock / 2u) / clock));
    CHECK(frame[1] == 0);
    CHECK(p.phase == rate - remainder);
    CHECK(p.area[0] == (uint64_t)(rate - remainder) * 60u);
    while (!atari8_pcm_feed(&p, silence, frame)) {}
    CHECK(frame[0] == (int16_t)(((uint64_t)(rate - remainder) * 32767u
                               + clock / 2u) / clock));
    CHECK(frame[1] == 0);
}

static void test_dc(void)
{
    Atari8Pcm p;
    CHECK(atari8_pcm_init(&p, 1773447u, 48000u, 2, true));
    uint8_t level[2] = {60, 0};
    int16_t frame[2];
    unsigned frames = 0;
    while (frames < 48000) {
        if (!atari8_pcm_feed(&p, level, frame))
            continue;
        CHECK(frame[1] == 0);
        if (frames == 0)
            CHECK(frame[0] > 32000);
        ++frames;
    }
    CHECK(frame[0] == 0);
    level[0] = 0;
    while (!atari8_pcm_feed(&p, level, frame)) {}
    CHECK(frame[0] < -32000);
    CHECK(frame[1] == 0);
}

static void test_core_audio(void)
{
    Atari8Core c;
    Atari8Pcm p;
    atari8_core_reset(&c, false, true);
    CHECK(atari8_pcm_init(&p, c.pokey[0].clock_hz, 44100u, 2, false));
    CHECK(atari8_core_write(&c, 0xd200, 71));
    CHECK(atari8_core_write(&c, 0xd201, 0xaf));
    CHECK(atari8_core_write(&c, 0xd209, 0));
    unsigned frames = 0, nonzero = 0, silent = 0;
    for (uint32_t n = 0; n < c.pokey[0].clock_hz / 10u; ++n) {
        uint8_t levels[2];
        int16_t frame[2];
        atari8_core_levels(&c, levels);
        bool ready = atari8_pcm_feed(&p, levels, frame);
        atari8_core_step(&c, 1);
        if (ready) {
            ++frames;
            CHECK(frame[1] == 0 && frame[0] >= 0 && frame[0] <= 8192);
            nonzero += frame[0] > 0;
            silent += frame[0] == 0;
        }
    }
    CHECK(frames == 4409);
    CHECK(nonzero > 1000 && silent > 1000);
}

int main(void)
{
    Atari8Pcm p;
    CHECK(!atari8_pcm_init(NULL, 1773447u, 48000u, 2, true));
    CHECK(!atari8_pcm_init(&p, 0, 48000u, 2, true));
    CHECK(!atari8_pcm_init(&p, 1773447u, 0, 2, true));
    CHECK(!atari8_pcm_init(&p, 1773447u, 192001u, 2, true));
    CHECK(!atari8_pcm_init(&p, 1773447u, 48000u, 3, true));
    const uint32_t rates[] = {8000, 44100, 48000, 96000, 192000};
    for (unsigned i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
        test_constant(1773447u, rates[i]);
        test_constant(1789773u, rates[i]);
    }
    test_fractional_boundary();
    test_dc();
    test_core_audio();
    puts("PASS: experimental Atari8 PCM");
    return 0;
}
