/* Coverage-guided parser audit (Clang libFuzzer + ASan/UBSan).
 * Deliberately does not execute guest code: arbitrary FASTPLAY and INIT
 * mutations could make individual fuzz iterations excessively expensive.
 * Runtime mutation tests are in fuzz_atari8_sap.c. */
#include "../src/decoders/retro/atari8/atari8_sapb.h"
#include "../src/decoders/retro/atari8/atari8_sapr.h"
#include <stdlib.h>
#include <string.h>

#define REQUIRE(x) do { if (!(x)) abort(); } while (0)

static void check_text(const Atari8SapText *t)
{
    REQUIRE(memchr(t->value,0,sizeof(t->value))!=NULL);
    if (!t->present) {
        REQUIRE(!t->truncated && !t->value[0]);
    }
    if (t->truncated) {
        REQUIRE(t->present && strlen(t->value)==sizeof(t->value)-1);
    }
}

static void check_metadata(const Atari8SapMetadata *m)
{
    check_text(&m->name);
    check_text(&m->author);
    check_text(&m->date);
}

static void check_time(const Atari8SapTime *t)
{
    REQUIRE(t->status==ATARI8_SAP_TIME_ABSENT ||
            t->status==ATARI8_SAP_TIME_INVALID ||
            t->status==ATARI8_SAP_TIME_VALID);
    if (t->status==ATARI8_SAP_TIME_VALID) {
        REQUIRE(t->duration_ms<=5999999u);
    } else {
        REQUIRE(!t->duration_ms && !t->loop);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size)
{
    Atari8SapbInfo b;
    Atari8SaprInfo r;
    Atari8SapbResult rb=atari8_sapb_parse(data,size,&b);
    Atari8SaprResult rr=atari8_sapr_parse(data,size,&r);
    REQUIRE(rb!=ATARI8_SAPB_OK || rr!=ATARI8_SAPR_OK);
    if (rb==ATARI8_SAPB_OK) {
        REQUIRE(b.type=='B' || b.type=='C' || b.type=='D' || b.type=='S');
        REQUIRE(b.songs>=1 && b.songs<=256 && b.default_song<b.songs);
        REQUIRE(b.interval>=114u && b.interval<=32767u*114u && b.interval%114u==0);
        REQUIRE(b.binary_offset>=5 && b.binary_offset<size &&
                size-b.binary_offset>=2);
        REQUIRE(data[b.binary_offset]==255 && data[b.binary_offset+1]==255);
        check_metadata(&b.metadata);
        for (unsigned i=0;i<256u;++i) {
            check_time(&b.time[i]);
            if(i>=b.songs) REQUIRE(b.time[i].status==ATARI8_SAP_TIME_ABSENT);
        }
    }
    if (rr==ATARI8_SAPR_OK) {
        size_t stride=r.stereo ? 18u : 9u;
        REQUIRE(r.data>=data && r.data<data+size);
        size_t remaining=(size_t)(data+size-r.data);
        REQUIRE(r.frame_count && remaining%stride==0 &&
                remaining/stride==r.frame_count);
        REQUIRE(r.cycles_per_frame>=114u && r.cycles_per_frame<=32767u*114u &&
                r.cycles_per_frame%114u==0);
        check_metadata(&r.metadata);
        check_time(&r.time);
    }
    return 0;
}
