#include "sauti_aac_sbr_patch.h"
#include "sauti_aac_sbr_limits.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void build_fixture(unsigned k0,unsigned k2,unsigned alter,
                          unsigned xover,
                          SautiAacSbrMaster *m,SautiAacSbrTables *t) {
    assert(sauti_aac_sbr_master_linear(k0,k2,alter,xover,m));
    assert(sauti_aac_sbr_tables_build(m,1,t));
}
static void verify_map(const SautiAacSbrMaster *m,
                       const SautiAacSbrPatches *p) {
    assert(p->count>=1u && p->count<=SAUTI_AAC_SBR_MAX_PATCHES);
    unsigned previous=m->kx;
    for(unsigned i=0;i<p->count;i++){
        const unsigned start=p->start[i],target=p->target[i],
                       width=p->width[i];
        assert(width>0 && start+width<=m->k0 &&
               target==previous && target+width<=m->k2);
        for(unsigned j=0;j<width;j++)
            assert(p->source_for_target[target+j]==(int)(start+j));
        previous=target+width;
    }
    for(unsigned i=0;i<64;i++){
        const int value=p->source_for_target[i];
        if(i<m->kx || i>=m->k2 || i>=previous)assert(value==-1);
        else assert(value>=0 && value<(int)m->k0);
    }
    assert(previous>=m->kx+1u && previous<=m->k2);
    assert(m->k2-previous<=2u); /* only final 1-2 band patch may be omitted */
}

static void test_golden(void) {
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    SautiAacSbrPatches p;
    build_fixture(16,32,1,0,&m,&t);
    assert(sauti_aac_sbr_patch_build(48000,&m,&t,&p));
    assert(p.count==1 && p.start[0]==2 && p.target[0]==16 &&
           p.width[0]==14);
    verify_map(&m,&p);
    assert(p.source_for_target[16]==2 &&
           p.source_for_target[29]==15 &&
           p.source_for_target[30]==-1 && p.source_for_target[31]==-1);

    assert(sauti_aac_sbr_patch_build(96000,&m,&t,&p));
    assert(p.count==2);
    assert(p.start[0]==10 && p.target[0]==16 && p.width[0]==6);
    assert(p.start[1]==6 && p.target[1]==22 && p.width[1]==10);
    verify_map(&m,&p);

    build_fixture(16,48,1,0,&m,&t);
    assert(sauti_aac_sbr_patch_build(48000,&m,&t,&p));
    assert(p.count==3);
    assert(p.start[0]==2 && p.target[0]==16 && p.width[0]==14);
    assert(p.start[1]==2 && p.target[1]==30 && p.width[1]==14);
    assert(p.start[2]==12 && p.target[2]==44 && p.width[2]==4);
    verify_map(&m,&p);

    assert(sauti_aac_sbr_patch_build(96000,&m,&t,&p));
    assert(p.count==3);
    assert(p.start[0]==10 && p.target[0]==16 && p.width[0]==6);
    assert(p.start[1]==2 && p.target[1]==22 && p.width[1]==14);
    assert(p.start[2]==4 && p.target[2]==36 && p.width[2]==12);
    verify_map(&m,&p);
}

static void test_error_atomicity(void) {
    SautiAacSbrMaster m;
    SautiAacSbrTables t;
    SautiAacSbrPatches p,prior;
    build_fixture(16,32,1,0,&m,&t);
    memset(&p,0xa5,sizeof(p));prior=p;
#define REJECT(rate,mp,tp) do {                                      \
    assert(!sauti_aac_sbr_patch_build((rate),(mp),(tp),&p));         \
    assert(memcmp(&p,&prior,sizeof(p))==0);                          \
} while (0)
    REJECT(0,&m,&t);
    REJECT(8000,&m,&t);
    REJECT(44101,&m,&t);
    REJECT(384000,&m,&t);
    REJECT(UINT32_MAX,&m,&t);
    REJECT(48000,NULL,&t);
    REJECT(48000,&m,NULL);
    assert(!sauti_aac_sbr_patch_build(48000,&m,&t,NULL));
    m.kx++;
    REJECT(48000,&m,&t);
    m.kx--;
    t.kx++;
    REJECT(48000,&m,&t);
    t.kx--;
    t.high[1]=t.high[0];
    REJECT(48000,&m,&t);
    t.high[1]=m.edges[1];
    m.edges[1]=m.edges[0];
    REJECT(48000,&m,&t);
    m.edges[1]=(uint8_t)(m.edges[0]+2u);
    m.count=49;
    REJECT(48000,&m,&t);
    m.count=8;
    m.crossover_band=8u;
    REJECT(48000,&m,&t);
#undef REJECT
}

static void test_limits_master_tables_chain(void) {
    const unsigned rates[]={16000,22050,24000,32000,44100,48000,
                            64000,88200,96000,128000,176400,192000};
    unsigned success=0,rejected=0,omitted=0;
    for(unsigned r=0;r<sizeof(rates)/sizeof(rates[0]);r++)
      for(unsigned start=0;start<16;start+=3)
        for(unsigned stop=0;stop<16;stop+=3) {
            SautiAacSbrLimits limits;
            if(!sauti_aac_sbr_limits_compute(rates[r],start,stop,&limits))
                continue;
            for(unsigned scale=0;scale<4;scale++)
              for(unsigned alter=0;alter<2;alter++)
                for(unsigned crossover=0;crossover<8;crossover++) {
                    SautiAacSbrMaster m;
                    int ok=scale==0
                       ? sauti_aac_sbr_master_linear(limits.k0,limits.k2,
                                                    alter,crossover,&m)
                       : sauti_aac_sbr_master_log(limits.k0,limits.k2,
                                                   scale,alter,crossover,&m);
                    if(!ok)continue;
                    SautiAacSbrTables t;
                    if(!sauti_aac_sbr_tables_build(&m,2,&t))continue;
                    SautiAacSbrPatches p,prior;
                    memset(&p,0xb6,sizeof(p));prior=p;
                    if(!sauti_aac_sbr_patch_build(rates[r],&m,&t,&p)){
                        assert(memcmp(&p,&prior,sizeof(p))==0);
                        rejected++;
                        continue;
                    }
                    success++;
                    verify_map(&m,&p);
                    unsigned mapped=0;
                    for(unsigned b=0;b<64;b++)
                        if(p.source_for_target[b]>=0)mapped++;
                    if(mapped<t.m)omitted++;
                }
        }
    printf("SBR-11 patch chain: %u accepted, %u rejected safely, %u short tails omitted\n",
           success,rejected,omitted);
    assert(success>500 && rejected>10);
}
int main(void) {
    test_golden();
    test_error_atomicity();
    test_limits_master_tables_chain();
    puts("PASS HE-AAC SBR-11 QMF patches: reference vectors, bounds, chain, fail-closed");
    return 0;
}
