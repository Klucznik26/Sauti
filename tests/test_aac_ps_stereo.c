#include "sauti_aac_ps_stereo.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void prepare(SautiAacPsHybridSchedule *schedule,
                    SautiAacPsComplexMatrix *matrix,unsigned mode){
    SautiAacPsFrame frame={0};SautiAacPsMappedFrame mapped={0};
    frame.resolved_num_env=1u;frame.border_position[0]=31u;
    mapped.bands=mode;mapped.env_count=1u;
    assert(sauti_aac_ps_hybrid_schedule_build(&frame,&mapped,schedule));
    memset(matrix,0,sizeof(*matrix));
    matrix->bands=mode;matrix->env_count=1u;
    for(unsigned b=0;b<mode;b++){
        matrix->coefficient[0][b].h11.re=1.0f;
        matrix->coefficient[0][b].h22.re=1.0f;
    }
}
static int near_complex(SautiAacSbrComplex a,SautiAacSbrComplex b){
    return fabsf(a.re-b.re)<2e-5f&&fabsf(a.im-b.im)<2e-5f;
}
int main(void){
    SautiAacPsHybridSchedule schedule={0};
    SautiAacPsComplexMatrix matrix={0};
    SautiAacSbrComplex qmf[38u*64u],left[32u*91u+1u],right[32u*91u+1u];
    SautiAacSbrComplex low[32u*91u],diffuse[32u*91u];
    SautiAacPsStereoState state={0},independent={0};
    memset(left,0,sizeof(left));memset(right,0,sizeof(right));
    for(unsigned mode=20u;mode<=34u;mode+=14u){
        const unsigned bands=mode==20u?71u:91u;
        const size_t n=(size_t)32u*bands;
        prepare(&schedule,&matrix,mode);
        memset(&state,0,sizeof(state));memset(&independent,0,sizeof(independent));
        for(unsigned frame=0;frame<3u;frame++){
            for(unsigned s=0;s<38u;s++)for(unsigned b=0;b<64u;b++){
                const int z=(int)((frame*31u+s*29u+b*23u)%131u)-65;
                qmf[(size_t)s*64u+b]=(SautiAacSbrComplex){z*0.007f,z*-0.004f};
            }
            left[n]=(SautiAacSbrComplex){123.0f,234.0f};
            right[n]=(SautiAacSbrComplex){345.0f,456.0f};
            assert(sauti_aac_ps_qmf_bank_analyze(&schedule,qmf,38u*64u,
                                                  &independent.qmf_low,low,n));
            assert(sauti_aac_ps_decorrelator_apply(&schedule,low,n,
                                                    &independent.decorrelator,diffuse,n));
            assert(sauti_aac_ps_stereo_hybrid_apply(&schedule,&matrix,qmf,38u*64u,
                                                    &state,left,n,right,n));
            for(size_t i=0;i<n;i++){
                /* The matrix is interpolated from the previous frame's last envelope (zero before the first frame): frame 0 ramps
                 * (n+1)/32 over its single envelope, later frames repeat the same matrix and are flat. */
                const float ramp=frame==0u?(float)(i/bands+1u)/32.0f:1.0f;
                const SautiAacSbrComplex el={low[i].re*ramp,low[i].im*ramp};
                const SautiAacSbrComplex er={diffuse[i].re*ramp,diffuse[i].im*ramp};
                assert(near_complex(left[i],el));
                assert(near_complex(right[i],er));
            }
            assert(memcmp(&state.qmf_low,&independent.qmf_low,sizeof(state.qmf_low))==0);
            assert(memcmp(&state.decorrelator,&independent.decorrelator,sizeof(state.decorrelator))==0);
            assert(state.previous_bands==mode && state.previous[0].h11.re==1.0f && state.previous[mode-1u].h22.re==1.0f);
            assert(left[n].re==123.0f&&right[n].im==456.0f);
        }
        /* Failure during DSP processing must preserve BOTH outputs and state. */
        SautiAacPsStereoState old=state;
        SautiAacSbrComplex lsave[32u*91u],rsave[32u*91u];
        memcpy(lsave,left,sizeof(lsave));memcpy(rsave,right,sizeof(rsave));
        qmf[0].re=NAN;
        assert(!sauti_aac_ps_stereo_hybrid_apply(&schedule,&matrix,qmf,38u*64u,
                                                 &state,left,n,right,n));
        assert(memcmp(&old,&state,sizeof(state))==0);
        assert(memcmp(lsave,left,sizeof(lsave))==0);
        assert(memcmp(rsave,right,sizeof(rsave))==0);
        qmf[0].re=0.0f;
        /* Invalid matrix must be rejected before updating any state. */
        matrix.coefficient[0][0].h11.re=NAN;
        assert(!sauti_aac_ps_stereo_hybrid_apply(&schedule,&matrix,qmf,38u*64u,
                                                 &state,left,n,right,n));
        assert(memcmp(&old,&state,sizeof(state))==0);
        matrix.coefficient[0][0].h11.re=1.0f;
        assert(!sauti_aac_ps_stereo_hybrid_apply(&schedule,&matrix,qmf,38u*64u-1u,
                                                 &state,left,n,right,n));
        assert(!sauti_aac_ps_stereo_hybrid_apply(&schedule,&matrix,qmf,38u*64u,
                                                 &state,left,n-1u,right,n));
        assert(!sauti_aac_ps_stereo_hybrid_apply(&schedule,&matrix,qmf,38u*64u,
                                                 &state,left,n,left,n));
        assert(memcmp(&old,&state,sizeof(state))==0);
    }
    /* SBR-84: the mixing coefficients move linearly from the end of the previous envelope to the envelope's own value, reaching it in the
     * envelope's last slot.  Two envelopes (slots 0..7 and 8..31), constant inputs, one parameter band. */
    {
        SautiAacPsFrame fr={0};SautiAacPsMappedFrame mp={0};SautiAacPsHybridSchedule sch;SautiAacPsComplexMatrix mx;
        fr.resolved_num_env=2u;fr.border_position[0]=7u;fr.border_position[1]=31u;mp.bands=20u;mp.env_count=2u;
        assert(sauti_aac_ps_hybrid_schedule_build(&fr,&mp,&sch));
        memset(&mx,0,sizeof(mx));mx.bands=20u;mx.env_count=2u;
        SautiAacPsComplexMatrixCoefficients prev[34];memset(prev,0,sizeof(prev));
        for(unsigned b=0;b<20u;b++){
            prev[b].h11.re=1.0f;prev[b].h21.re=0.5f;prev[b].h11.im=0.25f;           /* end of the previous frame */
            mx.coefficient[0][b].h11.re=3.0f;mx.coefficient[0][b].h21.re=2.5f;mx.coefficient[0][b].h11.im=0.25f;
            mx.coefficient[1][b].h11.re=-1.0f;mx.coefficient[1][b].h21.re=0.0f;mx.coefficient[1][b].h11.im=-0.75f;
            mx.coefficient[0][b].h12.re=2.0f;mx.coefficient[1][b].h22.im=1.0f;
        }
        static SautiAacSbrComplex d[32u*71u],x[32u*71u],l[32u*71u],r[32u*71u];
        for(size_t i=0;i<32u*71u;i++){d[i]=(SautiAacSbrComplex){1.0f,0.0f};x[i]=(SautiAacSbrComplex){0.0f,2.0f};}
        assert(sauti_aac_ps_mix_interpolated_apply(&sch,&mx,prev,d,32u*71u,x,32u*71u,l,32u*71u,r,32u*71u));
        for(unsigned n=0;n<32u;n++){
            for(unsigned k=0;k<71u;k++){
                const unsigned par=sch.hybrid_to_parameter[k];(void)par;
                const double sign=sch.phase_imaginary_sign[k];
                const unsigned first=n<8u?0u:8u,len=n<8u?8u:24u;
                const double t=(double)(n-first+1u)/(double)len;
                const double h11r_a=n<8u?1.0:3.0,h11r_b=n<8u?3.0:-1.0;
                const double h11i_a=sign*(n<8u?0.25:0.25),h11i_b=sign*(n<8u?0.25:-0.75);
                const double h21r_a=n<8u?0.5:2.5,h21r_b=n<8u?2.5:0.0;
                const double h11r=h11r_a+t*(h11r_b-h11r_a),h11i=h11i_a+t*(h11i_b-h11i_a),h21r=h21r_a+t*(h21r_b-h21r_a);
                /* direct = 1, decorrelated = 2j:  L = h11*1 + h21*2j  (h21 imaginary part is 0) */
                const double lre=h11r,lim=h11i+2.0*h21r;
                assert(fabs((double)l[(size_t)n*71u+k].re-lre)<1e-5 && fabs((double)l[(size_t)n*71u+k].im-lim)<1e-5);
            }
        }
        assert(!sauti_aac_ps_mix_interpolated_apply(&sch,&mx,NULL,d,32u*71u,x,32u*71u,l,32u*71u,r,32u*71u));
        assert(!sauti_aac_ps_mix_interpolated_apply(&sch,&mx,prev,d,32u*71u,x,32u*71u,l,32u*71u,l,32u*71u));
    }
    puts("PASS AAC PS-3a: 20/34 QMF+decorrelator+complex mix, 3 frames, rollback; interpolated mix over two envelopes");
    return 0;
}
