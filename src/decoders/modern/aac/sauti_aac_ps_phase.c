#include "sauti_aac_ps_phase.h"

#include <math.h>

static const float phase_cos[8]={1.0f,0.7071067811865475f,0.0f,-0.7071067811865475f,
                                 -1.0f,-0.7071067811865475f,0.0f,0.7071067811865475f};
static const float phase_sin[8]={0.0f,0.7071067811865475f,1.0f,0.7071067811865475f,
                                 0.0f,-0.7071067811865475f,-1.0f,-0.7071067811865475f};

static int smooth(uint8_t old,uint8_t now,float *re,float *im){
    if(old>63u||now>7u||!re||!im)return 0;
    const unsigned first=old>>3u,second=old&7u;
    float r=0.25f*phase_cos[first]+0.5f*phase_cos[second]+phase_cos[now];
    float i=0.25f*phase_sin[first]+0.5f*phase_sin[second]+phase_sin[now];
    const float magnitude=hypotf(r,i);
    if(!(magnitude>0.0f)||!isfinite(magnitude))return 0;
    *re=r/magnitude;*im=i/magnitude;
    return 1;
}
static SautiAacPsComplexCoefficient multiply(float value,float re,float im){
    SautiAacPsComplexCoefficient out={value*re,value*im};return out;
}

int sauti_aac_ps_phase_apply(const SautiAacPsFrame *frame,
                             const SautiAacPsMappedFrame *mapped,
                             const SautiAacPsMatrix *matrix,
                             SautiAacPsPhaseState *state,
                             SautiAacPsComplexMatrix *out){
    if(!frame||!mapped||!matrix||!state||!out||!matrix->env_count||
       matrix->env_count!=mapped->env_count||matrix->bands!=mapped->bands||
       (mapped->bands!=20u&&mapped->bands!=34u)||
       mapped->env_count!=frame->resolved_num_env||state->ready>1u)return 0;
    SautiAacPsPhaseState next_state=*state;
    if(next_state.ready&&next_state.bands!=mapped->bands){
        next_state=(SautiAacPsPhaseState){0};
    }
    SautiAacPsComplexMatrix next={0};
    next.bands=mapped->bands;next.env_count=mapped->env_count;
    next.has_phase=frame->enable_ipdopd;
    const unsigned phase_bands=mapped->bands==34u?17u:11u;
    for(unsigned e=0u;e<next.env_count;e++){
        for(unsigned b=0u;b<next.bands;b++){
            const SautiAacPsMatrixCoefficients real=matrix->coefficient[e][b];
            float opd_re=1.0f,opd_im=0.0f,ipd_re=1.0f,ipd_im=0.0f;
            if(next.has_phase&&b<phase_bands){
                const int ipd=mapped->ipd[e][b],opd=mapped->opd[e][b];
                if(ipd<0||ipd>7||opd<0||opd>7||
                   !smooth(next_state.ipd_history[b],(uint8_t)ipd,&ipd_re,&ipd_im)||
                   !smooth(next_state.opd_history[b],(uint8_t)opd,&opd_re,&opd_im))return 0;
                next_state.ipd_history[b]=(uint8_t)((next_state.ipd_history[b]&7u)<<3u|(unsigned)ipd);
                next_state.opd_history[b]=(uint8_t)((next_state.opd_history[b]&7u)<<3u|(unsigned)opd);
            }
            const float adjacent_re=opd_re*ipd_re+opd_im*ipd_im;
            const float adjacent_im=opd_im*ipd_re-opd_re*ipd_im;
            next.coefficient[e][b].h11=multiply(real.h11,opd_re,opd_im);
            next.coefficient[e][b].h12=multiply(real.h12,adjacent_re,adjacent_im);
            next.coefficient[e][b].h21=multiply(real.h21,opd_re,opd_im);
            next.coefficient[e][b].h22=multiply(real.h22,adjacent_re,adjacent_im);
        }
    }
    next_state.ready=1u;next_state.bands=mapped->bands;
    *state=next_state;*out=next;
    return 1;
}
