#include "sauti_aac_sbr_master.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Golden boundaries independently checked against geometric rounding,
 * SBR ascending-width ordering and both two-region warping modes. */
static void vector(unsigned k0, unsigned k2, unsigned scale,
                   unsigned alter, unsigned xover,
                   const uint8_t *expected, unsigned n) {
    SautiAacSbrMaster output;
    memset(&output, 0xa5, sizeof(output));
    assert(sauti_aac_sbr_master_log(k0,k2,scale,alter,xover,&output));
    assert(output.k0==k0 && output.k2==k2 && output.count==n &&
           output.crossover_band==xover && output.kx==expected[xover]);
    assert(memcmp(output.edges,expected,n+1u)==0);
    for(unsigned i=1;i<=n;i++)assert(output.edges[i]>output.edges[i-1u]);
}
static void test_golden(void) {
    static const uint8_t one_region[]={16,17,19,21,23,25,27,29,32};
    static const uint8_t two_region[]={12,13,14,15,16,18,20,22,24,26,28,31,34,37,40,44,48};
    static const uint8_t two_warped[]={12,13,14,15,16,18,20,22,24,27,30,34,38,43,48};
    static const uint8_t scale2[]={16,17,18,19,20,22,24,26,28,30,32,34,36,39,42,45,48};
    static const uint8_t scale1[]={14,15,16,17,18,19,20,21,22,23,24,26,28,30,32,34,36,39,42,45,48};
    vector(16,32,3,0,3,one_region,8);
    vector(12,48,3,0,7,two_region,16);
    vector(12,48,3,1,5,two_warped,14);
    vector(16,48,2,0,6,scale2,16);
    vector(14,48,1,1,1,scale1,20);
}
static void test_errors(void) {
    const unsigned bad[][5] = {
        {0,32,1,0,0},{32,32,1,0,0},{32,16,1,0,0},
        {16,65,1,0,0},{1,64,1,0,0},{16,32,0,0,0},
        {16,32,4,0,0},{16,32,3,2,0},{16,32,3,0,8},
        {16,32,3,0,48},{16,17,1,0,0}
    };
    SautiAacSbrMaster output, saved;
    memset(&output,0x96,sizeof(output));saved=output;
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){
        assert(!sauti_aac_sbr_master_log(bad[i][0],bad[i][1],bad[i][2],
                                        bad[i][3],bad[i][4],&output));
        assert(memcmp(&output,&saved,sizeof(output))==0);
    }
    assert(!sauti_aac_sbr_master_log(16,32,2,0,1,NULL));
}
static void test_boundaries(void) {
    unsigned valid=0,invalid=0;
    SautiAacSbrMaster out, original;
    for(unsigned k0=1;k0<=32;k0++)
      for(unsigned k2=k0+1;k2<=64;k2++)
        for(unsigned scale=1;scale<=3;scale++)
          for(unsigned alter=0;alter<=1;alter++)
            for(unsigned xover=0;xover<=7;xover++) {
                memset(&out,0xa5,sizeof(out));original=out;
                int ok=sauti_aac_sbr_master_log(k0,k2,scale,alter,xover,&out);
                if(!ok) {
                    assert(memcmp(&original,&out,sizeof(out))==0);
                    invalid++;continue;
                }
                valid++;
                assert(out.k0==k0 && out.k2==k2 &&
                       out.count>=2 && out.count<=48 &&
                       (out.count&1u)==0 && xover<out.count);
                assert(out.kx==out.edges[xover] &&
                       out.edges[0]==k0 && out.edges[out.count]==k2);
                for(unsigned n=1;n<=out.count;n++)
                    assert(out.edges[n]>out.edges[n-1u]);
            }
    assert(valid>1000 && invalid>1000);
    printf("SBR log grid sweep: %u valid, %u rejected safely\n",valid,invalid);
}
int main(void){
    test_golden();test_errors();test_boundaries();
    puts("PASS HE-AAC SBR logarithmic master bands: all scales, warp, fail-closed");
    return 0;
}
