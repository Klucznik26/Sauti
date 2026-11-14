#include "sauti_aac_he_adts_mono.h"
#include "sauti_aac_adts_crc.h"
#include "sauti_aac_sbr_crc.h"
#include "sauti_aac_sbr_master.h"
#include "sauti_aac_sbr_tables.h"
#include "sauti_aac_sbr_limits.h"
#include "sauti_aac_sbr_patch.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* A complete ADTS raw_data_block generated independently from the
 * parser: ID_SCE, 1024-frame zero AAC-LC spectrum, ID_FIL carrying
 * full MPEG-4 SBR entropy-coded payload, optional harmless EXT_FILL,
 * ID_END and zero byte alignment. Nothing is mocked at the parser API.
 */
typedef struct {uint8_t bytes[1024];size_t bit;} Bits;
static void put(Bits *b,unsigned n,unsigned value){
    assert(n<=32u && b->bit+n<=sizeof(b->bytes)*8u);
    for(unsigned i=n;i>0;i--){
        const unsigned x=(value>>(i-1u))&1u;
        b->bytes[b->bit/8u]|=(uint8_t)(x<<(7u-b->bit%8u));
        b->bit++;
    }
}
static void pack(Bits *out,const Bits *in){
    for(size_t i=0;i<in->bit;i++)
        put(out,1u,(in->bytes[i/8u]>>(7u-(unsigned)(i%8u)))&1u);
}
static void pad(Bits *b){while(b->bit&7u)put(b,1u,0u);}
static void find_header(SautiAacSbrHeader *h,SautiAacSbrTables *t){
    SautiAacSbrHeader z={0};
    z.amp_res=1u;z.freq_scale=0u;z.alter_scale=1u;
    z.noise_bands=2u;z.limiter_bands=2u;z.limiter_gains=2u;
    z.interpol_freq=1u;z.smoothing_mode=0u;
    z.extra_1=1u;z.extra_2=1u;
    for(unsigned st=0;st<16u;st++)
        for(unsigned sp=0;sp<16u;sp++){
            SautiAacSbrLimits l;
            SautiAacSbrMaster m;
            SautiAacSbrPatches p;
            if(!sauti_aac_sbr_limits_compute(48000u,st,sp,&l) ||
               !sauti_aac_sbr_master_linear(l.k0,l.k2,1u,0u,&m) ||
               !sauti_aac_sbr_tables_build(&m,2u,t) ||
               !sauti_aac_sbr_patch_build(48000u,&m,t,&p) ||
               t->n_high<3u || t->n_high>20u || t->n_noise>4u)
                continue;
            z.start_freq=st;z.stop_freq=sp;*h=z;return;
        }
    assert(!"No valid MPEG-4 SBR frequency setup");
}
static void sbr_bits(Bits *b,const SautiAacSbrHeader *h,
                     const SautiAacSbrTables *t,
                     unsigned header,unsigned deltas,size_t *syntax_bits){
    *b=(Bits){0};
    put(b,4u,13u); /* EXT_SBR_DATA */
    put(b,1u,header);
    if(header){
        put(b,1u,h->amp_res);put(b,4u,h->start_freq);
        put(b,4u,h->stop_freq);put(b,3u,h->xover_band);
        put(b,2u,0u);put(b,1u,h->extra_1);put(b,1u,h->extra_2);
        put(b,2u,h->freq_scale);put(b,1u,h->alter_scale);
        put(b,2u,h->noise_bands);
        put(b,2u,h->limiter_bands);put(b,2u,h->limiter_gains);
        put(b,1u,h->interpol_freq);put(b,1u,h->smoothing_mode);
    }
    put(b,1u,0u); /* bs_data_extra=0 */
    put(b,2u,0u);put(b,2u,1u);put(b,1u,1u); /* FIXFIX 2 high-res envelopes */
    for(unsigned i=0;i<4u;i++)put(b,1u,deltas);
    for(unsigned i=0;i<t->n_noise;i++)put(b,2u,0u);
    for(unsigned e=0;e<2u;e++){
        if(!deltas)put(b,6u,5u);
        for(unsigned i=deltas?0u:1u;i<t->n_high;i++)
            put(b,1u,0u); /* real SBR Huffman zero delta */
    }
    for(unsigned e=0;e<2u;e++){
        if(!deltas)put(b,5u,6u);
        for(unsigned i=deltas?0u:1u;i<t->n_noise;i++)
            put(b,1u,0u); /* real SBR Huffman zero delta */
    }
    put(b,1u,1u); /* harmonic flag present */
    for(unsigned i=0;i<t->n_high;i++)put(b,1u,i==0u);
    put(b,1u,0u); /* no extended SBR / PS */
    if(syntax_bits)*syntax_bits=b->bit;
    pad(b);
}
static unsigned payload_bit(const Bits *b,size_t at){
    return (b->bytes[at/8u]>>(7u-(unsigned)(at&7u)))&1u;
}
static void sbr_crc_bits(Bits *out,const Bits *plain,size_t syntax_bits){
    assert(syntax_bits>=5u && syntax_bits<=plain->bit);
    *out=(Bits){0};
    put(out,4u,14u);put(out,10u,0u);
    for(size_t i=4u;i<syntax_bits;i++)put(out,1u,payload_bit(plain,i));
    uint16_t crc=0u;
    assert(sauti_aac_sbr_crc10_bits(out->bytes,sizeof(out->bytes),14u,
                                    out->bit-14u,&crc));
    for(unsigned i=0u;i<10u;i++)
        out->bytes[(4u+i)/8u]|=(uint8_t)(((crc>>(9u-i))&1u)
                                          <<(7u-((4u+i)&7u)));
    pad(out);
}
static void sce_silence(Bits *b){
    put(b,3u,0u); /* ID_SCE */
    put(b,4u,0u); /* element_instance_tag */
    put(b,8u,100u); /* global_gain */
    put(b,1u,0u);put(b,2u,0u);put(b,1u,0u); /* ics long/sine */
    put(b,6u,0u);put(b,1u,0u); /* zero scalefactor bands, no prediction */
    put(b,1u,0u);put(b,1u,0u);put(b,1u,0u); /* pulse/TNS/gain absent */
}
static void fil(Bits *raw,const Bits *payload,unsigned *payload_start){
    assert((payload->bit&7u)==0u);
    const unsigned n=(unsigned)(payload->bit/8u);
    assert(n>=1u && n<=269u);
    put(raw,3u,6u); /* ID_FIL */
    if(n<15u)put(raw,4u,n);
    else {
        put(raw,4u,15u);
        put(raw,8u,n-14u);
    }
    if(payload_start)*payload_start=(unsigned)raw->bit;
    pack(raw,payload);
}
static void harmless(Bits *raw){
    Bits f={0};
    put(&f,4u,0u);put(&f,4u,0u); /* EXT_FILL */
    fil(raw,&f,NULL);
}
static size_t adts(uint8_t frame[1024],const Bits *raw,unsigned channels){
    assert((raw->bit&7u)==0u);
    const unsigned length=7u+(unsigned)(raw->bit/8u);
    assert(length<1024u);
    memset(frame,0,1024u);
    frame[0]=0xffu;frame[1]=0xf1u;
    frame[2]=(uint8_t)(0x40u|(6u<<2u)); /* AAC-LC, 24000 Hz */
    frame[3]=(uint8_t)((channels<<6u)|((length>>11u)&3u));
    frame[4]=(uint8_t)(length>>3u);
    frame[5]=(uint8_t)(((length&7u)<<5u)|0x1fu);
    frame[6]=0xfcu;
    memcpy(frame+7u,raw->bytes,raw->bit/8u);
    return length;
}
static size_t adts_crc(uint8_t frame[1024],const Bits *raw,
                       unsigned channels,size_t sce_bits){
    assert((raw->bit&7u)==0u && sce_bits>=3u && sce_bits<=raw->bit);
    const unsigned length=9u+(unsigned)(raw->bit/8u);
    assert(length<1024u);
    memset(frame,0,1024u);
    frame[0]=0xffu;frame[1]=0xf0u;
    frame[2]=(uint8_t)(0x40u|(6u<<2u));
    frame[3]=(uint8_t)((channels<<6u)|((length>>11u)&3u));
    frame[4]=(uint8_t)(length>>3u);
    frame[5]=(uint8_t)(((length&7u)<<5u)|0x1fu);frame[6]=0xfcu;
    memcpy(frame+9u,raw->bytes,raw->bit/8u);
    uint16_t crc=0xffffu;
    assert(sauti_aac_adts_crc16_update_bits(frame,7u,0u,56u,&crc));
    assert(sauti_aac_adts_crc16_update_bits(frame+9u,raw->bit/8u,3u,
                                            sce_bits-3u,&crc));
    assert(sauti_aac_adts_crc16_update_zero_bits(192u-(sce_bits-3u),&crc));
    frame[7]=(uint8_t)(crc>>8u);frame[8]=(uint8_t)crc;
    return length;
}
static size_t frame(Bits *raw,uint8_t output[1024],
                    const Bits *payload,unsigned extra,unsigned *start){
    *raw=(Bits){0};
    if(extra)harmless(raw); /* old SCE parser supports initial harmless FIL */
    sce_silence(raw);
    if(extra)harmless(raw); /* and another harmless FIL after SCE */
    fil(raw,payload,start);
    if(extra)harmless(raw);
    put(raw,3u,7u); /* ID_END */
    pad(raw);
    return adts(output,raw,1u);
}
static void test_full_adts_and_header_reuse(void){
    SautiAacSbrHeader h;SautiAacSbrTables tab;
    find_header(&h,&tab);
    Bits p,raw;
    size_t syntax_bits=0u;
    sbr_bits(&p,&h,&tab,1u,0u,&syntax_bits);
    Bits crc_payload;
    sbr_crc_bits(&crc_payload,&p,syntax_bits);
    p=crc_payload;
    {
        SautiAacSbrPayloadState crc_state={0};
        SautiAacSbrPayloadFrame crc_params;
        assert(sauti_aac_sbr_payload_sce_parse(
            p.bytes,sizeof(p.bytes),0u,p.bit,48000u,
            &crc_state,&crc_params));
    }
    uint8_t adts_frame[1024];
    unsigned start=0u;
    const size_t n=frame(&raw,adts_frame,&p,1u,&start);
    assert(start%8u!=0u);

    SautiAacHeAdtsMonoState state={0};
    assert(sauti_aac_he_adts_mono_init(&state));
    SautiAacReason reason=SAUTI_AAC_REASON_INVALID_ADTS;
    float pcm[1024];
    for(unsigned i=0;i<1024;i++)pcm[i]=-55.0f;
    SautiAacSbrPayloadFrame params;
    assert(sauti_aac_he_adts_mono_decode_ex(
        adts_frame,n,&state,pcm,&params,&reason)==1);
    assert(reason==SAUTI_AAC_REASON_OK);
    assert(state.frames==1u && state.sbr.frames==1u);
    assert(state.core.initialized);
    for(unsigned i=0;i<1024u;i++)assert(pcm[i]==0.0f);
    assert(params.tables.n_high==tab.n_high &&
           params.harmonic[0]==1u &&
           params.grid.env_count==2u);
    assert(params.env_edges[2]==32u && params.noise_edges[2]==32u);
    SautiAacSbrHfJointConfig hf;
    assert(sauti_aac_sbr_payload_make_hf(&params,&hf));
    assert(hf.tables==&params.tables && hf.slots==32u);
    /* Verify the FIL offsets returned by generic AAC fill parser
     * are the exact positions passed to SBR-29. */
    size_t at=(size_t)start;
    SautiAacSbrPayloadState fresh={0};
    SautiAacSbrPayloadFrame direct;
    assert(sauti_aac_sbr_payload_sce_parse(
        adts_frame+7u,n-7u,at,p.bit,48000u,&fresh,&direct));
    assert(memcmp(&params,&direct,sizeof(params))==0);

    sbr_bits(&p,&h,&tab,0u,1u,NULL); /* no SBR header, time-coded Huffman */
    const size_t next_length=frame(&raw,adts_frame,&p,0u,NULL);
    SautiAacSbrPayloadFrame prev=params;
    assert(sauti_aac_he_adts_mono_decode_ex(
        adts_frame,next_length,&state,pcm,&params,&reason)==1);
    assert(state.frames==2u && state.sbr.frames==2u &&
           state.sbr.quant_state.has_env);
    assert(memcmp(&prev.energy,&params.energy,sizeof(prev.energy))==0);
    assert(params.harmonic[0]==1u);
    for(unsigned i=0;i<1024u;i++)assert(pcm[i]==0.0f);
    printf("SBR-30: real ADTS 24 kHz AAC-LC SCE + unaligned FIL 48 kHz SBR; header reuse and native core PCM PASS\n");
}
static void test_rollbacks_and_unsupported(void){
    SautiAacSbrHeader h;SautiAacSbrTables t;find_header(&h,&t);
    Bits p,raw;
    sbr_bits(&p,&h,&t,1u,0u,NULL);
    uint8_t bytes[1024];
    unsigned extension_start=0u;
    const size_t length=frame(&raw,bytes,&p,0u,&extension_start);
    SautiAacHeAdtsMonoState state={0};
    assert(sauti_aac_he_adts_mono_init(&state));
    float pcm[1024],old_pcm[1024];
    for(unsigned i=0;i<1024;i++)pcm[i]=(float)i+0.5f;
    memcpy(old_pcm,pcm,sizeof(pcm));
    SautiAacSbrPayloadFrame output,saved;
    memset(&output,0x9d,sizeof(output));saved=output;
    SautiAacHeAdtsMonoState prior=state;
    SautiAacReason reason;
#define FAIL(F,N) do { \
    assert(sauti_aac_he_adts_mono_decode_ex( \
        (F),(N),&state,pcm,&output,&reason)!=1); \
    assert(memcmp(&state,&prior,sizeof(state))==0); \
    assert(memcmp(pcm,old_pcm,sizeof(pcm))==0); \
    assert(memcmp(&output,&saved,sizeof(output))==0); \
} while(0)
    FAIL(NULL,length);
    FAIL(bytes,0u);
    FAIL(bytes,length-1u);
    const uint8_t adts_id=bytes[1];
    bytes[1]&=(uint8_t)~1u; /* ADTS CRC-present -> reject */
    FAIL(bytes,length);bytes[1]=adts_id;
    const uint8_t chan=bytes[3];
    bytes[3]|=0x80u;FAIL(bytes,length);bytes[3]=chan; /* stereo */
    const unsigned pos=7u*8u+extension_start;
    const unsigned byte=pos/8u,shift=4u-(pos%8u);
    /* EXT_SBR_DATA 13 -> EXT_SBR_DATA_CRC 14, still 4 bits. */
    bytes[byte]^=(uint8_t)(3u<<shift);
    FAIL(bytes,length);
    bytes[byte]^=(uint8_t)(3u<<shift);
    const unsigned last_payload=7u*8u+extension_start+
                                 (unsigned)p.bit-1u;
    bytes[last_payload/8u]^=(uint8_t)(1u<<(7u-last_payload%8u));
    FAIL(bytes,length);
    bytes[last_payload/8u]^=(uint8_t)(1u<<(7u-last_payload%8u));
#undef FAIL

    /* A syntactically valid SCE but without SBR extension is not HE-AAC.
     * Never silently mark it as decoded high-frequency audio. */
    raw=(Bits){0};sce_silence(&raw);put(&raw,3u,7u);pad(&raw);
    const size_t plain=adts(bytes,&raw,1u);
    assert(sauti_aac_he_adts_mono_decode_ex(
        bytes,plain,&state,pcm,&output,&reason)==-1);
    assert(reason==SAUTI_AAC_REASON_UNSUPPORTED_FIL);
    assert(memcmp(&state,&prior,sizeof(state))==0);
    assert(memcmp(pcm,old_pcm,sizeof(pcm))==0);
    assert(memcmp(&output,&saved,sizeof(output))==0);
    puts("SBR-30: ADTS CRC/stereo/truncation, corrupted FIL and missing SBR roll back both channel states");
}
static void test_fill_data_after_sbr(void){
    SautiAacSbrHeader h;SautiAacSbrTables t;
    find_header(&h,&t);
    Bits payload,raw,fill_data={0};
    sbr_bits(&payload,&h,&t,1u,0u,NULL);
    put(&fill_data,4u,1u); /* EXT_FILL_DATA */
    put(&fill_data,4u,0u); /* mandatory zero nibble */
    put(&fill_data,8u,0xa5u);
    raw=(Bits){0};
    sce_silence(&raw);
    fil(&raw,&payload,NULL);
    const size_t start=raw.bit;
    fil(&raw,&fill_data,NULL);
    put(&raw,3u,7u);pad(&raw);
    uint8_t bytes[1024];
    const size_t length=adts(bytes,&raw,1u);
    SautiAacHeAdtsMonoState state={0};
    assert(sauti_aac_he_adts_mono_init(&state));
    SautiAacReason reason;
    float pcm[1024];
    SautiAacSbrPayloadFrame params;
    assert(sauti_aac_he_adts_mono_decode_ex(
        bytes,length,&state,pcm,&params,&reason)==1);
    assert(reason==SAUTI_AAC_REASON_OK && state.frames==1u);

    /* Corrupt the A5 pattern. Decoder must reject atomically. */
    const size_t corrupt_bit=start+15u; /* ID 3 + count 4 + type 4 + zero 4 */
    bytes[7u+corrupt_bit/8u]^=(uint8_t)(1u<<(7u-(unsigned)(corrupt_bit%8u)));
    SautiAacHeAdtsMonoState fresh={0};
    assert(sauti_aac_he_adts_mono_init(&fresh));
    const SautiAacHeAdtsMonoState before=fresh;
    float rejected[1024];
    for(unsigned i=0;i<1024u;i++)rejected[i]=-7.0f;
    SautiAacSbrPayloadFrame old;
    memset(&old,0x39,sizeof(old));
    SautiAacSbrPayloadFrame unchanged=old;
    assert(sauti_aac_he_adts_mono_decode_ex(
        bytes,length,&fresh,rejected,&old,&reason)!=1);
    assert(memcmp(&fresh,&before,sizeof(fresh))==0);
    assert(memcmp(&old,&unchanged,sizeof(old))==0);
    for(unsigned i=0;i<1024u;i++)assert(rejected[i]==-7.0f);
    puts("SBR-34: EXT_FILL_DATA A5 accepted; corrupted filler rollback PASS");
}
static void test_temporal_core_keeps_strict_parity(void){
    SautiAacSbrHeader h;SautiAacSbrTables tab;
    find_header(&h,&tab);
    Bits p,raw;
    sbr_bits(&p,&h,&tab,1u,0u,NULL);
    uint8_t bytes[1024];
    size_t len=frame(&raw,bytes,&p,0u,NULL);
    SautiAacHeAdtsMonoState strict={0},temporal={0};
    assert(sauti_aac_he_adts_mono_init(&strict));
    assert(sauti_aac_he_adts_mono_init(&temporal));
    float a[1024],b[1024];
    SautiAacSbrPayloadFrame p1,p2;
    SautiAacReason reason1,reason2;
    assert(sauti_aac_he_adts_mono_decode_ex(
        bytes,len,&strict,a,&p1,&reason1)==1);
    assert(sauti_aac_he_adts_mono_temporal_decode_ex(
        bytes,len,&temporal,b,&p2,&reason2)==1);
    assert(reason1==SAUTI_AAC_REASON_OK &&
           reason2==SAUTI_AAC_REASON_OK);
    assert(memcmp(a,b,sizeof(a))==0);
    assert(memcmp(&p1,&p2,sizeof(p1))==0);
    assert(memcmp(&strict,&temporal,sizeof(strict))==0);

    sbr_bits(&p,&h,&tab,0u,1u,NULL);
    unsigned ext_start=0u;
    len=frame(&raw,bytes,&p,0u,&ext_start);
    assert(sauti_aac_he_adts_mono_decode_ex(
        bytes,len,&strict,a,&p1,&reason1)==1);
    assert(sauti_aac_he_adts_mono_temporal_decode_ex(
        bytes,len,&temporal,b,&p2,&reason2)==1);
    assert(memcmp(a,b,sizeof(a))==0);
    assert(memcmp(&strict,&temporal,sizeof(strict))==0);

    /* A corrupt extension on a warmed stream must not advance state. */
    const SautiAacHeAdtsMonoState prev=temporal;
    const size_t extension_bit=ext_start;
    bytes[7u+extension_bit/8u]^=(uint8_t)(1u<<(7u-extension_bit%8u));
    const int result=sauti_aac_he_adts_mono_temporal_decode_ex(
        bytes,len,&temporal,b,&p2,&reason2);
    assert(result!=1);
    assert(memcmp(&temporal,&prev,sizeof(prev))==0);
    puts("SBR-44: temporal and strict core output/history parity, invalid rollback PASS");
}
static void test_adts_crc_sce(void){
    SautiAacSbrHeader h;SautiAacSbrTables tab;
    find_header(&h,&tab);
    Bits payload,raw;
    sbr_bits(&payload,&h,&tab,1u,0u,NULL);
    raw=(Bits){0};sce_silence(&raw);const size_t sce_bits=raw.bit;
    fil(&raw,&payload,NULL);put(&raw,3u,7u);pad(&raw);
    uint8_t bytes[1024];const size_t length=adts_crc(bytes,&raw,1u,sce_bits);
    SautiAacHeAdtsMonoState state={0},before;
    assert(sauti_aac_he_adts_mono_init(&state));
    float pcm[1024],saved[1024];SautiAacSbrPayloadFrame params,saved_params;
    SautiAacReason reason=SAUTI_AAC_REASON_INVALID_ADTS;
    assert(sauti_aac_he_adts_mono_decode_ex(
        bytes,length,&state,pcm,&params,&reason)==1);
    assert(reason==SAUTI_AAC_REASON_OK && state.frames==1u);
    before=state;memcpy(saved,pcm,sizeof(pcm));saved_params=params;
    bytes[9u]^=0x10u; /* protected element_instance_tag bit */
    assert(sauti_aac_he_adts_mono_decode_ex(
        bytes,length,&state,pcm,&params,&reason)!=1);
    assert(memcmp(&state,&before,sizeof(state))==0);
    assert(memcmp(pcm,saved,sizeof(pcm))==0);
    assert(memcmp(&params,&saved_params,sizeof(params))==0);
    puts("SBR-65: ADTS CRC-16 SCE accepts covered syntax and rolls back on corruption PASS");
}
int main(void){
    test_full_adts_and_header_reuse();
    test_rollbacks_and_unsupported();
    test_fill_data_after_sbr();
    test_temporal_core_keeps_strict_parity();
    test_adts_crc_sce();
    puts("PASS HE-AAC SBR-30 genuine ADTS mono SCE+FIL -> native LC PCM and decoded SBR side data");
    return 0;
}
