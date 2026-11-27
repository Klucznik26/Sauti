#include "../src/decoders/retro/atari8/atari8_sapr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void test_file(bool stereo, bool ntsc, bool nonlinear)
{
    uint8_t data[256];
    const char *header = stereo
        ? "SAP\r\nTYPE R\r\nSTEREO\r\nNTSC\r\nFASTPLAY 156\r\n\r\n"
        : "SAP\r\nNAME \"Test\"\r\nTYPE R\r\n\r\n";
    size_t h = strlen(header), stride = stereo ? 18 : 9;
    memcpy(data, header, h);
    memset(data + h, 0, stride * 3);
    for (unsigned i = 0; i < 3; ++i) {
        data[h + stride * i] = 71;
        data[h + stride * i + 1] = 0xaf;
        if (stereo) {
            data[h + stride * i + 9] = 95;
            data[h + stride * i + 10] = 0xa9;
        }
    }
    Atari8Sapr s;
    Atari8SaprInfo info;
    CHECK(atari8_sapr_parse(data, h + stride * 3, &info) == ATARI8_SAPR_OK);
    CHECK(info.data == data + h && info.frame_count == 3);
    CHECK(info.stereo == stereo && info.ntsc == ntsc);
    CHECK(info.cycles_per_frame == (stereo ? 156u : 312u) * 114u);
    CHECK(atari8_sapr_open(&s, data, h + stride * 3, 48000) == ATARI8_SAPR_OK);
    s.nonlinear_mix=nonlinear;
    uint64_t expected = (uint64_t)info.cycles_per_frame * 3u * 48000u
                      / s.core.pokey[0].clock_hz;
    unsigned count = 0, left = 0, right = 0;
    int16_t frame[2];
    Atari8SaprResult r;
    while ((r = atari8_sapr_next(&s, frame)) == ATARI8_SAPR_OK) {
        ++count;
        left += frame[0] != 0;
        right += frame[1] != 0;
    }
    CHECK(r == ATARI8_SAPR_EOF && count == expected && left > 0);
    CHECK(stereo ? right > 0 : right == 0);
    CHECK(atari8_sapr_next(&s, frame) == ATARI8_SAPR_EOF);
    CHECK(atari8_sapr_parse(data, h + stride * 3 - 1, &info) == ATARI8_SAPR_BAD_FORMAT);
    CHECK(atari8_sapr_parse(data, h, &info) == ATARI8_SAPR_BAD_FORMAT);
    for (size_t n = 0; n < h; ++n)
        CHECK(atari8_sapr_parse(data, n, &info) != ATARI8_SAPR_OK);
}

int main(void)
{
    test_file(false, false, false);
    test_file(true, true, false);
    test_file(false, false, true);
    test_file(true, true, true);
    const uint8_t bad[] = "SAP\r\nTYPE R\r\nFASTPLAY 999999999999\r\n\r\n123456789";
    const uint8_t other[] = "SAP\r\nTYPE B\r\n";
    Atari8SaprInfo i;
    CHECK(atari8_sapr_parse(bad, sizeof(bad)-1, &i) == ATARI8_SAPR_BAD_FORMAT);
    CHECK(atari8_sapr_parse(other, sizeof(other)-1, &i) == ATARI8_SAPR_UNSUPPORTED);
    puts("PASS: experimental Atari8 SAP-R");
    return 0;
}
