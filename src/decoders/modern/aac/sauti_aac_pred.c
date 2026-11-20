/* AAC Main prediction, see sauti_aac_pred.h.  Written from the algorithm of ISO/IEC 14496-3 4.6.7 (backward-adaptive lattice LMS, a = 61/64, alpha = 29/32). */
#include "sauti_aac_pred.h"
#include "sauti_aac_bands.h"
#include "sauti_aac_layout.h"
#include <string.h>

#define PRED_A 0.953125f      /* 61 / 64 */
#define PRED_ALPHA 0.90625f   /* 29 / 32 */

static uint32_t bits_of(float f){uint32_t u;memcpy(&u,&f,sizeof u);return u;}
static float float_of(uint32_t u){float f;memcpy(&f,&u,sizeof f);return f;}
/* the 16-bit storage of the standard: keep the sign, exponent and the 7 top mantissa bits */
static float trunc16(float f){return float_of(bits_of(f)&0xffff0000u);}
static float round16(float f){return float_of((bits_of(f)+0x00008000u)&0xffff0000u);}                           /* the prediction: to nearest */
static float even16(float f){const uint32_t u=bits_of(f);return float_of((u+0x00007fffu+((u>>16)&1u))&0xffff0000u);}   /* the gain a / var: to nearest even */

void sauti_aac_pred_reset(SautiAacPredState *state){
    for(unsigned k=0;k<1024u;k++){
        SautiAacPredLine *l=&state->line[k];
        l->r0=l->r1=0.0f;l->cor0=l->cor1=0.0f;l->var0=l->var1=1.0f;
    }
}
static void predict(SautiAacPredLine *l,float *coef,int use){
    const float k1=l->var0>1.0f?l->cor0*even16(PRED_A/l->var0):0.0f;
    const float k2=l->var1>1.0f?l->cor1*even16(PRED_A/l->var1):0.0f;
    const float pv=round16(k1*l->r0+k2*l->r1);
    if(use)*coef+=pv;
    const float e0=*coef;                          /* the reconstructed value of this line */
    const float e1=e0-k1*l->r0;
    l->cor1=trunc16(PRED_ALPHA*l->cor1+l->r1*e1);
    l->var1=trunc16(PRED_ALPHA*l->var1+0.5f*(l->r1*l->r1+e1*e1));
    l->cor0=trunc16(PRED_ALPHA*l->cor0+l->r0*e0);
    l->var0=trunc16(PRED_ALPHA*l->var0+0.5f*(l->r0*l->r0+e0*e0));
    l->r1=trunc16(PRED_A*(l->r0-k1*e0));
    l->r0=trunc16(PRED_A*e0);
}
int sauti_aac_pred_apply(SautiAacPredState *state,const SautiAacIcsInfo *ics,unsigned idx,float spectral[1024]){
    return sauti_aac_pred_apply_pns(state,ics,idx,spectral,NULL);
}
static void reset_line(SautiAacPredLine *l){l->r0=l->r1=0.0f;l->cor0=l->cor1=0.0f;l->var0=l->var1=1.0f;}
int sauti_aac_pred_apply_pns(SautiAacPredState *state,const SautiAacIcsInfo *ics,unsigned idx,float spectral[1024],const uint8_t *noise_band){
    const uint16_t *offsets=NULL;unsigned bands=0;
    if(!state||!ics||!spectral||ics->window_sequence>3u||
       !sauti_aac_band_offsets(idx,0u,&offsets,&bands))return 0;
    if(ics->window_sequence==2u){sauti_aac_pred_reset(state);return 1;}
    const unsigned limit=sauti_aac_pred_sfb_max(SAUTI_AAC_IDX_RATE(idx));
    if(!limit)return 0;
    const unsigned n=limit<bands?limit:bands;
    for(unsigned sfb=0;sfb<n;sfb++){
        if(noise_band && sfb<ics->max_sfb && noise_band[sfb]){                  /* PNS band: no prediction is added and the predictors start over (as in FAAD2) */
            for(unsigned k=offsets[sfb];k<offsets[sfb+1u];k++)reset_line(&state->line[k]);
            continue;
        }
        const int use=ics->predictor_present && sfb<ics->max_sfb && ics->prediction_used[sfb];
        for(unsigned k=offsets[sfb];k<offsets[sfb+1u];k++)predict(&state->line[k],&spectral[k],use);
    }
    if(ics->predictor_present && ics->predictor_reset && ics->predictor_reset_group){
        for(unsigned k=ics->predictor_reset_group-1u;k<1024u;k+=30u){
            SautiAacPredLine *l=&state->line[k];
            l->r0=l->r1=0.0f;l->cor0=l->cor1=0.0f;l->var0=l->var1=1.0f;
        }
    }
    return 1;
}
