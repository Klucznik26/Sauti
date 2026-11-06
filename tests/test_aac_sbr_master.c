#include "sauti_aac_sbr_master.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void check(unsigned k0, unsigned k2, unsigned alter,
                  unsigned xover, const uint8_t *edges, unsigned count) {
    SautiAacSbrMaster result;
    memset(&result, 0xcc, sizeof(result));
    assert(sauti_aac_sbr_master_linear(k0,k2,alter,xover,&result) == 1);
    assert(result.k0 == k0 && result.k2 == k2);
    assert(result.count == count && result.crossover_band == xover);
    assert(result.kx == edges[xover]);
    for (unsigned i = 0; i <= count; ++i)
        assert(result.edges[i] == edges[i]);
    for (unsigned i = 1; i <= count; ++i)
        assert(result.edges[i] > result.edges[i-1]);
}

static void test_linear(void) {
    const uint8_t even[] = {16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32};
    const uint8_t odd[]  = {17,18,19,20,21,22,24};
    const uint8_t minus_two[] = {10,11,12,14,16,18,20};
    const uint8_t minus_one[] = {20,21,23,25,27};
    const uint8_t plus_one[] = {20,22,24,26,29};
    const uint8_t narrow[] = {20,21,22};
    check(16,32,0,4,even,16);
    check(17,24,0,5,odd,6);
    check(10,20,1,2,minus_two,6);
    check(20,27,1,3,minus_one,4);
    check(20,29,1,0,plus_one,4);
    check(20,22,1,1,narrow,2);
}

static void test_fail_closed(void) {
    SautiAacSbrMaster out, saved;
    memset(&out,0xa5,sizeof(out)); saved = out;
    const struct { unsigned k0,k2,alter,xover; } bad[] = {
        {0,20,0,0}, {20,20,0,0}, {21,20,0,0}, {20,65,0,0},
        {20,21,0,0}, {20,21,1,0}, {20,22,1,2},
        {20,32,2,0}, {20,32,3,0}, {20,32,0,12},
        {64,64,0,0}, {64,65,0,0}
    };
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        const unsigned a=bad[i].k0,b=bad[i].k2,c=bad[i].alter,d=bad[i].xover;
        assert(sauti_aac_sbr_master_linear(a,b,c,d,&out) == 0);
        assert(memcmp(&out,&saved,sizeof(out)) == 0);
    }
    assert(sauti_aac_sbr_master_linear(16,32,0,0,NULL) == 0);
}

int main(void) {
    test_linear();
    test_fail_closed();
    puts("PASS HE-AAC SBR linear master bands: spacing, boundaries, crossover, fail-closed");
    return 0;
}
