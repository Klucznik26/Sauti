#include "sauti_aac_lc_frame.h"
#include "sauti_aac_tables.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {uint8_t bytes[128];size_t bits;} Bits;
static void put(Bits *b,unsigned n,uint32_t value){
    assert(n<=32 && b->bits+n<=sizeof(b->bytes)*8u);
    for(unsigned j=n;j>0;j--){
        b->bytes[b->bits/8u]|=(uint8_t)(((value>>(j-1u))&1u)
                                              <<(7u-b->bits%8u));
        b->bits++;
    }
}
static void codeword(Bits *b,unsigned cb,unsigned symbol){
    uint32_t value;uint8_t n;
    assert(sauti_aac_standard_codeword(cb,symbol,&value,&n));
    put(b,n,value);
}
static void init_header(uint8_t *dst,unsigned bytes,unsigned rate){
    assert(bytes>=9 && bytes<8192 && rate<13);
    memset(dst,0,bytes);
    dst[0]=0xff;dst[1]=0xf1;
    dst[2]=(uint8_t)(0x40u|(rate<<2));
    dst[3]=(uint8_t)(0x40u | ((bytes>>11)&3u));
    dst[4]=(uint8_t)(bytes>>3);
    dst[5]=(uint8_t)(((bytes&7u)<<5)|0x1fu);
    dst[6]=0xfcu;
}
static void sce_prefix(Bits *b,unsigned max_sfb){
    put(b,3,0); /* SCE */
    put(b,4,0); /* element_instance_tag */
    put(b,8,100); /* global_gain */
    put(b,1,0);put(b,2,0);put(b,1,0); /* ONLY_LONG/SIN */
    put(b,6,max_sfb);put(b,1,0); /* predictor absent */
}
static size_t prepare(Bits *b,int nonzero,int tool){
    memset(b,0,sizeof(*b));
    sce_prefix(b,nonzero?1u:0u);
    if(nonzero){
        put(b,4,1);put(b,5,1); /* one SFB codebook 1 */
        codeword(b,0,60);      /* zero scalefactor delta */
    }
    put(b,1,tool==1); /* pulse_data_present */
    if(tool==1)return b->bits;
    put(b,1,tool==2); /* tns_data_present */
    if(tool==2)return b->bits;
    put(b,1,tool==3); /* gain_control_data_present */
    if(tool==3)return b->bits;
    if(nonzero)codeword(b,1,40); /* one zero-valued quad */
    put(b,3,7); /* END */
    while(b->bits%8u)put(b,1,0);
    return b->bits;
}
static void simple(void){
    SautiAacLcSynthesis state={0};
    assert(sauti_aac_lc_synthesis_init(&state,0));
    float pcm[1024];for(unsigned i=0;i<1024;i++)pcm[i]=33;
    for(unsigned nonzero=0;nonzero<=1;nonzero++){
        Bits raw;prepare(&raw,(int)nonzero,0);
        size_t size=7u+raw.bits/8u;
        uint8_t frame[128];init_header(frame,(unsigned)size,4);
        memcpy(frame+7,raw.bytes,raw.bits/8u);
        SautiAacLcMonoData parsed={0};
        size_t offset=0;
        assert(sauti_aac_lc_mono_parse(raw.bytes,raw.bits/8u,
                                     &offset,4,&parsed)==1);
        assert(parsed.ics.max_sfb==nonzero);
        assert(parsed.quant[0]==0 && parsed.quant[3]==0);
        assert(sauti_aac_lc_adts_mono_decode(frame,size,&state,pcm)==1);
        for(unsigned i=0;i<1024;i++)assert(pcm[i]==0);
    }
}
static void prefixed_fil(void){
    Bits payload;
    prepare(&payload,0,0); /* SCE + ID_END */
    SautiAacLcSynthesis state={0};
    assert(sauti_aac_lc_synthesis_init(&state,0));
    float pcm[1024];
    for(unsigned ext=0;ext<=13;ext+=13){
        Bits raw={0};
        put(&raw,3,6); /* ID_FIL */
        put(&raw,4,1); /* one byte extension payload */
        put(&raw,4,ext); /* EXT_FILL=0 allowed; EXT_SBR_DATA=13 denied */
        put(&raw,4,0);
        for(size_t bit=0;bit<payload.bits;bit++)
            put(&raw,1,(payload.bytes[bit/8]>>(7-bit%8))&1u);
        while(raw.bits%8u)put(&raw,1,0);
        uint8_t frame[128];
        unsigned size=7u+(unsigned)(raw.bits/8u);
        init_header(frame,size,4);
        memcpy(frame+7,raw.bytes,raw.bits/8u);
        SautiAacReason reason=SAUTI_AAC_REASON_OK;
        SautiAacLcSynthesis old=state;
        int status=sauti_aac_lc_adts_mono_decode_ex(frame,size,
                                                    &state,pcm,&reason);
        if(ext==0){
            assert(status==1 && reason==SAUTI_AAC_REASON_OK);
            for(unsigned i=0;i<1024;i++)assert(pcm[i]==0);
        }else{
            assert(status==-1 && reason==SAUTI_AAC_REASON_UNSUPPORTED_FIL);
            assert(memcmp(&state,&old,sizeof(state))==0);
        }
    }
}
static void tns_frame(void){
    /* Spectral data uses zero-valued Huffman quad. A present TNS
     * filter with order=0 must be accepted without changing PCM. */
    Bits raw={0};
    sce_prefix(&raw,1u);
    put(&raw,4,1);put(&raw,5,1); /* codebook 1, one SFB */
    codeword(&raw,0,60);
    put(&raw,1,0); /* pulse absent */
    put(&raw,1,1); /* TNS present */
    put(&raw,2,1); /* n_filt=1 for long window */
    put(&raw,1,1); /* coef_res=4 */
    put(&raw,6,1); /* filter length=1 SFB */
    put(&raw,5,0); /* filter order zero */
    put(&raw,1,0); /* gain control absent */
    codeword(&raw,1,40); /* zero spectral quad */
    put(&raw,3,7); /* END */
    while(raw.bits%8u)put(&raw,1,0);
    uint8_t frame[128];
    size_t length=7u+raw.bits/8u;
    init_header(frame,(unsigned)length,4);
    memcpy(frame+7,raw.bytes,raw.bits/8u);
    SautiAacLcMonoData parsed={0};
    size_t pos=0;
    assert(sauti_aac_lc_mono_parse(raw.bytes,raw.bits/8u,
                                   &pos,4,&parsed)==1);
    assert(parsed.tns_present==1);
    assert(parsed.tns.filter_count[0]==1);
    assert(parsed.tns.filter[0][0].order==0);
    SautiAacLcSynthesis state={0};
    assert(sauti_aac_lc_synthesis_init(&state,0));
    float pcm[1024];
    SautiAacReason reason=SAUTI_AAC_REASON_OK;
    assert(sauti_aac_lc_adts_mono_decode_ex(frame,length,
                                            &state,pcm,&reason)==1);
    assert(reason==SAUTI_AAC_REASON_OK);
    for(unsigned i=0;i<1024;i++)assert(pcm[i]==0);

    /* Truncated TNS payload must not mutate output or history. */
    Bits broken={0};
    sce_prefix(&broken,0);
    put(&broken,1,0); /* pulse */
    put(&broken,1,1); /* TNS */
    put(&broken,2,1); /* n_filt, but no valid remaining payload */
    SautiAacLcMonoData saved=parsed;
    pos=0;
    assert(!sauti_aac_lc_mono_parse(broken.bytes,
                                    (broken.bits+7u)/8u,&pos,4,&parsed));
    assert(pos==0 && memcmp(&saved,&parsed,sizeof(parsed))==0);
}
static void failure(void){
    Bits raw;prepare(&raw,1,0);
    size_t size=7u+raw.bits/8u;
    uint8_t frame[128];init_header(frame,(unsigned)size,4);
    memcpy(frame+7,raw.bytes,raw.bits/8u);
    SautiAacLcSynthesis state={0};
    assert(sauti_aac_lc_synthesis_init(&state,0));
    float pcm[1024];for(unsigned j=0;j<1024;j++)pcm[j]=18;
    SautiAacLcSynthesis old=state;
    assert(sauti_aac_lc_adts_mono_decode(frame,size-1,&state,pcm)==0);
    assert(memcmp(&old,&state,sizeof(old))==0);
    for(unsigned j=0;j<1024;j++)assert(pcm[j]==18);
    for(unsigned tool=1;tool<=3;tool++){
        if(tool==2)continue; /* AAC-25: valid TNS is supported */
        prepare(&raw,1,(int)tool);
        size=7u+(raw.bits+7u)/8u;
        init_header(frame,(unsigned)size,4);
        memcpy(frame+7,raw.bytes,(raw.bits+7u)/8u);
        SautiAacReason reason=SAUTI_AAC_REASON_OK;
        assert(sauti_aac_lc_adts_mono_decode_ex(frame,size,&state,pcm,
                                                &reason)==-1);
        assert(reason==(tool==1?SAUTI_AAC_REASON_PULSE:
                                SAUTI_AAC_REASON_GAIN_CONTROL));
        assert(memcmp(&old,&state,sizeof(old))==0);
    }
    /* Stereo frames are explicitly unsupported. */
    prepare(&raw,0,0);
    size=7u+raw.bits/8u;
    init_header(frame,(unsigned)size,4);
    memcpy(frame+7,raw.bytes,raw.bits/8u);
    frame[3]=(uint8_t)((frame[3]&0x3fu)|0x80u); /* mono -> stereo */
    SautiAacReason reason=SAUTI_AAC_REASON_OK;
    assert(sauti_aac_lc_adts_mono_decode_ex(frame,size,&state,pcm,
                                            &reason)==-1);
    assert(reason==SAUTI_AAC_REASON_UNSUPPORTED_ADTS);
    assert(strcmp(sauti_aac_reason_name(reason),"unsupported_adts")==0);
    assert(memcmp(&old,&state,sizeof(old))==0);
    /* A structurally valid frame cannot resume audio when a previous
     * frame was skipped and its overlap history is unknown. */
    prepare(&raw,0,0);
    size=7u+raw.bits/8u;
    init_header(frame,(unsigned)size,4);
    memcpy(frame+7,raw.bytes,raw.bits/8u);
    state.initialized=0u;
    SautiAacLcSynthesis missing=state;
    reason=SAUTI_AAC_REASON_OK;
    assert(sauti_aac_lc_adts_mono_decode_ex(frame,size,&state,pcm,
                                            &reason)==-1);
    assert(reason==SAUTI_AAC_REASON_UNSUPPORTED_HISTORY);
    assert(memcmp(&missing,&state,sizeof(missing))==0);
    for(unsigned j=0;j<1024;j++)assert(pcm[j]==18);
}
int main(void){
    simple();prefixed_fil();tns_frame();failure();
    puts("PASS AAC-13/17: SCE -> PCM, explicit tool rejection reasons, rollback");
    return 0;
}
