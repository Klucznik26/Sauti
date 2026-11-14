#include "sauti_aac_ps_decorrelate.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

/* The PS phase centre coordinates and per-link fractional delays
 * follow ISO/IEC 14496-3 PS; audited against FFmpeg n6.1 tablegen. */
static const int fc20[10]={-3,-1,1,3,5,7,10,14,18,22};
static const int fc34[32]={
    2,6,10,14,18,22,26,30,34,-10,-6,-2,51,57,15,21,
    27,33,39,45,54,66,78,42,102,66,78,90,102,114,126,90
};
static const double link_delay[3]={0.43,0.75,0.347};
static const double link_gain[3]={0.65143905753106,0.56471812200776,0.48954165955695};
static const double pi=3.14159265358979323846264338327950288;

static int apart(const void *a,size_t as,const void *b,size_t bs){
    const uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<=y?y-x>=as:x-y>=bs;
}
static SautiAacSbrComplex rotate(SautiAacSbrComplex x,double angle){
    const double c=cos(angle),s=sin(angle);
    return (SautiAacSbrComplex){
        (float)((double)x.re*c-(double)x.im*s),
        (float)((double)x.re*s+(double)x.im*c)
    };
}
static int valid(SautiAacSbrComplex x){
    return isfinite(x.re)&&isfinite(x.im);
}
int sauti_aac_ps_decorrelate(const SautiAacPsHybridSchedule *schedule,
                             const SautiAacSbrComplex *direct,size_t direct_count,
                             SautiAacPsDecorrelateState *state,
                             SautiAacSbrComplex *out,size_t out_count){
    if(!schedule||!direct||!state||!out)return 0;
    const unsigned bands=schedule->hybrid_bands;
    const unsigned is34=schedule->parameter_bands==34u&&bands==91u;
    if(!((schedule->parameter_bands==20u&&bands==71u)||is34)||
       direct_count<(size_t)32u*bands||out_count<(size_t)32u*bands||
       (state->bands!=0u&&state->bands!=71u&&state->bands!=91u)||
       !schedule->env_count||schedule->env_count>SAUTI_AAC_PS_MAX_ENVELOPES)
        return 0;
    if(!apart(direct,(size_t)32u*bands*sizeof(*direct),
              out,(size_t)32u*bands*sizeof(*out))||
       !apart(state,sizeof(*state),direct,(size_t)32u*bands*sizeof(*direct))||
       !apart(state,sizeof(*state),out,(size_t)32u*bands*sizeof(*out)))
        return 0;
    for(unsigned k=0;k<bands;k++)
        if(schedule->hybrid_to_parameter[k]>=schedule->parameter_bands)return 0;
    for(unsigned n=0;n<32u;n++)
        if(schedule->slot_to_env[n]>=schedule->env_count)return 0;

    SautiAacPsDecorrelateState next=*state;
    if(next.bands!=bands)memset(&next,0,sizeof(next));
    next.bands=bands;
    const unsigned ap_count=is34?50u:30u;
    const unsigned short_start=is34?62u:42u;
    const unsigned cutoff=is34?32u:10u;
    for(unsigned k=0;k<bands;k++){
        for(unsigned h=0;h<14u;h++)
            if(!valid(next.input_history[k][h]))return 0;
        if(k<ap_count)
            for(unsigned m=0;m<3u;m++)
                for(unsigned h=0;h<5u;h++)
                    if(!valid(next.allpass_history[k][m][h]))return 0;
    }
    SautiAacSbrComplex tmp[32u*91u];
    for(unsigned k=0;k<bands;k++){
        const double fcenter=is34?(k<32u?fc34[k]/24.0:(double)k-26.5):
                                 (k<10u?fc20[k]/8.0:(double)k-6.5);
        double decay=1.0-0.05*((double)k-(double)cutoff);
        if(decay<0.0)decay=0.0;
        if(decay>1.0)decay=1.0;
        for(unsigned n=0;n<32u;n++){
            const SautiAacSbrComplex x=direct[(size_t)n*bands+k];
            if(!valid(x))return 0;
            SautiAacSbrComplex y;
            if(k<ap_count){
                /* 2-slot entrance delay, then all-pass link delays 3/4/5. */
                y=rotate(next.input_history[k][12],-pi*0.39*fcenter);
                for(unsigned m=0;m<3u;m++){
                    const double g=link_gain[m]*decay;
                    const SautiAacSbrComplex old=next.allpass_history[k][m][2u-m];
                    const SautiAacSbrComplex delayed=rotate(old,-pi*link_delay[m]*fcenter);
                    const SautiAacSbrComplex before=y;
                    y=(SautiAacSbrComplex){(float)((double)delayed.re-g*before.re),
                                           (float)((double)delayed.im-g*before.im)};
                    const SautiAacSbrComplex written=
                        (SautiAacSbrComplex){(float)((double)before.re+g*y.re),
                                             (float)((double)before.im+g*y.im)};
                    if(!valid(y)||!valid(written))return 0;
                    memmove(next.allpass_history[k][m],
                            next.allpass_history[k][m]+1u,4u*sizeof(*next.allpass_history[k][m]));
                    next.allpass_history[k][m][4]=written;
                }
            }else if(k<short_start)y=next.input_history[k][0];
            else y=next.input_history[k][13];
            if(!valid(y))return 0;
            tmp[(size_t)n*bands+k]=y;
            memmove(next.input_history[k],next.input_history[k]+1u,
                    13u*sizeof(*next.input_history[k]));
            next.input_history[k][13]=x;
        }
    }
    memcpy(out,tmp,(size_t)32u*bands*sizeof(*out));
    *state=next;
    return 1;
}
