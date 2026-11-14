#include "sauti_aac_ps_hybrid_synth.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void schedule_make(SautiAacPsHybridSchedule *s,unsigned mode){
    SautiAacPsFrame frame={0};SautiAacPsMappedFrame mapped={0};
    frame.resolved_num_env=1u;frame.border_position[0]=31u;
    mapped.bands=mode;mapped.env_count=1u;
    assert(sauti_aac_ps_hybrid_schedule_build(&frame,&mapped,s));
}
static int almost(float x,float y){
    return fabsf(x-y)<0.0001f;
}
int main(void){
    SautiAacPsHybridSchedule s={0};
    SautiAacSbrComplex left[32u*91u],right[32u*91u];
    SautiAacSbrComplex lout[32u*64u+1u],rout[32u*64u+1u];
    SautiAacSbrComplex saved_l[32u*64u+1u],saved_r[32u*64u+1u];
    const unsigned splits20[3]={6u,2u,2u};
    const unsigned splits34[5]={12u,8u,4u,4u,4u};
    for(unsigned mode=20u;mode<=34u;mode+=14u){
        const unsigned bands=mode==20u?71u:91u;
        const unsigned first=mode==20u?3u:5u;
        const unsigned *splits=mode==20u?splits20:splits34;
        schedule_make(&s,mode);
        for(unsigned slot=0;slot<32u;slot++)
            for(unsigned k=0;k<bands;k++){
                const int v=(int)((slot*47u+k*19u)%113u)-56;
                left[(size_t)slot*bands+k]=(SautiAacSbrComplex){0.01f*v,-0.005f*v};
                right[(size_t)slot*bands+k]=(SautiAacSbrComplex){0.03f*v,0.007f*v};
            }
        lout[32u*64u]=(SautiAacSbrComplex){123.0f,456.0f};
        rout[32u*64u]=(SautiAacSbrComplex){789.0f,321.0f};
        assert(sauti_aac_ps_hybrid_synthesize_stereo(&s,left,32u*bands,
                           right,32u*bands,lout,32u*64u,rout,32u*64u));
        for(unsigned slot=0;slot<32u;slot++){
            unsigned cursor=0u;
            for(unsigned k=0;k<first;k++){
                double lr=0,li=0,rr=0,ri=0;
                for(unsigned j=0;j<splits[k];j++){
                    const size_t i=(size_t)slot*bands+cursor+j;
                    lr+=left[i].re;li+=left[i].im;
                    rr+=right[i].re;ri+=right[i].im;
                }
                const size_t q=(size_t)slot*64u+k;
                assert(almost(lout[q].re,(float)lr));
                assert(almost(lout[q].im,(float)li));
                assert(almost(rout[q].re,(float)rr));
                assert(almost(rout[q].im,(float)ri));
                cursor+=splits[k];
            }
            for(unsigned k=first;k<64u;k++){
                const size_t q=(size_t)slot*64u+k;
                const size_t h=(size_t)slot*bands+cursor+k-first;
                assert(lout[q].re==left[h].re&&lout[q].im==left[h].im);
                assert(rout[q].re==right[h].re&&rout[q].im==right[h].im);
            }
        }
        assert(lout[32u*64u].re==123.0f&&rout[32u*64u].im==321.0f);
        memcpy(saved_l,lout,sizeof(lout));memcpy(saved_r,rout,sizeof(rout));
        const float saved_last_right=right[31u*bands+bands-1u].re;
        right[31u*bands+bands-1u].re=NAN;
        assert(!sauti_aac_ps_hybrid_synthesize_stereo(&s,left,32u*bands,
                   right,32u*bands,lout,32u*64u,rout,32u*64u));
        assert(memcmp(saved_l,lout,sizeof(lout))==0);
        assert(memcmp(saved_r,rout,sizeof(rout))==0);
        right[31u*bands+bands-1u].re=saved_last_right;
        /* Overflow in the low-band sum must be rejected for BOTH outputs. */
        const float original_l0=left[0].re,original_l1=left[1].re;
        left[0].re=FLT_MAX;left[1].re=FLT_MAX;
        assert(!sauti_aac_ps_hybrid_synthesize_stereo(&s,left,32u*bands,
                   right,32u*bands,lout,32u*64u,rout,32u*64u));
        assert(memcmp(saved_l,lout,sizeof(lout))==0);
        assert(memcmp(saved_r,rout,sizeof(rout))==0);
        left[0].re=original_l0;left[1].re=original_l1;
        assert(!sauti_aac_ps_hybrid_synthesize_stereo(&s,left,32u*bands,
                   right,32u*bands,lout,32u*64u-1u,rout,32u*64u));
        assert(!sauti_aac_ps_hybrid_synthesize_stereo(&s,left,32u*bands,
                   right,32u*bands,lout,32u*64u,lout,32u*64u));
        assert(!sauti_aac_ps_hybrid_synthesize_stereo(&s,left,32u*bands,
                   right,32u*bands,lout,32u*64u,(SautiAacSbrComplex*)left,32u*64u));
        assert(memcmp(saved_l,lout,sizeof(lout))==0);
        assert(memcmp(saved_r,rout,sizeof(rout))==0);
        /* Single-channel API must use the same synthesis. */
        assert(sauti_aac_ps_hybrid_synthesize(&s,right,32u*bands,rout,32u*64u));
        for(unsigned t=0;t<32u;t++)
            for(unsigned b=0;b<64u;b++)
                assert(almost(rout[t*64u+b].re,saved_r[t*64u+b].re)&&
                       almost(rout[t*64u+b].im,saved_r[t*64u+b].im));
    }
    puts("PASS AAC PS-3b: 20/34 inverse hybrid stereo QMF, high-band identity, rollback");
    return 0;
}
