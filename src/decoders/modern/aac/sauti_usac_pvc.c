/* PVC predictor (stage P1 of docs/USAC_PVC_PLAN.md): low band energy grouping, time smoothing, prediction of the high band envelope.  The coefficient tables are the normative data of
 * ISO/IEC 23003-3 (sauti_usac_pvc_tables.c, generated from libxaac); the procedure is written from the clause and checked against libxaac with the dumps of tools/. */
#include "sauti_usac_pvc.h"
#include "sauti_usac_pvc_tables.h"
#include <math.h>
#include <string.h>

#define NB_LOW 3                                 /* groups of low band subbands the prediction looks at */
#define ESG_MIN 0.1f                             /* floor of a group energy (linear) and its decibel value */
#define ESG_MIN_DB -10.0f

typedef struct {
    unsigned nb_high;                            /* high band groups: 8 (mode 1), 6 (mode 2) */
    unsigned per_group;                          /* subbands per group before the last two: 8 / rate or 12 / rate */
    unsigned smooth_len;
    const float *smooth;
    const uint8_t *boundary;
    const float *qfac;
} Mode;

static int mode_setup(unsigned mode,unsigned ns_mode,unsigned rate,Mode *m){
    if(rate!=2u && rate!=4u)return 0;
    if(mode==1u){
        m->nb_high=8u;m->per_group=8u/rate;
        m->boundary=sauti_usac_pvc_id_boundary_m1;m->qfac=sauti_usac_pvc_qfac_m1;
        m->smooth_len=ns_mode?4u:16u;m->smooth=ns_mode?sauti_usac_pvc_smooth_ns4:sauti_usac_pvc_smooth_ns16;
    }else if(mode==2u){
        m->nb_high=6u;m->per_group=12u/rate;
        m->boundary=sauti_usac_pvc_id_boundary_m2;m->qfac=sauti_usac_pvc_qfac_m2;
        m->smooth_len=ns_mode?3u:12u;m->smooth=ns_mode?sauti_usac_pvc_smooth_ns3:sauti_usac_pvc_smooth_ns12;
    }else return 0;
    return 1;
}

int sauti_usac_pvc_predict(SautiUsacPvcState *st,const SautiUsacPvcFrame *f,unsigned rate,unsigned first_band,const float low_energy[16][32],float out[16][64]){
    Mode m;
    if(!st || !f || !low_energy || !out || !mode_setup(f->mode,f->ns_mode,rate,&m) || first_band>=64u || f->first_slot>=SAUTI_USAC_PVC_SLOTS)return 0;
    for(unsigned t=0u;t<SAUTI_USAC_PVC_SLOTS;t++)if(f->id[t]>127u)return 0;
    SautiUsacPvcState next=*st;
    const int lbw=(int)(8u/rate);                /* subbands per low band group */
    /* 1. energy of the three groups just below the crossover, per time slot, in dB; below subband 0 the floor */
    for(unsigned t=0u;t<SAUTI_USAC_PVC_SLOTS;t++)
        for(int g=0;g<NB_LOW;g++){
            const int a=(int)first_band-lbw*NB_LOW+lbw*g;
            float e=ESG_MIN;
            if(a>=0){
                e=0.0f;
                for(int b=a;b<a+lbw;b++)e+=low_energy[t][b];
                e/=(float)lbw;
            }
            next.esg[t+SAUTI_USAC_PVC_SLOTS-1u][g]=e>ESG_MIN?10.0f*((float)log10((double)e)):ESG_MIN_DB;
        }
    /* without a usable history (first PVC frame, or the crossover moved) the missing past is the first slot that is processed */
    if(!st->prev_flag || (int32_t)(first_band*rate)!=st->prev_first_band*(int32_t)st->prev_rate)
        for(unsigned t=0u;t<SAUTI_USAC_PVC_SLOTS-1u+f->first_slot;t++)
            memcpy(next.esg[t],next.esg[SAUTI_USAC_PVC_SLOTS-1u+f->first_slot],sizeof next.esg[t]);
    /* 2. smoothing over the last smooth_len slots, 3. prediction, 4. back to linear energy per subband */
    for(unsigned t=0u;t<SAUTI_USAC_PVC_SLOTS;t++){
        float sm[NB_LOW]={0.0f,0.0f,0.0f};
        for(unsigned j=0u;j<m.smooth_len;j++)
            for(int g=0;g<NB_LOW;g++)sm[g]+=next.esg[t+SAUTI_USAC_PVC_SLOTS-1u-j][g]*m.smooth[j];
        const unsigned id=f->id[t];
        const unsigned grp=id<m.boundary[0]?0u:id<m.boundary[1]?1u:2u;
        float db[8];
        for(unsigned h=0u;h<m.nb_high;h++){
            const int coef2=f->mode==1u?sauti_usac_pvc_coef2_m1[id][h]:sauti_usac_pvc_coef2_m2[id][h];
            db[h]=(float)coef2*m.qfac[NB_LOW];
        }
        for(int g=0;g<NB_LOW;g++)
            for(unsigned h=0u;h<m.nb_high;h++){
                const int coef1=f->mode==1u?sauti_usac_pvc_coef1_m1[grp][g][h]:sauti_usac_pvc_coef1_m2[grp][g][h];
                db[h]+=(float)coef1*m.qfac[g]*sm[g];
            }
        unsigned a=first_band,z=a+m.per_group-1u;
        for(unsigned h=0u;h<m.nb_high;h++){
            const float lin=(float)pow(10.0,(double)db[h]/10.0);
            for(unsigned k=a;k<=z && k<64u;k++)out[t][k]=lin;
            a+=m.per_group;
            if(h+2u>=m.nb_high)z=63u;
            else{z=a+m.per_group-1u;if(z>=63u)z=63u;}
        }
    }
    memmove(next.esg[0],next.esg[SAUTI_USAC_PVC_SLOTS],sizeof(float)*3u*(SAUTI_USAC_PVC_SLOTS-1u));
    next.prev_flag=1u;next.prev_rate=rate;next.prev_first_band=(int32_t)first_band;
    *st=next;
    return 1;
}
