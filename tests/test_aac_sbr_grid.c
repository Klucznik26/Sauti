#include "sauti_aac_sbr_grid.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t bytes[64]; size_t bit; } Bits;
static void put(Bits *b, unsigned width, unsigned val) {
    assert(width <= 32u && b->bit + width <= sizeof(b->bytes) * 8u);
    for (unsigned i = width; i > 0; --i) {
        b->bytes[b->bit / 8u] |=
            (uint8_t)(((val >> (i - 1u)) & 1u) << (7u - b->bit % 8u));
        ++b->bit;
    }
}
static size_t used(const Bits *b) { return (b->bit + 7u) / 8u; }
static void test_fixed(void) {
    Bits b = {0};
    put(&b, 3, 5); /* unaligned prefix */
    size_t pos = b.bit;
    put(&b, 2, 0); put(&b, 2, 2); put(&b, 1, 1); /* FIXFIX, four envelopes */
    SautiAacSbrGrid grid = {0};
    assert(sauti_aac_sbr_grid_parse(b.bytes, used(&b), &pos, 16, &grid));
    assert(pos == b.bit && grid.env_count == 4 && grid.noise_count == 2);
    assert(grid.freq_res[0] == 1 && grid.freq_res[3] == 1);
    assert(grid.env_border[0] == 0 && grid.env_border[1] == 4 &&
           grid.env_border[2] == 8 && grid.env_border[3] == 12 &&
           grid.env_border[4] == 16 && grid.noise_border[1] == 8);
    b = (Bits){0}; put(&b, 2, 0); put(&b, 2, 0); put(&b, 1, 0);
    pos = 0;
    assert(sauti_aac_sbr_grid_parse(b.bytes, used(&b), &pos, 15, &grid));
    assert(grid.env_count == 1 && grid.noise_count == 1 &&
           grid.noise_border[0] == 0 && grid.noise_border[1] == 15);
}
static void test_variable(void) {
    SautiAacSbrGrid g = {0}; size_t pos = 0;
    Bits b = {0};
    /* FIXVAR: 0, 12, 16, 18. Reverse transmitted frequency flags. */
    put(&b,2,1); put(&b,2,2); put(&b,2,2);
    put(&b,2,0); put(&b,2,1); put(&b,2,2);
    put(&b,1,1); put(&b,1,1); put(&b,1,0);
    assert(sauti_aac_sbr_grid_parse(b.bytes,used(&b),&pos,16,&g));
    assert(pos==b.bit && g.frame_class==1 && g.env_count==3 &&
           g.pointer==2 && g.noise_border[1]==16);
    assert(g.env_border[0]==0 && g.env_border[1]==12 &&
           g.env_border[2]==16 && g.env_border[3]==18);
    assert(g.freq_res[0]==0 && g.freq_res[1]==1 && g.freq_res[2]==1);

    /* VARFIX: 1, 3, 7, 16. Forward flags. */
    b=(Bits){0}; pos=0;
    put(&b,2,2); put(&b,2,1); put(&b,2,2);
    put(&b,2,0); put(&b,2,1); put(&b,2,2);
    put(&b,1,1); put(&b,1,0); put(&b,1,1);
    assert(sauti_aac_sbr_grid_parse(b.bytes,used(&b),&pos,16,&g));
    assert(pos==b.bit && g.frame_class==2 && g.pointer==2 &&
           g.env_border[0]==1 && g.env_border[1]==3 &&
           g.env_border[2]==7 && g.env_border[3]==16 &&
           g.noise_border[1]==3 && g.freq_res[1]==0);

    /* VARVAR: 1, 5, 14, 18. */
    b=(Bits){0}; pos=0;
    put(&b,2,3); put(&b,2,1); put(&b,2,2);
    put(&b,2,1); put(&b,2,1);
    put(&b,2,1); put(&b,2,1); put(&b,2,1);
    put(&b,1,1); put(&b,1,0); put(&b,1,1);
    assert(sauti_aac_sbr_grid_parse(b.bytes,used(&b),&pos,16,&g));
    assert(pos==b.bit && g.frame_class==3 && g.env_count==3 &&
           g.pointer==1 && g.env_border[0]==1 &&
           g.env_border[1]==5 && g.env_border[2]==14 &&
           g.env_border[3]==18 && g.noise_border[1]==14);
}
static void test_failures(void) {
    Bits b = {0}; size_t pos = 0;
    SautiAacSbrGrid g, before;
    memset(&g,0xa5,sizeof(g)); before=g;
    /* Eight FIXFIX envelopes are not allowed for classic SBR. */
    put(&b,2,0);put(&b,2,3);put(&b,1,1);
    assert(!sauti_aac_sbr_grid_parse(b.bytes,used(&b),&pos,16,&g));
    assert(pos==0 && memcmp(&g,&before,sizeof(g))==0);
    /* Bad pointer (7) for four-envelope VARFIX. */
    b=(Bits){0};
    put(&b,2,2);put(&b,2,0);put(&b,2,3);
    for(unsigned i=0;i<3;i++)put(&b,2,0);
    put(&b,3,7);
    assert(!sauti_aac_sbr_grid_parse(b.bytes,used(&b),&pos,16,&g));
    assert(pos==0 && memcmp(&g,&before,sizeof(g))==0);
    /* Relative borders would collide with the fixed leading border. */
    b=(Bits){0};
    put(&b,2,1);put(&b,2,0);put(&b,2,2);
    put(&b,2,3);put(&b,2,3);put(&b,2,0);
    for(unsigned i=0;i<3;i++)put(&b,1,0);
    assert(!sauti_aac_sbr_grid_parse(b.bytes,used(&b),&pos,16,&g));
    assert(pos==0 && memcmp(&g,&before,sizeof(g))==0);
    /* Truncated unaligned input must not touch the caller's state. */
    b=(Bits){0};put(&b,6,0);pos=b.bit;
    put(&b,2,0);put(&b,2,2);put(&b,1,1);
    assert(!sauti_aac_sbr_grid_parse(b.bytes,1,&pos,16,&g));
    assert(pos==6 && memcmp(&g,&before,sizeof(g))==0);
    assert(!sauti_aac_sbr_grid_parse(b.bytes,used(&b),&pos,14,&g));
    assert(pos==6 && memcmp(&g,&before,sizeof(g))==0);
    assert(!sauti_aac_sbr_grid_parse(NULL,used(&b),&pos,16,&g));
    assert(!sauti_aac_sbr_grid_parse(b.bytes,used(&b),NULL,16,&g));
    assert(!sauti_aac_sbr_grid_parse(b.bytes,used(&b),&pos,16,NULL));
}
int main(void) {
    test_fixed(); test_variable(); test_failures();
    puts("PASS HE-AAC SBR grids: FIXFIX/FIXVAR/VARFIX/VARVAR, noise borders, fail-closed");
    return 0;
}
