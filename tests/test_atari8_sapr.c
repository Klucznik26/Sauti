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
    CHECK(s.nonlinear_mix);
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

/* Toggle one left DAC against constant left bias. The right chip
 * has a distinct DAC sequence. Compare every output sample with a
 * direct per-cycle PCM oracle, independent of SAP-R frame application. */
static void test_dac_mixer(bool nonlinear)
{
    static const char header[]="SAP\r\nTYPE R\r\nSTEREO\r\nFASTPLAY 78\r\n\r\n";
    enum { count=32, stride=18 };
    uint8_t data[sizeof(header)-1+count*stride];
    const size_t h=sizeof(header)-1;
    memcpy(data,header,h);
    memset(data+h,0,count*stride);
    for (unsigned i=0;i<count;++i) {
        uint8_t *r=data+h+i*stride;
        r[1]=(uint8_t)(0x10u+((i&1u)?15u:0u));
        r[3]=r[5]=0x1f; /* left bias 30 */
        r[10]=(uint8_t)(0x10u+((i&2u)?8u:0u));
    }
    Atari8Sapr s;
    Atari8Pcm ref;
    CHECK(atari8_sapr_open(&s,data,sizeof(data),48000)==ATARI8_SAPR_OK);
    CHECK(s.nonlinear_mix);
    s.nonlinear_mix=nonlinear;
    CHECK(atari8_pcm_init(&ref,1773447u,48000u,2,true));
    unsigned frames=0;
    for (unsigned i=0;i<count;++i) {
        const uint8_t level[2]={(uint8_t)(30u+((i&1u)?15u:0u)),
                                (uint8_t)((i&2u)?8u:0u)};
        const uint16_t analog[2]={atari8_pokey_curve[level[0]],
                                  atari8_pokey_curve[level[1]]};
        for (uint32_t c=0;c<78u*114u;++c) {
            int16_t expected[2],actual[2];
            bool ready=nonlinear ? atari8_pcm_feed_u16(&ref,analog,expected)
                                 : atari8_pcm_feed(&ref,level,expected);
            if (ready) {
                CHECK(atari8_sapr_next(&s,actual)==ATARI8_SAPR_OK);
                CHECK(actual[0]==expected[0] && actual[1]==expected[1]);
                ++frames;
            }
        }
    }
    int16_t end[2];
    CHECK(frames==(uint64_t)count*78u*114u*48000u/1773447u);
    CHECK(atari8_sapr_next(&s,end)==ATARI8_SAPR_EOF);
}

static void test_time_metadata(void)
{
    static const char header[]="SAP\r\nTYPE R\r\nTIME 00:01.25 LOOP\r\n\r\n";
    uint8_t data[sizeof(header)-1+9]={0};
    memcpy(data,header,sizeof(header)-1);
    Atari8Sapr s;
    CHECK(atari8_sapr_open(&s,data,sizeof(data),48000)==ATARI8_SAPR_OK);
    CHECK(s.info.time.status==ATARI8_SAP_TIME_VALID &&
          s.info.time.duration_ms==1250u && s.info.time.loop);
    unsigned frames=0;
    int16_t frame[2];
    while (atari8_sapr_next(&s,frame)==ATARI8_SAPR_OK) ++frames;
    CHECK(frames==(uint64_t)312u*114u*48000u/1773447u);
    CHECK(s.terminal==ATARI8_SAPR_EOF);
    static const char duplicate[]="SAP\r\nTYPE R\r\nTIME 00:01\r\nTIME 00:02\r\n\r\n";
    uint8_t dup[sizeof(duplicate)-1+9]={0};
    memcpy(dup,duplicate,sizeof(duplicate)-1);
    Atari8SaprInfo info;
    CHECK(atari8_sapr_parse(dup,sizeof(dup),&info)==ATARI8_SAPR_OK);
    CHECK(info.time.status==ATARI8_SAP_TIME_INVALID);
}

static void test_text_metadata(void)
{
    static const char header[]="SAP\r\nTYPE R\r\nNAME \"Stereo song\"\r\nAUTHOR \"\"\r\nDATE \"1995\"\r\n\r\n";
    uint8_t data[sizeof(header)-1+9]={0};
    memcpy(data,header,sizeof(header)-1);
    Atari8SaprInfo info;
    CHECK(atari8_sapr_parse(data,sizeof(data),&info)==ATARI8_SAPR_OK);
    CHECK(info.metadata.name.present && !info.metadata.name.truncated &&
          !strcmp(info.metadata.name.value,"Stereo song"));
    CHECK(info.metadata.author.present && info.metadata.author.value[0]=='\0');
    CHECK(info.metadata.date.present && !strcmp(info.metadata.date.value,"1995"));
}

int main(void)
{
    test_text_metadata();
    test_time_metadata();
    CHECK(atari8_pokey_curve[0]==0 && atari8_pokey_curve[60]==65535u);
    for (unsigned i=1;i<=60;++i)
        CHECK(atari8_pokey_curve[i]>atari8_pokey_curve[i-1]);
    CHECK(atari8_pokey_curve[60]-atari8_pokey_curve[45]<
          atari8_pokey_curve[15]-atari8_pokey_curve[0]);
    test_dac_mixer(false);
    test_dac_mixer(true);
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
