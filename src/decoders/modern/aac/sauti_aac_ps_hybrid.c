#include "sauti_aac_ps_hybrid.h"

#include <string.h>

/* ISO/IEC 14496-3 PS Tables 8.48 and 8.49.  These are routing indices, not
 * signal coefficients, therefore they stay scalar and in read-only data. */
static const uint8_t k_to_i_20[] = {
    1,0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,14,15,15,15,16,16,16,16,17,
    17,17,17,17,18,18,18,18,18,18,18,18,18,18,18,18,19,19,19,19,19,19,
    19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19
};
static const uint8_t k_to_i_34[] = {
    0,1,2,3,4,5,6,6,7,2,1,0,10,10,4,5,6,7,8,9,10,11,12,9,14,11,12,13,
    14,15,16,13,16,17,18,19,20,21,22,22,23,23,24,24,25,25,26,26,27,27,
    27,28,28,28,29,29,29,30,30,30,31,31,31,31,32,32,32,32,33,33,33,33,
    33,33,33,33,33,33,33,33,33,33,33,33,33,33,33,33,33,33,33
};
_Static_assert(sizeof(k_to_i_20)==71u,"PS 20-band routing must have 71 entries");
_Static_assert(sizeof(k_to_i_34)==91u,"PS 34-band routing must have 91 entries");

int sauti_aac_ps_hybrid_schedule_build(const SautiAacPsFrame *frame,
                                       const SautiAacPsMappedFrame *mapped,
                                       SautiAacPsHybridSchedule *out){
    if(!frame||!mapped||!out||!frame->resolved_num_env||
       frame->resolved_num_env>SAUTI_AAC_PS_MAX_ENVELOPES||
       mapped->env_count!=frame->resolved_num_env||
       (mapped->bands!=20u&&mapped->bands!=34u))return 0;
    SautiAacPsHybridSchedule next={0};
    next.parameter_bands=mapped->bands;
    next.hybrid_bands=mapped->bands==34u?91u:71u;
    next.env_count=frame->resolved_num_env;
    const uint8_t *routing=mapped->bands==34u?k_to_i_34:k_to_i_20;
    memcpy(next.hybrid_to_parameter,routing,next.hybrid_bands);
    for(unsigned k=0u;k<next.hybrid_bands;k++){
        next.phase_imaginary_sign[k]=1;
        if((mapped->bands==20u&&k<=1u)||
           (mapped->bands==34u&&k>=9u&&k<=13u))next.phase_imaginary_sign[k]=-1;
    }
    unsigned start=0u;
    for(unsigned e=0u;e<next.env_count;e++){
        const unsigned stop=frame->border_position[e];
        if(stop<start||stop>=SAUTI_AAC_PS_QMF_SLOTS)return 0;
        for(unsigned slot=start;slot<=stop;slot++)next.slot_to_env[slot]=(uint8_t)e;
        start=stop+1u;
    }
    if(start!=SAUTI_AAC_PS_QMF_SLOTS)return 0;
    *out=next;
    return 1;
}
