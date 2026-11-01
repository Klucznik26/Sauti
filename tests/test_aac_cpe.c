#include "sauti_aac_cpe.h"
#include "sauti_aac_tables.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct{uint8_t b[128];size_t bits;} Bits;
static void put(Bits *b,unsigned width,unsigned value){
    assert(b->bits+width<=sizeof(b->b)*8u);
    while(width--){
        b->b[b->bits/8]|=(uint8_t)(((value>>width)&1u)<<(7-b->bits%8));
        b->bits++;
    }
}
static void symbol(Bits *b,unsigned book,unsigned index){
    uint32_t value=0;uint8_t width=0;
    assert(sauti_aac_standard_codeword(book,index,&value,&width));
    put(b,width,value);
}
static void ics(Bits *b,unsigned max_sfb){
    put(b,1,0);put(b,2,0);put(b,1,0); /* reserved, long, sin */
    put(b,6,max_sfb);put(b,1,0);
}
static void channel(Bits *b,unsigned common){
    put(b,8,100); /* global gain */
    if(!common)ics(b,0);
    put(b,1,0); /* pulse absent */
    put(b,1,0); /* TNS absent */
    put(b,1,0); /* gain absent */
}
static void sample(Bits *b,unsigned common,unsigned mask){
    memset(b,0,sizeof(*b));
    put(b,3,1);put(b,4,0);put(b,1,common); /* CPE */
    if(common){
        ics(b,0);
        put(b,2,mask);
    }
    channel(b,common);channel(b,common);
    put(b,3,7); /* END */
    while(b->bits%8)put(b,1,0);
}
static void header(uint8_t *dst,unsigned len){
    assert(len>=9 && len<8192);
    memset(dst,0,len);
    dst[0]=0xff;dst[1]=0xf1;
    dst[2]=0x50; /* AAC-LC 44.1kHz */
    dst[3]=(uint8_t)(0x80u|((len>>11)&3u)); /* stereo channel_config=2 */
    dst[4]=(uint8_t)(len>>3);
    dst[5]=(uint8_t)(((len&7u)<<5)|0x1fu);
    dst[6]=0xfcu;
}
static void run(unsigned common,unsigned mask){
    Bits raw;sample(&raw,common,mask);
    size_t pos=0;
    SautiAacCpeData parsed={0};
    SautiAacReason reason=SAUTI_AAC_REASON_INVALID_SCE;
    assert(sauti_aac_lc_cpe_parse(raw.b,raw.bits/8,&pos,4,
                                  &parsed,&reason)==1);
    assert(pos+3<=raw.bits && parsed.common_window==common);
    assert(parsed.ms_mask_present==(common?mask:0u));
    assert(parsed.channel[0].ics.max_sfb==0);
    uint8_t frame[128];
    size_t length=7+raw.bits/8;
    header(frame,(unsigned)length);
    memcpy(frame+7,raw.b,raw.bits/8);
    SautiAacLcSynthesis state[2]={0};
    assert(sauti_aac_lc_synthesis_init(&state[0],0));
    assert(sauti_aac_lc_synthesis_init(&state[1],0));
    float pcm[2048];for(unsigned i=0;i<2048;i++)pcm[i]=42;
    assert(sauti_aac_lc_adts_stereo_decode_ex(frame,length,
                                              state,pcm,&reason)==1);
    assert(reason==SAUTI_AAC_REASON_OK);
    for(unsigned i=0;i<2048;i++)assert(pcm[i]==0);
    SautiAacLcSynthesis saved[2]={state[0],state[1]};
    for(unsigned i=0;i<2048;i++)pcm[i]=42;
    assert(sauti_aac_lc_adts_stereo_decode_ex(frame,length-1,
                                              state,pcm,&reason)==0);
    assert(memcmp(saved,state,sizeof(saved))==0);
    for(unsigned i=0;i<2048;i++)assert(pcm[i]==42);
}
static void unsupported(void){
    Bits b;sample(&b,1,3); /* reserved mask */
    size_t pos=0;
    SautiAacCpeData out={0},old=out;
    SautiAacReason reason=SAUTI_AAC_REASON_OK;
    assert(sauti_aac_lc_cpe_parse(b.b,b.bits/8,&pos,4,&out,&reason)==0);
    assert(pos==0 && memcmp(&out,&old,sizeof(out))==0);
}
static void nonzero_ms(unsigned mask){
    Bits raw={0};
    put(&raw,3,1);put(&raw,4,0);put(&raw,1,1); /* CPE common */
    ics(&raw,1); /* SFB0 has four spectral coefficients */
    put(&raw,2,mask);
    if(mask==1u)put(&raw,1,1); /* MS only in SFB0 */
    for(unsigned ch=0;ch<2;ch++){
        put(&raw,8,100); /* global gain */
        put(&raw,4,1);put(&raw,5,1); /* section: codebook 1 */
        symbol(&raw,0,60); /* scalefactor delta=0 */
        put(&raw,1,0);put(&raw,1,0);put(&raw,1,0);
        symbol(&raw,1,ch==0?80u:40u); /* M=+1 quad, S=zero */
    }
    put(&raw,3,7);
    while(raw.bits%8u)put(&raw,1,0);
    uint8_t frame[128];unsigned length=7u+(unsigned)(raw.bits/8u);
    header(frame,length);memcpy(frame+7,raw.b,raw.bits/8u);
    SautiAacLcSynthesis state[2]={0};
    assert(sauti_aac_lc_synthesis_init(&state[0],0));
    assert(sauti_aac_lc_synthesis_init(&state[1],0));
    float pcm[2048];SautiAacReason reason=SAUTI_AAC_REASON_OK;
    assert(sauti_aac_lc_adts_stereo_decode_ex(frame,length,
                                              state,pcm,&reason)==1);
    assert(reason==SAUTI_AAC_REASON_OK);
    double energy=0.0;
    for(unsigned i=0;i<1024;i++){
        float l=pcm[2*i],r=pcm[2*i+1];
        assert(isfinite(l)&&isfinite(r));
        if(mask!=0)assert(fabsf(l-r)<1e-6f);
        else assert(r==0.0f);
        energy+=(double)l*l;
    }
    assert(energy>0.0);
    size_t pos=0;
    SautiAacCpeData parsed={0};
    assert(sauti_aac_lc_cpe_parse(raw.b,raw.bits/8,&pos,4,
                                  &parsed,&reason)==1);
    assert(parsed.ms_mask_present==mask);
    assert(parsed.ms_used[0][0]==(mask!=0));
}
int main(void){
    run(0,0);run(1,0);run(1,1);run(1,2);
    nonzero_ms(0);nonzero_ms(1);nonzero_ms(2);unsupported();
    puts("PASS AAC-26/27 CPE: independent/common, MS spectral recovery, atomic channels");
    return 0;
}
