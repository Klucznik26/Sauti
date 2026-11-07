/* Reuse the independently serialized CPE-SBR payload fixture. */
#define main sbr_payload_fixture_main
#include "test_aac_sbr_payload.c"
#undef main

#include "sauti_aac_he_adts_cpe_qmf.h"
#include "sauti_aac_adts_crc.h"
#include "sauti_decoder_aac_native.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void cpe_core_silence(Bits *raw){
    *raw=(Bits){0};
    put(raw,3u,1u);put(raw,4u,0u);put(raw,1u,1u); /* ID_CPE common */
    put(raw,1u,0u);put(raw,2u,0u);put(raw,1u,0u); /* shared long ICS */
    put(raw,6u,0u);put(raw,1u,0u);put(raw,2u,0u); /* no SFB, no MS */
    for(unsigned ch=0u;ch<2u;ch++){
        put(raw,8u,100u); /* global_gain */
        put(raw,1u,0u);put(raw,1u,0u);put(raw,1u,0u); /* tool flags */
    }
}
static void append_fil(Bits *raw,const Bits *payload,size_t *start){
    assert((payload->bit&7u)==0u && payload->bit/8u>=1u &&
           payload->bit/8u<=269u);
    const unsigned count=(unsigned)(payload->bit/8u);
    put(raw,3u,6u);
    if(count<15u)put(raw,4u,count);
    else {put(raw,4u,15u);put(raw,8u,count-14u);}
    if(start)*start=raw->bit;
    for(size_t i=0u;i<payload->bit;i++)put(raw,1u,bit_at(payload,i));
}
static size_t adts_stereo(uint8_t frame[1024],const Bits *raw){
    assert((raw->bit&7u)==0u);
    const unsigned length=7u+(unsigned)(raw->bit/8u);
    memset(frame,0,1024u);
    frame[0]=0xffu;frame[1]=0xf1u;
    frame[2]=(uint8_t)(0x40u|(6u<<2u)); /* AAC-LC, 24 kHz */
    frame[3]=(uint8_t)(0x80u|((length>>11u)&3u));
    frame[4]=(uint8_t)(length>>3u);
    frame[5]=(uint8_t)(((length&7u)<<5u)|0x1fu);frame[6]=0xfcu;
    memcpy(frame+7u,raw->data,raw->bit/8u);
    return length;
}
static size_t adts_stereo_crc(uint8_t frame[1024],const Bits *raw,
                              size_t cpe_end,size_t second_ics){
    assert((raw->bit&7u)==0u && cpe_end>=3u && second_ics<=cpe_end);
    const unsigned length=9u+(unsigned)(raw->bit/8u);
    memset(frame,0,1024u);frame[0]=0xffu;frame[1]=0xf0u;
    frame[2]=(uint8_t)(0x40u|(6u<<2u));
    frame[3]=(uint8_t)(0x80u|((length>>11u)&3u));
    frame[4]=(uint8_t)(length>>3u);
    frame[5]=(uint8_t)(((length&7u)<<5u)|0x1fu);frame[6]=0xfcu;
    memcpy(frame+9u,raw->data,raw->bit/8u);
    uint16_t crc=0xffffu;size_t first=cpe_end-3u,second=cpe_end-second_ics;
    if(first>192u)first=192u;
    if(second>128u)second=128u;
    assert(sauti_aac_adts_crc16_update_bits(frame,7u,0u,56u,&crc));
    assert(sauti_aac_adts_crc16_update_bits(frame+9u,raw->bit/8u,3u,first,&crc));
    assert(sauti_aac_adts_crc16_update_zero_bits(192u-first,&crc));
    assert(sauti_aac_adts_crc16_update_bits(frame+9u,raw->bit/8u,second_ics,second,&crc));
    assert(sauti_aac_adts_crc16_update_zero_bits(128u-second,&crc));
    frame[7]=(uint8_t)(crc>>8u);frame[8]=(uint8_t)crc;return length;
}
static void test_cpe_adts_crc_bridge(void){
    SautiAacSbrHeader header;SautiAacSbrTables tables;
    choose(&header,&tables);
    Bits side,checked,raw;
    cpe_payload(&side,&header,&tables);
    SautiAacSbrPayloadCpeState inspect={0};
    SautiAacSbrPayloadCpeFrame parsed;
    assert(sauti_aac_sbr_payload_cpe_parse(
        side.data,sizeof(side.data),0u,side.bit,48000u,&inspect,&parsed));
    crc_payload(&checked,&side,parsed.channel[0].payload_data_bits);
    cpe_core_silence(&raw);
    size_t side_start=0u;
    append_fil(&raw,&checked,&side_start);
    put(&raw,3u,7u);align_byte(&raw);
    uint8_t frame[1024];const size_t bytes=adts_stereo(frame,&raw);
    uint8_t coupled_frame[1024];memcpy(coupled_frame,frame,sizeof(frame));
    SautiAacHeAdtsCpeState state={0};
    assert(sauti_aac_he_adts_cpe_init(&state));
    float pcm[2048],saved_pcm[2048];
    SautiAacSbrPayloadCpeFrame output,saved_payload;
    SautiAacReason reason=SAUTI_AAC_REASON_INVALID_ADTS;
    assert(sauti_aac_he_adts_cpe_decode_ex(
        frame,bytes,&state,pcm,&output,&reason)==1);
    assert(reason==SAUTI_AAC_REASON_OK && state.frames==1u &&
           state.sbr.frames==1u && output.coupling==1u);
    for(unsigned i=0u;i<2048u;i++)assert(pcm[i]==0.0f);
    SautiAacHeAdtsCpeQmfState qmf={0};
    assert(sauti_aac_he_adts_cpe_qmf_init(&qmf));
    float pcm_2x[4096];
    SautiAacSbrLimiterReport reports[2];
    assert(sauti_aac_he_adts_cpe_qmf_decode_ex(
        frame,bytes,&qmf,pcm_2x,reports,&reason)==1);
    assert(reason==SAUTI_AAC_REASON_OK && qmf.frames==1u &&
           qmf.qmf[0].frames==1u && qmf.qmf[1].frames==1u);
    for(unsigned i=0u;i<4096u;i++)assert(isfinite(pcm_2x[i]));
    uint8_t stream[2048];
    memcpy(stream,frame,bytes);memcpy(stream+bytes,frame,bytes);
    const SautiDecoderOps *registry[]={&sauti_decoder_aac_he_cpe_native_ops};
    SautiDecoder decoder={0};
    assert(sauti_decoder_open(&decoder,registry,1u,stream,2u*bytes));
    assert(decoder.ops==&sauti_decoder_aac_he_cpe_native_ops);
    assert(decoder.info.sample_rate==48000u && decoder.info.channels==2u &&
           decoder.info.total_frames==4096u);
    float expected[8192],replayed[1554];
    assert(sauti_decoder_read(&decoder,expected,4096u)==4096u);
    for(unsigned i=0u;i<8192u;i++)assert(isfinite(expected[i]));
    assert(sauti_decoder_seek(&decoder,1231u));
    assert(sauti_decoder_read(&decoder,replayed,777u)==777u);
    assert(memcmp(replayed,expected+2u*1231u,sizeof(replayed))==0);
    sauti_decoder_close(&decoder);
    Bits plain={0};
    cpe_core_silence(&plain);put(&plain,3u,7u);align_byte(&plain);
    const size_t plain_bytes=adts_stereo(stream,&plain);
    assert(sauti_decoder_aac_he_cpe_native_ops.probe(stream,plain_bytes));
    assert(!sauti_decoder_open(&decoder,registry,1u,stream,plain_bytes));

    /* The separate uncoupled ordering must traverse the complete ADTS,
     * CPE, QMF and public-API route, not just the side-data parser. */
    Bits uncoupled_side,uncoupled_checked,uncoupled_raw;
    uncoupled_cpe_payload(&uncoupled_side,&header,&tables);
    SautiAacSbrPayloadCpeState uncoupled_inspect={0};
    SautiAacSbrPayloadCpeFrame uncoupled_parsed;
    assert(sauti_aac_sbr_payload_cpe_parse(
        uncoupled_side.data,sizeof(uncoupled_side.data),0u,uncoupled_side.bit,
        48000u,&uncoupled_inspect,&uncoupled_parsed));
    crc_payload(&uncoupled_checked,&uncoupled_side,
                uncoupled_parsed.channel[0].payload_data_bits);
    cpe_core_silence(&uncoupled_raw);
    append_fil(&uncoupled_raw,&uncoupled_checked,NULL);
    put(&uncoupled_raw,3u,7u);align_byte(&uncoupled_raw);
    const size_t uncoupled_bytes=adts_stereo(frame,&uncoupled_raw);
    SautiAacHeAdtsCpeQmfState uncoupled_qmf={0};
    assert(sauti_aac_he_adts_cpe_qmf_init(&uncoupled_qmf));
    assert(sauti_aac_he_adts_cpe_qmf_decode_ex(
        frame,uncoupled_bytes,&uncoupled_qmf,pcm_2x,reports,&reason)==1);
    assert(reason==SAUTI_AAC_REASON_OK && uncoupled_qmf.frames==1u &&
           uncoupled_qmf.adts.sbr.frames==1u);
    for(unsigned i=0u;i<4096u;i++)assert(isfinite(pcm_2x[i]));
    memcpy(stream,frame,uncoupled_bytes);
    memcpy(stream+uncoupled_bytes,frame,uncoupled_bytes);
    assert(sauti_decoder_open(&decoder,registry,1u,stream,2u*uncoupled_bytes));
    assert(sauti_decoder_read(&decoder,expected,4096u)==4096u);
    for(unsigned i=0u;i<8192u;i++)assert(isfinite(expected[i]));
    sauti_decoder_close(&decoder);

    /* Restore the coupled fixture before the CRC-rollback assertion. */
    memcpy(frame,coupled_frame,sizeof(frame));
    SautiAacHeAdtsCpeState previous=state;
    memcpy(saved_pcm,pcm,sizeof(pcm));saved_payload=output;
    /* Flip only bs_sbr_crc_bits; CPE syntax stays otherwise valid. */
    const size_t crc_bit=7u*8u+side_start+4u;
    frame[crc_bit/8u]^=(uint8_t)(1u<<(7u-crc_bit%8u));
    assert(sauti_aac_he_adts_cpe_decode_ex(
        frame,bytes,&state,pcm,&output,&reason)!=1);
    assert(memcmp(&state,&previous,sizeof(state))==0);
    assert(memcmp(pcm,saved_pcm,sizeof(pcm))==0);
    assert(memcmp(&output,&saved_payload,sizeof(output))==0);
    /* Build a protected transport independently from the parsed CPE
     * boundaries; a protected CPE bit must fail before state commits. */
    cpe_core_silence(&raw);append_fil(&raw,&checked,NULL);put(&raw,3u,7u);align_byte(&raw);
    SautiAacCpeData crc_cpe;size_t crc_end=0u;
    assert(sauti_aac_lc_cpe_parse(raw.data,raw.bit/8u,&crc_end,6u,&crc_cpe,&reason)==1);
    const size_t crc_bytes=adts_stereo_crc(frame,&raw,crc_end,crc_cpe.crc_second_ics_bit);
    SautiAacHeAdtsCpeState crc_state={0},crc_before;
    assert(sauti_aac_he_adts_cpe_init(&crc_state));
    assert(sauti_aac_he_adts_cpe_decode_ex(frame,crc_bytes,&crc_state,pcm,&output,&reason)==1);
    crc_before=crc_state;frame[9u]^=0x10u;
    assert(sauti_aac_he_adts_cpe_decode_ex(frame,crc_bytes,&crc_state,pcm,&output,&reason)!=1);
    assert(memcmp(&crc_state,&crc_before,sizeof(crc_state))==0);
    puts("SBR-64: ADTS CPE + coupled/uncoupled type-14 SBR -> core, dual-QMF and adapter PASS");
}
int main(void){test_cpe_adts_crc_bridge();return 0;}
