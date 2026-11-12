#include "sauti_aac_he_adts_ps.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Reuse the pre-existing real ADTS/SBR bit-writer as a fixture.
 * It emits an actual AAC-LC SCE + full SBR FIL; its PS flag is
 * absent, so this strict PS adapter must reject the whole frame. */
#define main sauti_existing_mono_fixture_main
#include "test_aac_sbr_adts_mono.c"
#undef main

int main(void){
    SautiAacSbrHeader header;
    SautiAacSbrTables tables;
    find_header(&header,&tables);
    Bits payload,raw;
    sbr_bits(&payload,&header,&tables,1u,0u,NULL);
    uint8_t bytes[1024u];
    const size_t count=frame(&raw,bytes,&payload,0u,NULL);

    SautiAacHeAdtsPsState state={0};
    assert(sauti_aac_he_adts_ps_init(&state));
    const SautiAacHeAdtsPsState original=state;
    SautiAacSbrLimiterReport report,saved_report;
    memset(&report,0x59,sizeof(report));
    saved_report=report;
    float pcm[4096u+1u],saved_pcm[4096u+1u];
    for(size_t i=0;i<4097u;i++)pcm[i]=(-13.0f);
    memcpy(saved_pcm,pcm,sizeof(pcm));
    unsigned produced=81u;
    SautiAacReason reason=SAUTI_AAC_REASON_OK;
    assert(sauti_aac_he_adts_ps_decode_ex(
        bytes,count,&state,pcm,4096u,&produced,&report,&reason)==-1);
    assert(reason==SAUTI_AAC_REASON_UNSUPPORTED_FIL);
    assert(memcmp(&state,&original,sizeof(state))==0);
    assert(memcmp(&report,&saved_report,sizeof(report))==0);
    assert(memcmp(pcm,saved_pcm,sizeof(pcm))==0);
    assert(produced==81u);
    assert(sauti_aac_he_adts_ps_decode_ex(
        bytes,count-1u,&state,pcm,4096u,&produced,&report,&reason)!=1);
    assert(memcmp(&state,&original,sizeof(state))==0);
    assert(memcmp(&report,&saved_report,sizeof(report))==0);
    assert(memcmp(pcm,saved_pcm,sizeof(pcm))==0);
    assert(produced==81u);
    assert(sauti_aac_he_adts_ps_decode_ex(
        bytes,count,&state,pcm,4095u,&produced,&report,&reason)==0);
    assert(memcmp(&state,&original,sizeof(state))==0);
    assert(memcmp(&report,&saved_report,sizeof(report))==0);
    assert(produced==81u);
    state.frames=1u;
    const SautiAacHeAdtsPsState bad=state;
    assert(sauti_aac_he_adts_ps_decode_ex(
        bytes,count,&state,pcm,4096u,&produced,&report,&reason)==0);
    assert(memcmp(&state,&bad,sizeof(state))==0);
    assert(memcmp(&report,&saved_report,sizeof(report))==0);
    assert(memcmp(pcm,saved_pcm,sizeof(pcm))==0);
    assert(produced==81u);
    puts("PASS AAC PS-4c2: genuine ADTS SCE/SBR without PS rejected atomically; truncation, sizes, history");
    return 0;
}
