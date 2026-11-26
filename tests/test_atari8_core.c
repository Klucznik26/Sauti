#include "../src/decoders/retro/atari8/atari8_core.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
    exit(EXIT_FAILURE); \
} } while (0)

static void levels(const Atari8Core *c, unsigned left, unsigned right)
{
    uint8_t out[2];
    atari8_core_levels(c, out);
    CHECK(out[0] == left && out[1] == right);
}

static void test_mapping(bool stereo)
{
    Atari8Core c;
    atari8_core_reset(&c, false, stereo);
    CHECK(atari8_core_channels(&c) == (stereo ? 2u : 1u));
    levels(&c, 0, 0);
    CHECK(atari8_core_write(&c, 0xd201, 0x15));
    CHECK(atari8_core_write(&c, 0xd211, 0x19));
    levels(&c, stereo ? 5 : 9, stereo ? 9 : 0);
    for (unsigned page = 0; page < 256; page += 16) {
        unsigned chip = stereo && (page & 16) ? 1 : 0;
        CHECK(atari8_core_write(&c, (uint16_t)(0xd203 + page), 0x12));
        CHECK(c.pokey[chip].audc[1] == 0x12);
    }
    levels(&c, stereo ? 7 : 11, stereo ? 11 : 0);
    CHECK(!atari8_core_write(&c, 0xd1ff, 0x1f));
    CHECK(!atari8_core_write(&c, 0xd300, 0x1f));
    CHECK(!atari8_core_write(&c, 0xd2ff, 0x1f));
    uint8_t value = 42;
    CHECK(!atari8_core_read(&c, 0xd300, &value));
    CHECK(value == 42);
    CHECK(!atari8_core_read(&c, 0xd20e, NULL));
    atari8_core_reset(&c, true, stereo);
    levels(&c, 0, 0);
    CHECK(c.pokey[0].clock_hz == 1789773u);
    CHECK(c.pokey[1].clock_hz == 1789773u);
}

static void test_stereo_timing(void)
{
    Atari8Core c;
    atari8_core_reset(&c, false, true);
    CHECK(atari8_core_write(&c, 0xd200, 0));
    CHECK(atari8_core_write(&c, 0xd201, 0xaf));
    CHECK(atari8_core_write(&c, 0xd210, 1));
    CHECK(atari8_core_write(&c, 0xd211, 0xa7));
    CHECK(atari8_core_write(&c, 0xd209, 0));
    CHECK(atari8_core_write(&c, 0xd219, 0));
    levels(&c, 0, 0);
    atari8_core_step(&c, 27);
    levels(&c, 0, 0);
    atari8_core_step(&c, 1);
    levels(&c, 15, 0);
    atari8_core_step(&c, 28);
    levels(&c, 0, 7);
    atari8_core_step(&c, 28);
    levels(&c, 15, 7);
    atari8_core_step(&c, 28);
    levels(&c, 0, 0);
    CHECK(c.pokey[0].phase28 == c.pokey[1].phase28);
    CHECK(c.pokey[0].phase114 == c.pokey[1].phase114);
    CHECK(atari8_core_write(&c, 0xd211, 0x1f));
    levels(&c, 0, 15);
    atari8_core_step(&c, 28);
    levels(&c, 15, 15);
}

static void test_irq_routing(void)
{
    Atari8Core c;
    uint8_t value;
    atari8_core_reset(&c, false, true);
    CHECK(atari8_core_write(&c, 0xd21e, 1));
    CHECK(atari8_core_write(&c, 0xd219, 0));
    atari8_core_step(&c, 28);
    CHECK(atari8_core_read(&c, 0xd23e, &value) && value == 0xfe);
    CHECK(atari8_core_read(&c, 0xd20e, &value) && value == 0xff);
    CHECK(!atari8_core_irq(&c));
    CHECK(atari8_core_write(&c, 0xd20e, 1));
    atari8_core_step(&c, 28);
    CHECK(atari8_core_irq(&c));
    CHECK(atari8_core_write(&c, 0xd20e, 0));
    CHECK(!atari8_core_irq(&c));
    CHECK(atari8_core_read(&c, 0xd21e, &value) && value == 0xfe);
    atari8_core_reset(&c, false, false);
    CHECK(atari8_core_write(&c, 0xd21e, 1)); /* mono mirror */
    atari8_core_step(&c, 28);
    CHECK(atari8_core_irq(&c));
    CHECK(atari8_core_read(&c, 0xd2fe, &value) && value == 0xfe);
}

int main(void)
{
    test_mapping(false);
    test_mapping(true);
    test_stereo_timing();
    test_irq_routing();
    puts("PASS: experimental Atari8 mono/dual POKEY");
    return 0;
}
