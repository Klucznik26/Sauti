#include "sauti_aac_ps_qmf_bank.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void build(SautiAacPsHybridSchedule *s,unsigned mode){
    SautiAacPsFrame frame={0};SautiAacPsMappedFrame mapped={0};
    frame.resolved_num_env=1u;frame.border_position[0]=31u;
    mapped.bands=mode;mapped.env_count=1u;
    assert(sauti_aac_ps_hybrid_schedule_build(&frame,&mapped,s));
}
int main(void){
    SautiAacPsHybridSchedule schedule={0};
    SautiAacSbrComplex qmf[38u*64u],out[32u*91u+1u],low[32u*32u];
    SautiAacPsQmfLowState state={0},reference={0};
    for(unsigned i=0;i<38u*64u;i++)
        qmf[i]=(SautiAacSbrComplex){(float)(i%157u)*0.001f,
                                    -(float)(i%127u)*0.002f};
    for(unsigned mode=20u;mode<=34u;mode+=14u){
        const unsigned bands=mode==20u?71u:91u;
        const unsigned nb=mode==20u?10u:32u;
        const unsigned first=mode==20u?3u:5u;
        build(&schedule,mode);
        memset(&state,0,sizeof(state));memset(&reference,0,sizeof(reference));
        out[32u*bands]=(SautiAacSbrComplex){123.0f,456.0f};
        assert(sauti_aac_ps_qmf_bank_analyze(&schedule,qmf,38u*64u,
                                               &state,out,32u*bands));
        assert(sauti_aac_ps_qmf_low_analyze(&schedule,qmf,38u*64u,
                                              &reference,low,32u*nb));
        assert(memcmp(&state,&reference,sizeof(state))==0);
        for(unsigned t=0;t<32u;t++){
            for(unsigned k=0;k<nb;k++){
                const SautiAacSbrComplex a=out[t*bands+k],b=low[t*nb+k];
                assert(a.re==b.re&&a.im==b.im);
            }
            /* FFmpeg aacps.c: high bins use unshifted L[t][first+k]. */
            for(unsigned k=nb;k<bands;k++){
                const SautiAacSbrComplex a=out[t*bands+k];
                const SautiAacSbrComplex b=qmf[t*64u+first+k-nb];
                assert(a.re==b.re&&a.im==b.im);
            }
        }
        assert(out[32u*bands].re==123.0f);
        SautiAacSbrComplex saved[32u*91u];
        memcpy(saved,out,sizeof(saved));
        SautiAacPsQmfLowState old=state;
        /* A bad high-band value must NOT publish low state or output. */
        const unsigned bad=first+3u;
        const SautiAacSbrComplex orig=qmf[bad];
        qmf[bad].re=NAN;
        assert(!sauti_aac_ps_qmf_bank_analyze(&schedule,qmf,38u*64u,
                                                &state,out,32u*bands));
        assert(memcmp(&old,&state,sizeof(state))==0);
        assert(memcmp(saved,out,sizeof(saved))==0);
        qmf[bad]=orig;
        assert(!sauti_aac_ps_qmf_bank_analyze(&schedule,qmf,38u*64u,
                                                &state,out,32u*bands-1u));
        assert(memcmp(&old,&state,sizeof(state))==0);
        assert(memcmp(saved,out,sizeof(saved))==0);
    }
    puts("PASS AAC PS-1c: full 71/91 hybrid QMF frame, unshifted high bins, atomic rollback");
    return 0;
}
