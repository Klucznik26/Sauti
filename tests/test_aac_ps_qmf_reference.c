#include "sauti_aac_ps_qmf_low.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* An independently structured, offline reference oracle. Prototype coefficients
 * are the ISO PS values also published by FFmpeg n6.1 aacps_tablegen.h.
 * This oracle uses the conjugate-pair formula from the FFmpeg reference,
 * rather than the 13-tap direct convolution in Sauti's production code.
 * License/provenance review remains mandatory before distributing derived data. */
static const double p8[7]={.00746082949812,.02270420949825,.04546865930473,
 .07266113929591,.09885108575264,.11793710567217,.125};
static const double p12[7]={.04081179924692,.03812810994926,.05144908135699,
 .06399831151592,.07428313801106,.08100347892914,.08333333333333};
static const double p81[7]={.01565675600122,.03752716391991,.05417891378782,
 .08417044116767,.10307344158036,.12222452249753,.125};
static const double p4[7]={-.05908211155639,-.04871498374946,0,
 .07778723915851,.16486303567403,.23279856662996,.25};
static const double p2[7]={0,.01899487526049,0,-.07293139167538,0,.30596630545168,.5};
static const double pi=3.14159265358979323846264338327950288;

static SautiAacSbrComplex at(const SautiAacSbrComplex *qmf,
                              const SautiAacPsQmfLowState *old,int t,unsigned band){
    if(t<0)return old->history[band][t+6];
    return qmf[t*64u+band];
}
static void oracle_complex(const SautiAacSbrComplex *qmf,
                           const SautiAacPsQmfLowState *old,
                           unsigned slot,unsigned band,unsigned n,
                           const double p[7],SautiAacSbrComplex *out){
    for(unsigned k=0;k<n;k++){
        const SautiAacSbrComplex center=at(qmf,old,(int)slot,band);
        double re=p[6]*center.re,im=p[6]*center.im;
        for(unsigned j=0;j<6u;j++){
            const SautiAacSbrComplex a=at(qmf,old,(int)slot+(int)j-6,band);
            const SautiAacSbrComplex b=at(qmf,old,(int)slot+6-(int)j,band);
            const double theta=(2*pi*(k+.5)*((int)j-6))/(double)n;
            const double cr=p[j]*cos(theta),ci=-p[j]*sin(theta);
            const double r0=a.re+b.re,r1=a.im-b.im;
            const double i0=a.im+b.im,i1=a.re-b.re;
            re+=cr*r0-ci*r1;
            im+=cr*i0+ci*i1;
        }
        out[k]=(SautiAacSbrComplex){(float)re,(float)im};
    }
}
static void oracle_two(const SautiAacSbrComplex *qmf,
                       const SautiAacPsQmfLowState *old,
                       unsigned slot,unsigned band,int reverse,
                       SautiAacSbrComplex *dst){
    const SautiAacSbrComplex x=at(qmf,old,(int)slot,band);
    double re=p2[6]*x.re,im=p2[6]*x.im,op_re=0,op_im=0;
    for(unsigned j=0;j<6u;j+=2u){
        const SautiAacSbrComplex a=at(qmf,old,(int)slot+(int)j-5,band);
        const SautiAacSbrComplex b=at(qmf,old,(int)slot+5-(int)j,band);
        op_re+=p2[j+1u]*(a.re+b.re);
        op_im+=p2[j+1u]*(a.im+b.im);
    }
    dst[reverse]=(SautiAacSbrComplex){(float)(re+op_re),(float)(im+op_im)};
    dst[!reverse]=(SautiAacSbrComplex){(float)(re-op_re),(float)(im-op_im)};
}
static void compare(const SautiAacSbrComplex a,const SautiAacSbrComplex b){
    assert(fabsf(a.re-b.re)<2e-5f);
    assert(fabsf(a.im-b.im)<2e-5f);
}
int main(void){
    SautiAacSbrComplex qmf[38u*64u],out[32u*32u];
    SautiAacPsQmfLowState state={0};
    SautiAacPsHybridSchedule schedule={0};
    for(unsigned mode=20u;mode<=34u;mode+=14u){
        const unsigned low_bands=mode==20u?10u:32u;
        SautiAacPsFrame frame={0};SautiAacPsMappedFrame mapped={0};
        frame.resolved_num_env=1u;frame.border_position[0]=31u;
        mapped.bands=mode;mapped.env_count=1u;
        assert(sauti_aac_ps_hybrid_schedule_build(&frame,&mapped,&schedule));
        memset(&state,0,sizeof(state));
        for(unsigned pass=0;pass<2u;pass++){
            for(unsigned t=0;t<38u;t++)for(unsigned b=0;b<64u;b++){
                const unsigned n=(pass*599u+t*61u+b*97u)%379u;
                qmf[t*64u+b]=(SautiAacSbrComplex){(float)((int)n-175)*.004f,
                                                      (float)((int)n-195)*-.003f};
            }
            const SautiAacPsQmfLowState old=state;
            assert(sauti_aac_ps_qmf_low_analyze(&schedule,qmf,38u*64u,&state,
                                                   out,32u*low_bands));
            for(unsigned t=0;t<32u;t++){
                SautiAacSbrComplex expected[32];
                if(mode==20u){
                    SautiAacSbrComplex tmp[8];
                    oracle_complex(qmf,&old,t,0u,8u,p8,tmp);
                    expected[0]=tmp[6];expected[1]=tmp[7];
                    expected[2]=tmp[0];expected[3]=tmp[1];
                    for(unsigned j=0;j<2u;j++){
                        expected[4u+j].re=tmp[2u+j].re+tmp[5u-j].re;
                        expected[4u+j].im=tmp[2u+j].im+tmp[5u-j].im;
                    }
                    oracle_two(qmf,&old,t,1u,1,expected+6u);
                    oracle_two(qmf,&old,t,2u,0,expected+8u);
                }else{
                    oracle_complex(qmf,&old,t,0u,12u,p12,expected);
                    oracle_complex(qmf,&old,t,1u,8u,p81,expected+12u);
                    for(unsigned b=2u;b<5u;b++)
                        oracle_complex(qmf,&old,t,b,4u,p4,expected+20u+4u*(b-2u));
                }
                for(unsigned k=0;k<low_bands;k++)compare(out[t*low_bands+k],expected[k]);
            }
        }
    }
    puts("PASS AAC PS reference: independent conjugate-pair FIR oracle, 20/34, two frames");
    return 0;
}
