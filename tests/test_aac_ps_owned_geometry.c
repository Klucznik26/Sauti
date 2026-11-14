#include "sauti_aac_sbr_qmf_frame.h"
#include "sauti_aac_sbr_master.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Two distinct valid HE-AAC geometries. Independent low-QMF reference
 * checks every output bin, including the first two inherited slots. */
static void geometry(unsigned rate,unsigned k2,
                     SautiAacSbrMaster *m,SautiAacSbrTables *t,
                     SautiAacSbrPatches *p){
    assert(sauti_aac_sbr_master_linear(16u,k2,1u,0u,m));
    assert(sauti_aac_sbr_tables_build(m,1u,t));
    assert(sauti_aac_sbr_patch_build(rate,m,t,p));
}
int main(void){
    SautiAacSbrMaster mp,mc;
    SautiAacSbrTables tp,tc;
    SautiAacSbrPatches pp,pc;
    geometry(48000u,32u,&mp,&tp,&pp);
    geometry(96000u,48u,&mc,&tc,&pc);
    float pcm[1024u];
    for(unsigned i=0;i<1024u;i++)pcm[i]=(float)((int)((i*23u)%113u)-56)*0.125f;
    SautiAacSbrQmfFrameState owned={0},ordinary={0};
    SautiAacSbrQmfAnalysisState ref={0};
    SautiAacSbrComplex actual[2049u],plain[2048u],low[1024u];
    SautiAacSbrQmfStage stage;
    /* Warm the history with an independent real analysis frame. */
    assert(sauti_aac_sbr_qmf_frame_export_raw(
        &tp,&pp,pcm,1024u,plain,2048u,&owned,&stage));
    assert(sauti_aac_sbr_qmf_frame_export_raw(
        &tp,&pp,pcm,1024u,plain,2048u,&ordinary,&stage));
    assert(sauti_aac_sbr_qmf_analyze_32(pcm,1024u,32u,32u,
                                       low,1024u,&ref));
    assert(sauti_aac_sbr_qmf_analyze_32(pcm,1024u,32u,32u,
                                       low,1024u,&ref));
    actual[2048u]=(SautiAacSbrComplex){123.0f,456.0f};
    assert(sauti_aac_sbr_qmf_frame_export_raw_owned(
        &tc,&pc,&tp,&pp,2u,pcm,1024u,actual,2048u,&owned,&stage));
    assert(stage==SAUTI_SBR_QMF_STAGE_OK);
    assert(actual[2048u].re==123.0f&&actual[2048u].im==456.0f);
    assert(sauti_aac_sbr_qmf_frame_export_raw(
        &tc,&pc,pcm,1024u,plain,2048u,&ordinary,&stage));
    unsigned changed=0u;
    for(unsigned t=0;t<32u;t++){
        const SautiAacSbrTables *tab=t<2u?&tp:&tc;
        const SautiAacSbrPatches *patch=t<2u?&pp:&pc;
        for(unsigned k=0;k<64u;k++){
            SautiAacSbrComplex expected={0};
            if(k<tab->kx)expected=low[t*32u+k];
            else if(k<tab->kx+tab->m&&patch->source_for_target[k]>=0)
                expected=low[t*32u+(unsigned)patch->source_for_target[k]];
            assert(memcmp(&actual[t*64u+k],&expected,
                          sizeof(expected))==0);
            if(t<2u && memcmp(&actual[t*64u+k],&plain[t*64u+k],
                               sizeof(expected))!=0)changed++;
            if(t>=2u)assert(memcmp(&actual[t*64u+k],
                                   &plain[t*64u+k],sizeof(expected))==0);
        }
    }
    assert(changed>0u);
    assert(memcmp(&owned.analysis,&ordinary.analysis,
                  sizeof(owned.analysis))==0);
    SautiAacSbrQmfFrameState snapshot=owned;
    SautiAacSbrComplex saved[2049u];
    memcpy(saved,actual,sizeof(saved));
    assert(!sauti_aac_sbr_qmf_frame_export_raw_owned(
        &tc,&pc,&tp,&pp,7u,pcm,1024u,actual,2048u,&owned,&stage));
    assert(memcmp(&owned,&snapshot,sizeof(owned))==0);
    assert(memcmp(actual,saved,sizeof(saved))==0);
    assert(!sauti_aac_sbr_qmf_frame_export_raw_owned(
        &tc,&pc,NULL,NULL,2u,pcm,1024u,actual,2048u,&owned,&stage));
    assert(memcmp(&owned,&snapshot,sizeof(owned))==0);
    pcm[1023u]=NAN;
    assert(!sauti_aac_sbr_qmf_frame_export_raw_owned(
        &tc,&pc,&tp,&pp,2u,pcm,1024u,actual,2048u,&owned,&stage));
    assert(memcmp(&owned,&snapshot,sizeof(owned))==0);
    assert(memcmp(actual,saved,sizeof(saved))==0);
    printf("PASS PS owner geometry: %u changed bins, 2 inherited slots, "
           "single analysis and atomic rollback\n",changed);
    return 0;
}
