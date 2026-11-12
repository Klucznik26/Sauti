#include "sauti_aac_ps_pcm_stereo.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PCM_N (32u*64u)
#define PCM_ST (PCM_N*2u)

static void prepare(SautiAacPsHybridSchedule *s,
                    SautiAacPsComplexMatrix *m,unsigned mode){
    SautiAacPsFrame f={0};SautiAacPsMappedFrame mapped={0};
    f.resolved_num_env=1u;f.border_position[0]=31u;
    mapped.bands=mode;mapped.env_count=1u;
    assert(sauti_aac_ps_hybrid_schedule_build(&f,&mapped,s));
    memset(m,0,sizeof(*m));
    m->bands=mode;m->env_count=1u;
    for(unsigned k=0;k<mode;k++){
        m->coefficient[0][k].h11.re=1.0f;
        m->coefficient[0][k].h22.re=1.0f;
    }
}
static void test_pair(void){
    SautiAacSbrComplex l[PCM_N],r[PCM_N];
    SautiAacPsPcmStereoState actual={0},ref={0};
    float out[PCM_ST+1u]={0},left[PCM_N],right[PCM_N];
    float previous[PCM_ST+1u];
    for(unsigned frame=0;frame<3u;frame++){
        for(unsigned i=0;i<PCM_N;i++){
            const int v=(int)((i*17u+frame*23u)%113u)-56;
            l[i]=(SautiAacSbrComplex){v*0.01f,v*0.003f};
            r[i]=(SautiAacSbrComplex){v*-0.007f,v*0.011f};
        }
        out[PCM_ST]=77.0f;
        assert(sauti_aac_sbr_qmf_synth_64(l,PCM_N,32u,left,PCM_N,&ref.left));
        assert(sauti_aac_sbr_qmf_synth_64(r,PCM_N,32u,right,PCM_N,&ref.right));
        assert(sauti_aac_ps_pcm_stereo_from_qmf(l,PCM_N,r,PCM_N,
                                                 &actual,out,PCM_ST));
        for(unsigned i=0;i<PCM_N;i++){
            assert(out[2u*i]==left[i]);
            assert(out[2u*i+1u]==right[i]);
        }
        assert(out[PCM_ST]==77.0f);
        assert(memcmp(&actual,&ref,sizeof(actual))==0);
    }
    memcpy(previous,out,sizeof(previous));
    const SautiAacPsPcmStereoState old=actual;
    r[PCM_N-1u].re=NAN;
    assert(!sauti_aac_ps_pcm_stereo_from_qmf(l,PCM_N,r,PCM_N,
                                              &actual,out,PCM_ST));
    assert(memcmp(&actual,&old,sizeof(old))==0);
    assert(memcmp(out,previous,sizeof(previous))==0);
    r[PCM_N-1u].re=0.0f;
    assert(!sauti_aac_ps_pcm_stereo_from_qmf(l,PCM_N,r,PCM_N,
                                              &actual,out,PCM_ST-1u));
    assert(!sauti_aac_ps_pcm_stereo_from_qmf(l,PCM_N,r,PCM_N,
                                              &actual,out,(size_t)PCM_ST));
    /* Previous line is valid: ensure the state and guard are still correct. */
    assert(out[PCM_ST]==77.0f);
    ref=actual;
    actual.right.head=10u;
    SautiAacPsPcmStereoState corrupt=actual;
    memcpy(previous,out,sizeof(previous));
    assert(!sauti_aac_ps_pcm_stereo_from_qmf(l,PCM_N,r,PCM_N,
                                              &actual,out,PCM_ST));
    assert(memcmp(&actual,&corrupt,sizeof(actual))==0);
    assert(memcmp(out,previous,sizeof(previous))==0);
    (void)ref;
}
static void test_full(void){
    SautiAacPsHybridSchedule schedule={0};
    SautiAacPsComplexMatrix matrix={0};
    SautiAacSbrComplex mono[38u*64u],hl[32u*91u],hr[32u*91u];
    SautiAacSbrComplex ql[PCM_N],qr[PCM_N];
    SautiAacPsPcmPipelineState state={0},manual={0};
    float output[PCM_ST+1u]={0},reference[PCM_ST],left[PCM_N],right[PCM_N];
    for(unsigned mode=20u;mode<=34u;mode+=14u){
        const unsigned bands=mode==20u?71u:91u;
        prepare(&schedule,&matrix,mode);
        memset(&state,0,sizeof(state));memset(&manual,0,sizeof(manual));
        for(unsigned frame=0;frame<2u;frame++){
            for(unsigned t=0;t<38u;t++)for(unsigned k=0;k<64u;k++){
                const int v=(int)((t*37u+k*19u+frame*11u)%109u)-54;
                mono[t*64u+k]=(SautiAacSbrComplex){v*0.005f,v*-0.003f};
            }
            output[PCM_ST]=123.0f;
            assert(sauti_aac_ps_stereo_hybrid_apply(&schedule,&matrix,mono,38u*64u,
                        &manual.hybrid,hl,32u*bands,hr,32u*bands));
            assert(sauti_aac_ps_hybrid_synthesize_stereo(&schedule,hl,32u*bands,
                        hr,32u*bands,ql,PCM_N,qr,PCM_N));
            assert(sauti_aac_sbr_qmf_synth_64(ql,PCM_N,32u,left,PCM_N,
                                               &manual.synthesis.left));
            assert(sauti_aac_sbr_qmf_synth_64(qr,PCM_N,32u,right,PCM_N,
                                               &manual.synthesis.right));
            for(unsigned i=0;i<PCM_N;i++){
                reference[2u*i]=left[i];reference[2u*i+1u]=right[i];
            }
            assert(sauti_aac_ps_pcm_stereo_from_mono_qmf(&schedule,&matrix,
                            mono,38u*64u,&state,output,PCM_ST));
            assert(memcmp(output,reference,sizeof(reference))==0);
            assert(memcmp(&state,&manual,sizeof(state))==0);
            assert(output[PCM_ST]==123.0f);
        }
        const SautiAacPsPcmPipelineState saved=state;
        float previous[PCM_ST+1u];
        memcpy(previous,output,sizeof(previous));
        mono[0].im=NAN;
        assert(!sauti_aac_ps_pcm_stereo_from_mono_qmf(&schedule,&matrix,
                    mono,38u*64u,&state,output,PCM_ST));
        assert(memcmp(&saved,&state,sizeof(state))==0);
        assert(memcmp(previous,output,sizeof(previous))==0);
        mono[0].im=0.0f;
        matrix.coefficient[0][0].h11.re=NAN;
        assert(!sauti_aac_ps_pcm_stereo_from_mono_qmf(&schedule,&matrix,
                    mono,38u*64u,&state,output,PCM_ST));
        assert(memcmp(&saved,&state,sizeof(state))==0);
        assert(memcmp(previous,output,sizeof(previous))==0);
    }
}
int main(void){
    test_pair();test_full();
    puts("PASS AAC PS-3c: dual QMF synthesis, full PS->interleaved PCM, multiframe rollback");
    return 0;
}
