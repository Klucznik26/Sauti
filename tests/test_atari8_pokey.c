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
    CHECK(!atari8_pokey_write(&p, 8, 0x80));
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

static void test_fast(unsigned ch, unsigned freq)
{
    Atari8Pokey p;
    atari8_pokey_reset(&p, false);
    CHECK(atari8_pokey_write(&p, 8, ch == 0 ? 0x40 : 0x20));
    CHECK(atari8_pokey_write(&p, (uint8_t)(2 * ch), (uint8_t)freq));
    CHECK(atari8_pokey_write(&p, (uint8_t)(2 * ch + 1), 0xaf));
    CHECK(atari8_pokey_write(&p, 9, 0));
    atari8_pokey_step(&p, freq + 3);
    CHECK(atari8_pokey_level(&p) == 0);
    atari8_pokey_step(&p, 1);
    CHECK(atari8_pokey_level(&p) == 15);
    atari8_pokey_step(&p, freq + 4);
    CHECK(atari8_pokey_level(&p) == 0);
}

static void test_link(unsigned low, unsigned n, unsigned divider)
{
    Atari8Pokey p;
    atari8_pokey_reset(&p, false);
    uint8_t ctl = low == 0 ? 0x10 : 0x08;
    if (divider == 1)
        ctl |= low == 0 ? 0x40 : 0x20;
    if (divider == 114)
        ctl |= 1;
    CHECK(atari8_pokey_write(&p, 8, ctl));
    CHECK(atari8_pokey_write(&p, (uint8_t)(low * 2), (uint8_t)n));
    CHECK(atari8_pokey_write(&p, (uint8_t)(low * 2 + 2), (uint8_t)(n >> 8)));
    CHECK(atari8_pokey_write(&p, (uint8_t)(low * 2 + 3), 0xaf));
    CHECK(!atari8_pokey_write(&p, (uint8_t)(low * 2 + 1), 0xaf));
    CHECK(atari8_pokey_write(&p, 9, 0));
    uint32_t half = divider == 1 ? n + 7u : (n + 1u) * divider;
    atari8_pokey_step(&p, half - 1);
    CHECK(atari8_pokey_level(&p) == 0);
    atari8_pokey_step(&p, 1);
    CHECK(atari8_pokey_level(&p) == 15);
    atari8_pokey_step(&p, half);
    CHECK(atari8_pokey_level(&p) == 0);
}

static void test_mixed_clocks(void)
{
    Atari8Pokey p;
    atari8_pokey_reset(&p, false);
    CHECK(atari8_pokey_write(&p, 8, 0x40));
    CHECK(atari8_pokey_write(&p, 1, 0xa1));
    CHECK(atari8_pokey_write(&p, 3, 0xa2));
    CHECK(atari8_pokey_write(&p, 9, 0));
    atari8_pokey_step(&p, 4);
    CHECK(atari8_pokey_level(&p) == 1);
    atari8_pokey_step(&p, 24);
    CHECK(atari8_pokey_level(&p) == 3);
    CHECK(!atari8_pokey_write(&p, 8, 0x50));
    CHECK(p.audctl == 0x40);
    CHECK(atari8_pokey_write(&p, 1, 0x13));
    CHECK(atari8_pokey_write(&p, 8, 0x50));
    CHECK(atari8_pokey_level(&p) == 3);
}

int main(void)
{
    for (unsigned f = 0; f < 256; ++f) {
        test_period(28, f, false);
        test_period(114, f, true);
    }
    for (unsigned f = 0; f < 256; ++f) {
        test_fast(0, f);
        test_fast(2, f);
    }
    const unsigned values[] = {0, 1, 255, 256, 257, 0x1234, 65535};
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        for (unsigned low = 0; low <= 2; low += 2) {
            test_link(low, values[i], 1);
            test_link(low, values[i], 28);
            test_link(low, values[i], 114);
        }
    }
    test_mixed_clocks();
    test_volume_and_rejection();
    test_channels();
    puts("PASS: experimental Atari8 POKEY");
    return 0;
}
