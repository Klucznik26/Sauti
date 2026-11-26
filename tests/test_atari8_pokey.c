#include "../src/decoders/retro/atari8/atari8_pokey.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

static void test_period(unsigned divider, unsigned freq, bool ntsc)
{
    Atari8Pokey p;
    atari8_pokey_reset(&p, ntsc);
    CHECK(p.clock_hz == (ntsc ? 1789773u : 1773447u));
    CHECK(atari8_pokey_write(&p, 8, divider == 114));
    CHECK(atari8_pokey_write(&p, 0, (uint8_t)freq));
    CHECK(atari8_pokey_write(&p, 1, 0xaf));
    CHECK(atari8_pokey_write(&p, 9, 0));
    unsigned half = divider * (freq + 1);
    CHECK(atari8_pokey_level(&p) == 0);
    atari8_pokey_step(&p, half - 1);
    CHECK(atari8_pokey_level(&p) == 0);
    atari8_pokey_step(&p, 1);
    CHECK(atari8_pokey_level(&p) == 15);
    atari8_pokey_step(&p, half);
    CHECK(atari8_pokey_level(&p) == 0);
}

static void test_volume_and_rejection(void)
{
    Atari8Pokey p;
    atari8_pokey_reset(&p, false);
    CHECK(atari8_pokey_level(&p) == 0);
    for (unsigned ch = 0; ch < 4; ++ch)
        CHECK(atari8_pokey_write(&p, (uint8_t)(2 * ch + 1), 0x1f));
    CHECK(atari8_pokey_level(&p) == 60);
    atari8_pokey_step(&p, 10000);
    CHECK(atari8_pokey_level(&p) == 60);
    CHECK(!atari8_pokey_write(&p, 1, 0x8f));
    CHECK(!atari8_pokey_write(&p, 8, 0x40));
    CHECK(!atari8_pokey_write(&p, 255, 0));
    CHECK(p.audctl == 0 && p.audc[0] == 0x1f);
    CHECK(atari8_pokey_level(&p) == 60);
    CHECK(atari8_pokey_write(&p, 1, 0));
    CHECK(atari8_pokey_level(&p) == 45);
    atari8_pokey_reset(&p, true);
    CHECK(atari8_pokey_level(&p) == 0);
}

static void test_channels(void)
{
    Atari8Pokey p;
    atari8_pokey_reset(&p, false);
    for (unsigned ch = 0; ch < 4; ++ch) {
        CHECK(atari8_pokey_write(&p, (uint8_t)(2 * ch), (uint8_t)ch));
        CHECK(atari8_pokey_write(&p, (uint8_t)(2 * ch + 1),
                                (uint8_t)(0xa1 + ch)));
    }
    CHECK(atari8_pokey_write(&p, 9, 0));
    atari8_pokey_step(&p, 28);
    CHECK(atari8_pokey_level(&p) == 1);
    atari8_pokey_step(&p, 28);
    CHECK(atari8_pokey_level(&p) == 2);
    atari8_pokey_step(&p, 28);
    CHECK(atari8_pokey_level(&p) == 6);
    atari8_pokey_step(&p, 28);
    CHECK(atari8_pokey_level(&p) == 7);
}

int main(void)
{
    for (unsigned f = 0; f < 256; ++f) {
        test_period(28, f, false);
        test_period(114, f, true);
    }
    test_volume_and_rejection();
    test_channels();
    puts("PASS: experimental Atari8 POKEY");
    return 0;
}
