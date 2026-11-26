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
    CHECK(!atari8_pokey_write(&p, 14, 0x80));
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
    CHECK(atari8_pokey_level(&p) == 8);
    atari8_pokey_step(&p, 28);
    CHECK(atari8_pokey_level(&p) == 9);
    atari8_pokey_step(&p, 28);
    CHECK(atari8_pokey_level(&p) == 7);
    atari8_pokey_step(&p, 28);
    CHECK(atari8_pokey_level(&p) == 0);
}

static void test_fast(unsigned ch, unsigned freq)
{
    Atari8Pokey p;
    atari8_pokey_reset(&p, false);
    CHECK(atari8_pokey_write(&p, 8, ch == 0 ? 0x40 : 0x20));
    CHECK(atari8_pokey_write(&p, (uint8_t)(2 * ch), (uint8_t)freq));
    CHECK(atari8_pokey_write(&p, (uint8_t)(2 * ch + 1), 0xaf));
    CHECK(atari8_pokey_write(&p, 9, 0));
    unsigned initial = ch < 2 ? 0 : 15;
    atari8_pokey_step(&p, freq + 3);
    CHECK(atari8_pokey_level(&p) == initial);
    atari8_pokey_step(&p, 1);
    CHECK(atari8_pokey_level(&p) == 15 - initial);
    atari8_pokey_step(&p, freq + 4);
    CHECK(atari8_pokey_level(&p) == initial);
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
    CHECK(atari8_pokey_write(&p, (uint8_t)(low * 2 + 1), 0xa0));
    CHECK(atari8_pokey_write(&p, 9, 0));
    uint32_t half = divider == 1 ? n + 7u : (n + 1u) * divider;
    unsigned initial = low == 0 ? 0 : 15;
    atari8_pokey_step(&p, half - 1);
    CHECK(atari8_pokey_level(&p) == initial);
    atari8_pokey_step(&p, 1);
    CHECK(atari8_pokey_level(&p) == 15 - initial);
    atari8_pokey_step(&p, half);
    CHECK(atari8_pokey_level(&p) == initial);
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
    CHECK(atari8_pokey_write(&p, 8, 0x50));
    CHECK(p.audctl == 0x50);
    CHECK(atari8_pokey_write(&p, 1, 0x13));
    CHECK(atari8_pokey_level(&p) == 3);
}

/* Verify full polynomial cycles, uniqueness and output balance. */
static void test_polynomials(void)
{
    static const unsigned periods[] = {15, 31, 511, 131071};
    for (unsigned i = 0; i < 4; ++i) {
        Atari8Pokey p;
        atari8_pokey_reset(&p, false);
        unsigned initial = p.poly[i];
        unsigned ones = 0;
        for (unsigned n = 0; n < periods[i]; ++n) {
            atari8_pokey_step(&p, 1);
            CHECK(p.poly[i] != 0);
            CHECK((p.poly[i] == initial) == (n + 1 == periods[i]));
            ones += p.noise_history[i] & 1u;
        }
        CHECK(ones == (periods[i] + (i < 2 ? 0u : 1u)) / 2u);
    }
}

/* Documented short-polynomial bit patterns, allowing a cyclic rotation
 * because hardware initialization phase is not yet implemented. */
static void test_short_pattern(unsigned i, const char *pattern, unsigned n)
{
    Atari8Pokey p;
    char observed[31];
    atari8_pokey_reset(&p, false);
    for (unsigned j = 0; j < n; ++j) {
        atari8_pokey_step(&p, 1);
        observed[j] = (char)('0' + (p.noise_history[i] & 1u));
    }
    unsigned matches = 0;
    for (unsigned offset = 0; offset < n; ++offset) {
        unsigned j;
        for (j = 0; j < n; ++j)
            if (observed[j] != pattern[(offset + j) % n])
                break;
        matches += j == n;
    }
    CHECK(matches == 1);
}

static void test_waveforms(void)
{
    /* Independent bit-selection expectations at successive timer events,
     * including the five-bit gate and per-channel delay. */
    for (unsigned short_noise = 0; short_noise < 2; ++short_noise) {
        for (unsigned mode = 0; mode < 8; ++mode) {
            Atari8Pokey p;
            uint8_t expected[4] = {1, 1, 1, 1};
            atari8_pokey_reset(&p, false);
            CHECK(atari8_pokey_write(&p, 8, short_noise ? 0x80 : 0));
            for (unsigned ch = 0; ch < 4; ++ch)
                CHECK(atari8_pokey_write(&p, (uint8_t)(2 * ch + 1),
                                        (uint8_t)(mode * 32 + 15)));
            CHECK(atari8_pokey_write(&p, 9, 0));
            for (unsigned n = 0; n < 1000; ++n) {
                atari8_pokey_step(&p, 28);
                for (unsigned ch = 0; ch < 4; ++ch) {
                    if ((mode >= 4) || ((p.noise_history[1] >> ch) & 1)) {
                        if (mode & 1)
                            expected[ch] = 1 - expected[ch];
                        else {
                            unsigned source = (mode & 2) ? 0 :
                                              (short_noise ? 2 : 3);
                            expected[ch] = (p.noise_history[source] >> ch) & 1;
                        }
                    }
                    CHECK(p.output[ch] == expected[ch]);
                }
            }
        }
    }
    Atari8Pokey p;
    atari8_pokey_reset(&p, false);
    CHECK(atari8_pokey_write(&p, 1, 0xaf));
    CHECK(atari8_pokey_write(&p, 9, 0));
    atari8_pokey_step(&p, 27);
    CHECK(atari8_pokey_write(&p, 8, 0x80));
    atari8_pokey_step(&p, 1);
    CHECK(atari8_pokey_level(&p) == 15);
}

static void test_filter(unsigned low)
{
    Atari8Pokey p;
    uint8_t ctl = low == 0 ? 4 : 2;
    atari8_pokey_reset(&p, false);
    CHECK(atari8_pokey_write(&p, 8, ctl));
    CHECK(atari8_pokey_write(&p, (uint8_t)(2 * low), 1));
    CHECK(atari8_pokey_write(&p, (uint8_t)(2 * low + 1), 0xaf));
    CHECK(atari8_pokey_write(&p, (uint8_t)(2 * low + 4), 2));
    CHECK(atari8_pokey_write(&p, 9, 0));
    CHECK(atari8_pokey_level(&p) == 0);
    atari8_pokey_step(&p, 56);
    CHECK(atari8_pokey_level(&p) == 15);
    atari8_pokey_step(&p, 28);
    CHECK(atari8_pokey_level(&p) == 15);
    atari8_pokey_step(&p, 1);
    CHECK(atari8_pokey_level(&p) == 15);
    atari8_pokey_step(&p, 1);
    CHECK(atari8_pokey_level(&p) == 0);
    CHECK(atari8_pokey_write(&p, (uint8_t)(2 * low + 1), 0x1f));
    CHECK(atari8_pokey_level(&p) == 15);
    CHECK(atari8_pokey_write(&p, (uint8_t)(2 * low + 1), 0xaf));
    CHECK(atari8_pokey_level(&p) == 0);
    atari8_pokey_step(&p, 26);
    CHECK(atari8_pokey_level(&p) == 15);
    CHECK(atari8_pokey_write(&p, 8, 0));
    CHECK(atari8_pokey_level(&p) == 0);
}

static void test_polarity(void)
{
    Atari8Pokey p;
    atari8_pokey_reset(&p, false);
    CHECK(atari8_pokey_write(&p, 1, 0xaf));
    CHECK(atari8_pokey_write(&p, 5, 0xaf));
    CHECK(atari8_pokey_write(&p, 9, 0));
    for (unsigned n = 0; n < 20; ++n) {
        CHECK(atari8_pokey_level(&p) == 15);
        atari8_pokey_step(&p, 28);
    }
    CHECK(atari8_pokey_write(&p, 1, 0x1f));
    CHECK(atari8_pokey_write(&p, 5, 0x1f));
    CHECK(atari8_pokey_level(&p) == 30);
}

static void check_irq(const Atari8Pokey *p, uint8_t expected)
{
    uint8_t value = 0;
    CHECK(atari8_pokey_read(p, 14, &value));
    CHECK(value == expected);
    CHECK(atari8_pokey_irq(p) == (expected != 255u));
    CHECK(atari8_pokey_read(p, 14, &value));
    CHECK(value == expected);
}

static void test_irq_latches(unsigned divider)
{
    Atari8Pokey p;
    atari8_pokey_reset(&p, false);
    check_irq(&p, 0xff);
    CHECK(atari8_pokey_write(&p, 8, divider == 114));
    CHECK(atari8_pokey_write(&p, 0, 1));
    CHECK(atari8_pokey_write(&p, 2, 2));
    CHECK(atari8_pokey_write(&p, 4, 0)); /* timer 3: no IRQ */
    CHECK(atari8_pokey_write(&p, 6, 3));
    CHECK(atari8_pokey_write(&p, 9, 0));
    CHECK(atari8_pokey_write(&p, 14, 7));
    atari8_pokey_step(&p, divider * 2 - 1);
    check_irq(&p, 0xff);
    atari8_pokey_step(&p, 1);
    check_irq(&p, 0xfe);
    atari8_pokey_step(&p, divider);
    check_irq(&p, 0xfc);
    atari8_pokey_step(&p, divider);
    check_irq(&p, 0xf8);
    CHECK(atari8_pokey_write(&p, 9, 0));
    check_irq(&p, 0xf8); /* STIMER is not IRQ acknowledgment. */
    CHECK(atari8_pokey_write(&p, 14, 6));
    check_irq(&p, 0xf9);
    CHECK(atari8_pokey_write(&p, 14, 4));
    check_irq(&p, 0xfb);
    CHECK(atari8_pokey_write(&p, 14, 0));
    check_irq(&p, 0xff);
    atari8_pokey_step(&p, divider * 12);
    check_irq(&p, 0xff);
    CHECK(atari8_pokey_write(&p, 14, 7));
    check_irq(&p, 0xff); /* no retroactive IRQ for disabled events */
    atari8_pokey_step(&p, divider * 2);
    check_irq(&p, 0xfe);
    atari8_pokey_reset(&p, true);
    check_irq(&p, 0xff);
    CHECK(p.irq_enable == 0);
}

static void test_irq_fast_and_linked(void)
{
    Atari8Pokey p;
    atari8_pokey_reset(&p, false);
    CHECK(atari8_pokey_write(&p, 8, 0x40));
    CHECK(atari8_pokey_write(&p, 0, 9));
    CHECK(atari8_pokey_write(&p, 9, 0));
    CHECK(atari8_pokey_write(&p, 14, 1));
    atari8_pokey_step(&p, 12);
    check_irq(&p, 0xff);
    atari8_pokey_step(&p, 1);
    check_irq(&p, 0xfe);
    CHECK(atari8_pokey_write(&p, 8, 0x50));
    CHECK(!atari8_pokey_write(&p, 14, 0xff));
    check_irq(&p, 0xfe);
    CHECK(p.audctl == 0x50 && p.irq_enable == 1);
    CHECK(atari8_pokey_write(&p, 14, 0));
    CHECK(atari8_pokey_write(&p, 8, 0x78)); /* both linked, fast */
    CHECK(atari8_pokey_write(&p, 14, 1));
    CHECK(atari8_pokey_write(&p, 0, 1));
    CHECK(atari8_pokey_write(&p, 2, 1));
    CHECK(atari8_pokey_write(&p, 4, 2));
    CHECK(atari8_pokey_write(&p, 6, 1));
    CHECK(atari8_pokey_write(&p, 14, 6));
    CHECK(atari8_pokey_write(&p, 9, 0));
    atari8_pokey_step(&p, 263);
    check_irq(&p, 0xff);
    atari8_pokey_step(&p, 1);
    check_irq(&p, 0xfd);
    atari8_pokey_step(&p, 1);
    check_irq(&p, 0xf9);
    uint8_t v = 42;
    CHECK(atari8_pokey_read(&p, 0, &v) && v == 228);
    v = 42;
    CHECK(!atari8_pokey_read(&p, 16, &v));
    CHECK(v == 42);
    CHECK(!atari8_pokey_read(&p, 14, NULL));
}

/* Example $0140: two low pulses per high period. In fast mode the
 * low pulse precedes the high pulse by three modeled master cycles. */
static void test_linked_low(unsigned low, unsigned divider)
{
    Atari8Pokey p;
    atari8_pokey_reset(&p, false);
    uint8_t ctl = low == 0 ? 0x10 : 0x08;
    if (divider == 1)
        ctl |= low == 0 ? 0x40 : 0x20;
    if (divider == 114)
        ctl |= 1;
    CHECK(atari8_pokey_write(&p, 8, ctl));
    CHECK(atari8_pokey_write(&p, (uint8_t)(2 * low), 0x40));
    CHECK(atari8_pokey_write(&p, (uint8_t)(2 * low + 2), 1));
    CHECK(atari8_pokey_write(&p, (uint8_t)(2 * low + 1), 0xaf));
    CHECK(atari8_pokey_write(&p, (uint8_t)(2 * low + 3), 0xa7));
    CHECK(atari8_pokey_write(&p, 14, low == 0 ? 3 : 4));
    CHECK(atari8_pokey_write(&p, 9, 0));
    unsigned first = divider == 1 ? 68 : 65 * divider;
    unsigned gap = 256 * divider;
    unsigned lag = divider == 1 ? 3 : 0;
    uint8_t high_irq = low == 0 ? 2 : 4;
    for (unsigned period = 0; period < 3; ++period) {
        uint8_t high_before = p.output[low + 1];
        CHECK(p.output[low] == 1);
        atari8_pokey_step(&p, first - 1);
        CHECK(p.output[low] == 1 && p.output[low + 1] == high_before);
        check_irq(&p, 0xff);
        atari8_pokey_step(&p, 1);
        CHECK(p.output[low] == 0 && p.output[low + 1] == high_before);
        check_irq(&p, low == 0 ? 0xfe : 0xff);
        atari8_pokey_step(&p, gap - 1);
        CHECK(p.output[low] == 0 && p.output[low + 1] == high_before);
        atari8_pokey_step(&p, 1);
        CHECK(p.output[low] == 1);
        if (lag) {
            CHECK(p.output[low + 1] == high_before);
            atari8_pokey_step(&p, lag);
        }
        CHECK(p.output[low + 1] != high_before);
        check_irq(&p, (uint8_t)~(high_irq | (low == 0 ? 1u : 0u)));
        CHECK(atari8_pokey_write(&p, 14, 0));
        CHECK(atari8_pokey_write(&p, 14, low == 0 ? 3 : 4));
    }
}

static void test_linked_filter(void)
{
    Atari8Pokey p;
    atari8_pokey_reset(&p, false);
    CHECK(atari8_pokey_write(&p, 8, 0x0c));
    CHECK(atari8_pokey_write(&p, 0, 1));
    CHECK(atari8_pokey_write(&p, 1, 0xaf));
    CHECK(atari8_pokey_write(&p, 4, 2));
    CHECK(atari8_pokey_write(&p, 6, 1));
    CHECK(atari8_pokey_write(&p, 9, 0));
    atari8_pokey_step(&p, 84);
    CHECK(atari8_pokey_level(&p) == 15);
    atari8_pokey_step(&p, 2);
    CHECK(atari8_pokey_level(&p) == 0);
    CHECK(p.filter_latch[0] == 0);
    CHECK(atari8_pokey_write(&p, 8, 0x0e)); /* both filters with linked pair */
}

static uint8_t random_byte(const Atari8Pokey *p)
{
    uint8_t value = 0;
    CHECK(atari8_pokey_read(p, 10, &value));
    return value;
}

static void test_random(bool short_mode)
{
    Atari8Pokey p;
    atari8_pokey_reset(&p, false);
    CHECK(atari8_pokey_write(&p, 8, short_mode ? 0x80 : 0));
    atari8_pokey_step(&p, 197);
    CHECK(atari8_pokey_write(&p, 15, 0));
    atari8_pokey_step(&p, 9);
    CHECK(random_byte(&p) == 255);
    atari8_pokey_step(&p, 9);
    CHECK(random_byte(&p) == 255);
    CHECK(p.poly[0] == 15u && p.poly[1] == 31u);
    CHECK(p.poly[2] == 511u && p.poly[3] == 131071u);
    atari8_pokey_step(&p, 200);
    CHECK(random_byte(&p) == 255);
    CHECK(atari8_pokey_write(&p, 15, 3));
    /* From the all-ones seed the first three feedback bits are zero. */
    for (unsigned i = 0; i < 3; ++i) {
        atari8_pokey_step(&p, 1);
        CHECK(random_byte(&p) == (255u >> (i + 1)));
    }
    CHECK(!atari8_pokey_write(&p, 15, 0x0b));
    CHECK(p.skctl == 3);
}

static void test_random_scanline(void)
{
    Atari8Pokey p;
    atari8_pokey_reset(&p, false);
    CHECK(atari8_pokey_write(&p, 8, 0x80));
    /* Find the unique 9-bit state whose visible byte is zero. */
    unsigned n = 0;
    while (random_byte(&p) != 0 && n++ < 511)
        atari8_pokey_step(&p, 1);
    CHECK(random_byte(&p) == 0);
    /* Independently documented 114-cycle sample progression. */
    static const uint8_t sequence[] = {0xdf, 0xee, 0x16, 0xb9};
    for (unsigned i = 0; i < sizeof(sequence); ++i) {
        atari8_pokey_step(&p, 114);
        CHECK(random_byte(&p) == sequence[i]);
    }
}

static void test_skctl_clocks(unsigned divider)
{
    Atari8Pokey p;
    atari8_pokey_reset(&p, false);
    CHECK(atari8_pokey_write(&p, 8, (uint8_t)(0x40 | (divider == 114))));
    CHECK(atari8_pokey_write(&p, 0, 0));
    CHECK(atari8_pokey_write(&p, 2, 0));
    CHECK(atari8_pokey_write(&p, 1, 0xaf));
    CHECK(atari8_pokey_write(&p, 3, 0xaf));
    CHECK(atari8_pokey_write(&p, 9, 0));
    CHECK(atari8_pokey_write(&p, 14, 3));
    CHECK(atari8_pokey_write(&p, 15, 0));
    atari8_pokey_step(&p, 4);
    CHECK(p.output[0] == 0 && p.output[1] == 1);
    check_irq(&p, 0xfe);
    atari8_pokey_step(&p, 400);
    CHECK(p.output[1] == 1 && p.counter[1] == 1);
    CHECK(atari8_pokey_write(&p, 15, 1));
    unsigned first = divider == 114 ? 78u : 19u;
    atari8_pokey_step(&p, first - 1);
    CHECK(p.output[1] == 1);
    atari8_pokey_step(&p, 1);
    CHECK(p.output[1] == 0);
    check_irq(&p, 0xfc);
    atari8_pokey_step(&p, divider);
    CHECK(p.output[1] == 1);
}

static void test_headless_inputs(void)
{
    Atari8Pokey p;
    uint8_t v;
    atari8_pokey_reset(&p, false);
    CHECK(atari8_pokey_read(&p,8,&v) && v==0);
    CHECK(atari8_pokey_write(&p,8,0x40));
    CHECK(atari8_pokey_read(&p,8,&v) && v==0 && p.audctl==0x40);
    CHECK(atari8_pokey_write(&p,11,0));
    CHECK(atari8_pokey_read(&p,8,&v) && v==255);
    CHECK(atari8_pokey_read(&p,7,&v) && v==0);
    atari8_pokey_step(&p,114u*227u);
    CHECK(atari8_pokey_read(&p,8,&v) && v==255);
    CHECK(atari8_pokey_read(&p,0,&v) && v==227);
    atari8_pokey_step(&p,114);
    CHECK(atari8_pokey_read(&p,8,&v) && v==0);
    CHECK(atari8_pokey_read(&p,7,&v) && v==228);
    CHECK(atari8_pokey_write(&p,15,7));
    CHECK(atari8_pokey_write(&p,11,0));
    atari8_pokey_step(&p,227);
    CHECK(atari8_pokey_read(&p,8,&v) && v==255);
    atari8_pokey_step(&p,1);
    CHECK(atari8_pokey_read(&p,8,&v) && v==0);
    CHECK(atari8_pokey_read(&p,9,&v) && v==255);
    CHECK(atari8_pokey_read(&p,15,&v) && v==255);
    CHECK(atari8_pokey_write(&p,10,0));
    CHECK(atari8_pokey_write(&p,13,0xa5) && p.serout==0xa5);
    CHECK(atari8_pokey_read(&p,13,&v) && v==0xff);
    CHECK(!atari8_pokey_write(&p,15,0x0b));
    CHECK(!atari8_pokey_read(&p,16,&v));
}

static void test_two_tone_break(void)
{
    Atari8Pokey p;
    atari8_pokey_reset(&p,false);
    CHECK(atari8_pokey_write(&p,0,4)); /* timer 1 period 5 ticks */
    CHECK(atari8_pokey_write(&p,2,2)); /* timer 2 period 3 ticks */
    CHECK(atari8_pokey_write(&p,1,0xaf));
    CHECK(atari8_pokey_write(&p,3,0xaf));
    CHECK(atari8_pokey_write(&p,15,0x8b));
    CHECK(atari8_pokey_write(&p,14,3));
    CHECK(atari8_pokey_write(&p,9,0));
    atari8_pokey_step(&p,84);
    CHECK(p.counter[0]==5 && p.counter[1]==3);
    CHECK(p.output[0]==1 && p.output[1]==0);
    CHECK(p.irq_pending==2);
    atari8_pokey_step(&p,84);
    CHECK(p.counter[0]==5 && p.output[0]==1 && p.output[1]==1);
    CHECK(p.irq_pending==2);
    CHECK(atari8_pokey_write(&p,15,3));
    atari8_pokey_step(&p,140);
    CHECK(p.output[0]==0 && (p.irq_pending & 1u));
    CHECK(atari8_pokey_write(&p,15,0x20));
    CHECK(p.skctl==0x20 && p.phase28==9 && p.phase114==36);
    atari8_pokey_step(&p,114);
    CHECK(p.phase28==9 && p.phase114==36);
    CHECK(!atari8_pokey_write(&p,15,0x23));
}

static void test_async_idle(void)
{
    Atari8Pokey p;
    atari8_pokey_reset(&p,false);
    CHECK(atari8_pokey_write(&p,1,0xaf));
    CHECK(atari8_pokey_write(&p,5,0xaf));
    CHECK(atari8_pokey_write(&p,7,0xaf));
    CHECK(atari8_pokey_write(&p,14,5));
    CHECK(atari8_pokey_write(&p,9,0));
    CHECK(atari8_pokey_write(&p,15,0x96));
    atari8_pokey_step(&p,28);
    CHECK(p.output[0]==0 && p.output[2]==1 && p.output[3]==1);
    CHECK(p.irq_pending==1 && p.counter[2]==1 && p.counter[3]==1);
    CHECK(atari8_pokey_write(&p,15,3));
    atari8_pokey_step(&p,28);
    CHECK(p.output[0]==1 && p.output[2]==0 && p.output[3]==0);
    CHECK(p.irq_pending==5);
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
    for (unsigned low = 0; low < 4; low += 2) {
        test_linked_low(low, 1);
        test_linked_low(low, 28);
        test_linked_low(low, 114);
    }
    test_linked_filter();
    test_irq_latches(28);
    test_irq_latches(114);
    test_irq_fast_and_linked();
    test_filter(0);
    test_filter(1);
    test_polarity();
    test_headless_inputs();
    test_two_tone_break();
    test_async_idle();
    test_random(false);
    test_random(true);
    test_random_scanline();
    test_skctl_clocks(28);
    test_skctl_clocks(114);
    test_polynomials();
    test_short_pattern(0, "000111011001010", 15);
    test_short_pattern(1, "1000001110010001010111101101001", 31);
    test_waveforms();
    test_mixed_clocks();
    test_volume_and_rejection();
    test_channels();
    puts("PASS: experimental Atari8 POKEY");
    return 0;
}
