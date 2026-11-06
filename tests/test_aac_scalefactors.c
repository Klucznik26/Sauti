#include "sauti_aac_scalefactors.h"
#include "sauti_aac_tables.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {uint8_t bytes[64];size_t bit;} Bits;
static void put(Bits *b,unsigned width,uint32_t code){
    assert(width<=32 && b->bit+width<=sizeof(b->bytes)*8);
    for(unsigned i=width;i;i--){
        if((code>>(i-1u))&1u)
            b->bytes[b->bit/8u]|=(uint8_t)(1u<<(7u-b->bit%8u));
        b->bit++;
    }
}
static void delta(Bits *b,unsigned symbol){
    uint32_t code;uint8_t width;
    assert(sauti_aac_standard_codeword(0,symbol,&code,&width));
    put(b,width,code);
}
static void add(SautiAacSections *s,unsigned group,unsigned cb,
                unsigned first,unsigned end){
    SautiAacSection *x=&s->sections[s->section_count++];
    x->group=(uint8_t)group;x->codebook=(uint8_t)cb;
    x->first_sfb=(uint8_t)first;x->end_sfb=(uint8_t)end;
}
int main(void){
    SautiAacSections s={0};s.group_count=2;s.max_sfb=5;
    add(&s,0,1,0,2);add(&s,0,0,2,3);add(&s,0,13,3,4);add(&s,0,14,4,5);
    add(&s,1,1,0,1);add(&s,1,13,1,3);add(&s,1,15,3,5);
    Bits b={0};put(&b,3,5); /* non-byte-aligned input */
    delta(&b,62);delta(&b,59); /* spectral 102,101 */
    put(&b,9,260); /* noise: gain-90+(260-256) = 14 */
    delta(&b,61); /* intensity 1 */
    delta(&b,60); /* spectral 101 */
    delta(&b,63);delta(&b,58); /* noise 17, 15 */
    delta(&b,59);delta(&b,62); /* intensity 0, 2 */
    SautiAacScaleFactors out={0};size_t pos=3;
    assert(sauti_aac_lc_scalefactors_parse(b.bytes,sizeof(b.bytes),
                                          &pos,100,&s,&out));
    assert(pos==b.bit);
    assert(out.value[0][0]==102 && out.value[0][1]==101);
    assert(out.value[0][2]==0 && out.codebook[0][2]==0);
    assert(out.value[0][3]==14 && out.value[0][4]==1);
    assert(out.value[1][0]==101 && out.value[1][1]==17);
    assert(out.value[1][2]==15 && out.value[1][3]==0);
    assert(out.value[1][4]==2);
    /* Force truncation immediately before the 9-bit first noise field
     * while keeping enough complete bytes for both preceding symbols. */
    pos=3;
    SautiAacScaleFactors saved=out;
    assert(!sauti_aac_lc_scalefactors_parse(b.bytes,2,&pos,100,&s,&out));
    assert(pos==3 && memcmp(&out,&saved,sizeof(out))==0);
    SautiAacSections bad=s;bad.sections[1].first_sfb=1;
    assert(!sauti_aac_lc_scalefactors_parse(b.bytes,sizeof(b.bytes),
                                           &pos,100,&bad,&out));
    assert(pos==3 && memcmp(&out,&saved,sizeof(out))==0);
    bad=s;bad.sections[0].codebook=12;
    assert(!sauti_aac_lc_scalefactors_parse(b.bytes,sizeof(b.bytes),
                                           &pos,100,&bad,&out));
    /* Missing last section must not be accepted. */
    bad=s;bad.section_count--;
    assert(!sauti_aac_lc_scalefactors_parse(b.bytes,sizeof(b.bytes),
                                           &pos,100,&bad,&out));
    puts("PASS AAC-LC scalefactors: spectral/noise/intensity, groups, rollback");
    return 0;
}
