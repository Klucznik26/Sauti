/* SBR-82: one SBR channel in the QMF domain, laid out as the standard describes it (see the header for the data flow). */
#include "sauti_aac_sbr_channel.h"
#include "sauti_aac_sbr_gain_limit_partition.h"
#include "sauti_aac_sbr_limiter_bands.h"
#include "sauti_aac_sbr_noise_rom.h"
#include "sauti_aac_sbr_lpc_estimate.h"
#include <math.h>
#include <string.h>
#ifdef SAUTI_SBR_CHANNEL_TRACE
#include <stdio.h>
#include <stdlib.h>
static unsigned long long g_trace_frame;
#endif

#define SLOTS_MAX (2u*SAUTI_AAC_SBR_CHANNEL_SLOTS)     /* 4:1: 64 slots */
#define BANDS SAUTI_AAC_SBR_CHANNEL_BANDS
#define MAX_I 76u            /* an envelope never ends after output slot 38 (2:1) / 76 (4:1) */

/* Smoothing filter of the standard (bs_smoothing_mode == 0): five taps over the current and the four preceding slots. */
static const double h_smooth[5]={
    0.33333333333333,0.30150283239582,0.21816949906249,0.11516383427084,0.03183050093751
};

int sauti_aac_sbr_channel_init(SautiAacSbrChannel *ch){
    if(!ch)return 0;
    memset(ch,0,sizeof(*ch));
    ch->ready=1u;
    return 1;
}

static int finite_c(SautiAacSbrComplex x){return isfinite(x.re) && isfinite(x.im);}

/* One envelope: E, Q, the sine map and the carrier power of its own slots go through the limiter. Gain, noise and sine amplitude
 * (all in the 2^15-scaled units of the reference decoders, so the last two are brought back by 2^-15 where they are added). */
typedef struct {
    double gain[64],noise[64],sine[64];
    unsigned delta;
} EnvAdjust;

static int adjust_envelope(const SautiAacSbrPayloadFrame *f,
                           const SautiAacSbrPayloadFrame *before,
                           const SautiAacSbrLimiterBands *bands,
                           const SautiAacSbrComplex *xh,unsigned env,
                           const uint8_t *flag_source,   /* sine flags this envelope works with, per bin */
                           EnvAdjust *out){
    const SautiAacSbrTables *tab=&f->tables;
    const unsigned kx=tab->kx,k2=kx+tab->m;
    const unsigned lo=f->env_edges[env],hi=f->env_edges[env+1u];
    unsigned noise_idx=f->grid.noise_count;
    for(unsigned j=0u;j<f->grid.noise_count;j++)
        if(lo>=f->noise_edges[j] && hi<=f->noise_edges[j+1u]){noise_idx=j;break;}
    if(noise_idx>=f->grid.noise_count)return 0;
    double e[64]={0},q[64]={0},curr[64]={0};
    uint8_t sine_index[64]={0},sine_mapped[64]={0};
    const unsigned fine=f->energy.freq_res[env];
    const uint8_t *freq=fine?tab->high:tab->low;
    const unsigned n=fine?tab->n_high:tab->n_low;
    for(unsigned b=0u;b<n;b++){
        if(freq[b]<kx || freq[b+1u]>k2 || freq[b]>=freq[b+1u])return 0;
        const double v=f->energy.env_energy[env][b];
        if(!isfinite(v) || v<0.0)return 0;
        for(unsigned k=freq[b];k<freq[b+1u];k++)e[k]=v;
    }
    for(unsigned b=0u;b<tab->n_noise;b++){
        if(tab->noise[b]<kx || tab->noise[b+1u]>k2 || tab->noise[b]>=tab->noise[b+1u])return 0;
        const double v=f->energy.noise_factor[noise_idx][b];
        if(!isfinite(v) || v<0.0)return 0;
        for(unsigned k=tab->noise[b];k<tab->noise[b+1u];k++)q[k]=v;
    }
    for(unsigned k=kx;k<k2;k++)sine_index[k]=flag_source[k]?1u:0u;
    for(unsigned b=0u;b<n;b++){                      /* the sine of a band is mapped across the envelope-resolution band */
        unsigned present=0u;
        for(unsigned k=freq[b];k<freq[b+1u];k++)if(sine_index[k])present=1u;
        if(present)for(unsigned k=freq[b];k<freq[b+1u];k++)sine_mapped[k]=1u;
    }
    /* mean carrier power over the envelope's own slots; the limiter works in the reference units (2^30 on a power) */
    for(unsigned k=kx;k<k2;k++){
        double sum=0.0;
        for(unsigned i=lo;i<hi;i++){
            const SautiAacSbrComplex z=xh[(size_t)i*64u+k];
            sum+=(double)z.re*z.re+(double)z.im*z.im;
        }
        curr[k]=ldexp(sum/(double)(hi-lo),30);
        if(!isfinite(curr[k]))return 0;
    }
    if(!f->header.interpol_freq){                    /* bs_interpol_freq = 0: one measured power per envelope FREQUENCY band (the mean over its subbands), not per subband */
        for(unsigned b=0u;b<n;b++){
            double band_sum=0.0;
            for(unsigned k=freq[b];k<freq[b+1u];k++)band_sum+=curr[k];
            band_sum/=(double)(freq[b+1u]-freq[b]);
            for(unsigned k=freq[b];k<freq[b+1u];k++)curr[k]=band_sum;
        }
    }
    const unsigned delta=sauti_aac_sbr_env_delta(f,env,before);
    SautiAacSbrGainLimitedFrame limited;
    if(!sauti_aac_sbr_gain_limit_partition_flags(e,curr,q,sine_index,sine_mapped,
                                                 bands->edges,bands->count,f->gain_mode,delta,&limited))return 0;
#ifdef SAUTI_SBR_CHANNEL_TRACE   /* debugging aid, compiled in only for the comparison tools: frame,env,delta,k,gain,noise,sine,ecurr,E,Q */
    {
        static FILE *tr;
        const char *path=getenv("SAUTI_SBR82_ENV");
        if(path && !tr)tr=fopen(path,"wb");
        if(tr)for(unsigned k=kx;k<k2;k++)
            fprintf(tr,"%llu,%u,%u,%u,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n",(unsigned long long)g_trace_frame,env,delta,k,
                    limited.band[k].hf_gain,limited.band[k].noise_amplitude,limited.band[k].sine_amplitude,curr[k],e[k],q[k]);
        if(tr)fflush(tr);
    }
#endif
    memset(out,0,sizeof(*out));
    out->delta=delta;
    for(unsigned k=kx;k<k2;k++){
        out->gain[k]=limited.band[k].hf_gain;
        out->noise[k]=limited.band[k].noise_amplitude;
        out->sine[k]=limited.band[k].sine_amplitude;
        if(!isfinite(out->gain[k]) || !isfinite(out->noise[k]) || !isfinite(out->sine[k]) ||
           out->gain[k]<0.0 || out->noise[k]<0.0 || out->sine[k]<0.0)return 0;
    }
    return 1;
}

/* One time slot of a PVC frame: the envelope energies come from the predictor (per subband), the noise floor of the slot from the grid, the sines per the flagged bands.  The same limiter as above.
 * A slot inside the first sin_len_cur slots of the frame belongs to a sine that began in the previous frame's variable length part: which bins carry the tone follows the previous frame's flags
 * (they decide the band flags, and the tone amplitude is scaled with the previous frame's last noise floor), the current flags only add tones inside the flagged bands. */
static int adjust_pvc_slot(const SautiAacSbrPayloadFrame *f,const SautiAacSbrPayloadFrame *previous,const SautiAacSbrLimiterBands *bands,const SautiAacSbrComplex *xh,
                           unsigned t,unsigned rate,const float *pred,const double *q,
                           const uint8_t *flags_now,const uint8_t *last_sine,const uint8_t *last_sine_before,EnvAdjust *out){
    const SautiAacSbrTables *tab=&f->tables;
    const unsigned kx=tab->kx,k2=kx+tab->m;
    const int carried=t<f->pvc.sin_len_cur;
    double e[64]={0},curr[64]={0},q_sine[64];
    uint8_t cond1[64]={0},cond2[64]={0},sine_index[64]={0},sine_mapped[64]={0};
    memcpy(q_sine,q,sizeof q_sine);
    if(carried && (!previous || previous->energy.noise_count<1u))return 0;
    for(unsigned k=kx;k<k2;k++){
        double sum=0.0;
        for(unsigned l=0u;l<rate;l++){
            const SautiAacSbrComplex z=xh[(size_t)(t*rate+l)*64u+k];
            sum+=(double)z.re*z.re+(double)z.im*z.im;
        }
        curr[k]=ldexp(sum/(double)rate,30);
        e[k]=pred[k];
        if(!isfinite(curr[k]) || !isfinite(e[k]) || e[k]<0.0)return 0;
        cond1[k]=(uint8_t)(flags_now[k] && (t>=f->pvc.sine_position || last_sine[k]));
        cond2[k]=(uint8_t)(carried && last_sine[k] && (t>=f->pvc.sin_start_cur || last_sine_before[k]));
    }
    for(unsigned b=0u;b<tab->n_low;b++){
        unsigned present=0u;
        for(unsigned k=tab->low[b];k<tab->low[b+1u];k++)if(carried?cond2[k]:cond1[k])present=1u;
        if(present)for(unsigned k=tab->low[b];k<tab->low[b+1u];k++){
            sine_mapped[k]=1u;
            sine_index[k]=carried?(uint8_t)(cond1[k]||cond2[k]):cond1[k];
        }
        if(!f->header.interpol_freq){                /* one measured power per low-resolution band */
            double band_sum=0.0;
            for(unsigned k=tab->low[b];k<tab->low[b+1u];k++)band_sum+=curr[k];
            band_sum/=(double)(tab->low[b+1u]-tab->low[b]);
            for(unsigned k=tab->low[b];k<tab->low[b+1u];k++)curr[k]=band_sum;
        }
    }
    if(carried)
        for(unsigned b=0u;b<tab->n_noise;b++)
            for(unsigned k=tab->noise[b];k<tab->noise[b+1u];k++)
                if(cond2[k])q_sine[k]=previous->energy.noise_factor[previous->energy.noise_count-1u][b];
    SautiAacSbrGainLimitedFrame limited;
    if(!sauti_aac_sbr_gain_limit_partition_flags_ex(e,curr,q,q_sine,sine_index,sine_mapped,bands->edges,bands->count,f->gain_mode,1u,&limited))return 0;
    memset(out,0,sizeof(*out));
    out->delta=1u;
    for(unsigned k=kx;k<k2;k++){
        out->gain[k]=limited.band[k].hf_gain;
        out->noise[k]=limited.band[k].noise_amplitude;
        out->sine[k]=limited.band[k].sine_amplitude;
        if(!isfinite(out->gain[k]) || !isfinite(out->noise[k]) || !isfinite(out->sine[k]) ||
           out->gain[k]<0.0 || out->noise[k]<0.0 || out->sine[k]<0.0)return 0;
    }
    return 1;
}

int sauti_aac_sbr_channel_frame(SautiAacSbrChannel *ch,const float core[1024],
                                const SautiAacSbrPayloadFrame *f,
                                SautiAacSbrComplex grid[]){
    if(!ch || !core || !f || !grid || ch->ready!=1u)return 0;
    const SautiAacSbrTables *tab=&f->tables;
    const unsigned kx=tab->kx,k2=kx+tab->m;
    const int pvc=f->pvc.active!=0u;
    const unsigned envs=pvc?SAUTI_USAC_PVC_SLOTS-f->pvc.first_slot:f->grid.env_count;      /* PVC: every time slot is an envelope of its own */
    if(envs<1u || (!pvc && envs>SAUTI_AAC_SBR_MAX_ENV) || kx<1u || kx>32u || k2<=kx || k2>64u ||
       f->transient_env>envs || f->bs_smoothing_mode>1u)return 0;
    if(pvc && (f->pvc.first_slot>=SAUTI_USAC_PVC_SLOTS || (ch->analysis_bands!=0u && ch->analysis_bands!=32u && ch->analysis_bands!=16u)))return 0;
#ifdef SAUTI_SBR_CHANNEL_TRACE
    g_trace_frame=ch->frames;
#endif
    const int hbe_on=f->hbe.enabled!=0u;                  /* bs_harmonicSBR: low band and transposer lag the analysis by one frame */
    const unsigned R=ch->analysis_bands==16u?2u:1u;       /* 1: 2:1 / 8:3 (32 slots), 2: 4:1 (64 slots) */
    const unsigned SLOTS=SAUTI_AAC_SBR_CHANNEL_SLOTS*R,HIST=2u+6u*R,MAXI=38u*R;
    const unsigned rate=2u*R;                         /* QMF slots per time slot */
    unsigned elo[SAUTI_USAC_PVC_SLOTS],ehi[SAUTI_USAC_PVC_SLOTS];   /* the QMF slots of every envelope */
    for(unsigned e=0u;e<envs;e++){
        elo[e]=pvc?(f->pvc.first_slot+e)*rate:f->env_edges[e];
        ehi[e]=pvc?(f->pvc.first_slot+e+1u)*rate:f->env_edges[e+1u];
    }
    const unsigned t0=elo[0],tN=ehi[envs-1u];
    if(t0>6u*R || tN<SLOTS || tN>MAXI)return 0;
    for(unsigned e=0u;e<envs;e++)
        if(ehi[e]<=elo[e])return 0;
    const unsigned core_len=ch->analysis_bands==24u?768u:1024u;
    if(ch->analysis_bands!=0u && ch->analysis_bands!=16u && ch->analysis_bands!=24u && ch->analysis_bands!=32u)return 0;
    for(unsigned i=0u;i<core_len;i++)if(!isfinite(core[i]))return 0;

    SautiAacSbrChannel staged=*ch;                    /* committed only when the whole frame succeeded */
    SautiAacSbrChannel *next=&staged;

    /* 1. QMF analysis and the 40-slot low-band window */
    SautiAacSbrComplex low[SLOTS_MAX*32u];
    if(ch->analysis_bands==24u){
        if(!sauti_aac_sbr_qmf_analyze_24(core,768u,SLOTS,low,SLOTS*32u,&next->analysis))return 0;
    }else if(ch->analysis_bands==16u){
        if(!sauti_aac_sbr_qmf_analyze_16(core,1024u,SLOTS,low,SLOTS*32u,&next->analysis))return 0;
    }else if(!sauti_aac_sbr_qmf_analyze_32(core,1024u,SLOTS,32u,low,SLOTS*32u,&next->analysis))return 0;
    SautiAacSbrComplex xlow[(SLOTS_MAX+14u)*32u];
    SautiUsacHbeCx xpv[(14u+64u)*64u];                /* transposer output of the frame, with the HIST slots before it */
    const SautiAacSbrComplex *lowsrc=low;
    memcpy(xlow,next->low_history,(size_t)HIST*32u*sizeof(SautiAacSbrComplex));
    if(hbe_on){
        SautiUsacHbeCx qin[64u*64u],pvnew[64u*64u];
        const SautiAacSbrTables *pt=ch->have_previous?&ch->previous.tables:NULL;
        const int same=next->hbe.ready && pt && pt->n_low==tab->n_low && pt->n_high==tab->n_high &&
                       !memcmp(pt->low,tab->low,tab->n_low+1u) && !memcmp(pt->high,tab->high,tab->n_high+1u);
        const unsigned up4=ch->analysis_bands==16u;
        unsigned s,i,k;
        lowsrc=ch->hbe_low[0];
        if(!same){                                    /* a reset: new tables, the transposer is cleared and replays the low band the reference decoder still has in its buffers */
            if(!sauti_usac_hbe_reinit(&next->hbe,SLOTS,tab->low,tab->n_low,tab->high,tab->n_high,up4,ch->analysis_bands==24u))return 0;
            for(s=0u;s<(up4?1u:2u);s++){
                memset(qin,0,sizeof qin);
                for(i=0u;i<SLOTS;i++){
                    /* 2:1 / 8:3: the last two frames; 4:1: the 64 slots from slot 30 of the frame before the last */
                    const SautiAacSbrComplex *src=up4?(i<34u?ch->hbe_low[1]+(size_t)(30u+i)*32u:ch->hbe_low[0]+(size_t)(i-34u)*32u):ch->hbe_low[1u-s]+(size_t)i*32u;
                    for(k=0u;k<32u;k++){qin[(size_t)i*64u+k].re=src[k].re;qin[(size_t)i*64u+k].im=src[k].im;}
                }
                if(!sauti_usac_hbe_process(&next->hbe,qin,f->hbe.pitch_in_bins,pvnew))return 0;
            }
            for(i=0u;i<HIST*64u;i++){next->pv_history[i][0]=(float)pvnew[(size_t)(SLOTS-HIST)*64u+i].re;next->pv_history[i][1]=(float)pvnew[(size_t)(SLOTS-HIST)*64u+i].im;}
        }
        memset(qin,0,sizeof qin);
        for(i=0u;i<SLOTS;i++){                        /* 4:1: the transposer works 32 slots ahead of the frame it produces: the last half of the previous frame, then the first half of this one */
            const SautiAacSbrComplex *src=up4?(i<32u?ch->hbe_low[0]+(size_t)(32u+i)*32u:low+(size_t)(i-32u)*32u):low+(size_t)i*32u;
            for(k=0u;k<32u;k++){qin[(size_t)i*64u+k].re=src[k].re;qin[(size_t)i*64u+k].im=src[k].im;}
        }
        if(!sauti_usac_hbe_process(&next->hbe,qin,f->hbe.pitch_in_bins,pvnew))return 0;
        for(i=0u;i<HIST*64u;i++){xpv[i].re=next->pv_history[i][0];xpv[i].im=next->pv_history[i][1];}
        memcpy(xpv+(size_t)HIST*64u,pvnew,sizeof(SautiUsacHbeCx)*(size_t)SLOTS*64u);
        for(i=0u;i<HIST*64u;i++){next->pv_history[i][0]=(float)xpv[(size_t)SLOTS*64u+i].re;next->pv_history[i][1]=(float)xpv[(size_t)SLOTS*64u+i].im;}
    }
    memcpy(xlow+(size_t)HIST*32u,lowsrc,(size_t)SLOTS*32u*sizeof(SautiAacSbrComplex));

    /* 2. inverse filtering: LPC over 38 equations of the 40-slot window, chirp */
    SautiAacSbrLpcCoeffs lpc;
    SautiAacSbrChirp chirp=next->chirp;
    const SautiAacSbrChirp chirp_before=chirp;
    /* A header that changes the number of noise bands does not clear the bandwidth factors and the previous inverse-filter modes: they are
     * kept per band index (the reference decoder never clears them on a reset, and the entries above the new count stay as they were). */
    if(chirp.ready && chirp.count!=tab->n_noise)chirp.count=tab->n_noise;
    if(!sauti_aac_sbr_lpc_estimate_2(xlow,(size_t)(SLOTS+HIST)*32u,SLOTS+HIST-2u,&lpc) ||
       !sauti_aac_sbr_chirp_update(f->control.invf_mode,tab->n_noise,&chirp))return 0;
    if(chirp_before.ready)
        for(unsigned j=tab->n_noise;j<SAUTI_AAC_SBR_MAX_NOISE_BANDS;j++){
            chirp.bw[j]=chirp_before.bw[j];
            chirp.previous_mode[j]=chirp_before.previous_mode[j];
        }
    next->chirp=chirp;

    /* 3. HF carrier for the slots this frame owns: i in [t0, tN), taken from X_low[i..i+2] */
    SautiAacSbrComplex xh[MAX_I*64u];
    memset(xh,0,sizeof(xh));
    /* A PVC frame adjusts only the 16 time slots of the frame, but its carrier is generated over the whole grid (it may run past the frame by 0..3 time slots): those slots are adjusted by the next
     * frame, which starts with the carrier left over here; the carrier of this frame's own first slots, that the previous grid ran into, is the one the previous frame generated. */
    const unsigned g0=pvc?f->grid.env_border[0]*rate:t0,gN=pvc?f->grid.env_border[f->grid.env_count]*rate:tN;
    if(gN<tN || gN>MAXI || g0<t0)return 0;
    if(hbe_on && f->hbe.active){
        /* harmonic transposer frame: no patching, every HF band is the transposed signal of its own band plus its second-order prediction (the LPC of the transposed signal, the chirp of the band's noise band) */
        SautiAacSbrComplex plane[2][(14u+64u)*32u];
        SautiAacSbrLpcCoeffs lp[2];
        for(unsigned t=0u;t<HIST+SLOTS;t++)
            for(unsigned k=0u;k<32u;k++)for(unsigned half=0u;half<2u;half++){
                plane[half][(size_t)t*32u+k].re=(float)xpv[(size_t)t*64u+half*32u+k].re;
                plane[half][(size_t)t*32u+k].im=(float)xpv[(size_t)t*64u+half*32u+k].im;
            }
        if(!sauti_aac_sbr_lpc_estimate_2(plane[0],(size_t)(SLOTS+HIST)*32u,SLOTS+HIST-2u,&lp[0]) ||
           !sauti_aac_sbr_lpc_estimate_2(plane[1],(size_t)(SLOTS+HIST)*32u,SLOTS+HIST-2u,&lp[1]))return 0;
        for(unsigned k=kx;k<k2;k++){
            unsigned nb=0u;
            while(nb+1u<tab->n_noise && k>=tab->noise[nb+1u])nb++;
            const double bw=chirp.bw[nb],bw2=bw*bw;
            const SautiAacSbrComplex a0=lp[k>=32u].alpha0[k&31u],a1=lp[k>=32u].alpha1[k&31u];
            for(unsigned i=g0;i<gN;i++){
                const SautiUsacHbeCx c=xpv[(size_t)(i+2u)*64u+k],p1=xpv[(size_t)(i+1u)*64u+k],p2=xpv[(size_t)i*64u+k];
                double re=c.re,im=c.im;
                if(bw>0.0){
                    re+=bw*((double)a0.re*p1.re-(double)a0.im*p1.im)+bw2*((double)a1.re*p2.re-(double)a1.im*p2.im);
                    im+=bw*((double)a0.im*p1.re+(double)a0.re*p1.im)+bw2*((double)a1.im*p2.re+(double)a1.re*p2.im);
                }
                xh[(size_t)i*64u+k].re=(float)re;xh[(size_t)i*64u+k].im=(float)im;
            }
        }
    }else if(!sauti_aac_sbr_hf_predict(&f->patches,tab,xlow+(size_t)g0*32u,(size_t)(gN-g0+2u)*32u,gN-g0,&lpc,&chirp,
                                 xh+(size_t)g0*64u,(size_t)(gN-g0)*64u))return 0;
    if(pvc){
        if(t0<g0){
            if(!ch->have_previous || ch->previous.pvc.active==0u || g0>12u)return 0;
            memcpy(xh+(size_t)t0*64u,ch->pvc_carry,(size_t)(g0-t0)*64u*sizeof(SautiAacSbrComplex));
        }
        memset(next->pvc_carry,0,sizeof(next->pvc_carry));
        if(gN>SLOTS)memcpy(next->pvc_carry,xh+(size_t)SLOTS*64u,(size_t)(gN-SLOTS)*64u*sizeof(SautiAacSbrComplex));
    }

    /* 4. one limiter pass per envelope. The sine map of an envelope is this frame's flags once its onset has been reached. Before
     * the onset a tone exists only if it is flagged now AND was already sounding at the end of the previous frame (it continues);
     * a tone flagged now that was not running does not start before its onset, and one that was running but is no longer flagged
     * stops. (Per bin, over 125 pre-onset cases in three streams: the reference decoder follows this rule in all of them.) */
    uint8_t flags_now[64]={0};
    for(unsigned b=0u;b<tab->n_high;b++){
        if(f->harmonic[b]>1u || tab->high[b]>=tab->high[b+1u] || tab->high[b+1u]>64u)return 0;
        if(f->harmonic[b])flags_now[(tab->high[b]+tab->high[b+1u])/2u]=1u;
    }
    SautiAacSbrLimiterBands lim;                      /* limiter bands of the frame: junctions of the patch map, or the borders of the transposer's orders */
    if(hbe_on && f->hbe.active){
        uint8_t junction[SAUTI_AAC_SBR_MAX_PATCHES];
        unsigned n=0u;
        for(unsigned i=1u;i<(R==2u?6u:4u);i++)if(next->hbe.x_over[i] && n<SAUTI_AAC_SBR_MAX_PATCHES)n++;
        for(unsigned i=1u;i<n;i++)junction[i-1u]=(uint8_t)next->hbe.x_over[i];     /* the reference counts the orders 2.. and takes the borders x_over[1 .. n-1] as junctions */
        if(!sauti_aac_sbr_limiter_bands_build_junctions(tab,junction,n>0u?n-1u:0u,f->limiter_mode,&lim))return 0;
    }else if(!sauti_aac_sbr_limiter_bands_build(tab,&f->patches,f->limiter_mode,&lim))return 0;
    EnvAdjust adj[SAUTI_USAC_PVC_SLOTS];
    const SautiAacSbrPayloadFrame *before=ch->have_previous?&ch->previous:NULL;
    uint8_t flags_pre[64],flags_last[64]={0};
    for(unsigned k=0u;k<64u;k++)flags_pre[k]=(uint8_t)(flags_now[k] && ch->last_sine[k]);
    if(pvc){
        /* PVC: predict the envelope of the 16 time slots from the low band energy, then adjust every slot as an envelope */
        float low_energy[16][32],pred[16][64];
        for(unsigned t=0u;t<16u;t++)
            for(unsigned b=0u;b<32u;b++){
                double sum=0.0;
                for(unsigned l=0u;l<rate;l++){
                    const SautiAacSbrComplex z=xlow[(size_t)(t*rate+l+2u)*32u+b];
                    sum+=(double)z.re*z.re+(double)z.im*z.im;
                }
                low_energy[t][b]=(float)ldexp(sum/(double)rate,30);
            }
        if(!sauti_usac_pvc_predict(&next->pvc,&f->pvc,rate,kx,low_energy,pred))return 0;
        /* noise floor of every time slot of the frame's grid (it may run past the frame; the first slots come from the previous frame's overrun) */
        double qs[SAUTI_USAC_PVC_SLOTS+4u][64];
        memset(qs,0,sizeof(qs));
        const unsigned tb0=f->grid.env_border[0];
        for(unsigned t=0u;t<tb0 && t<4u;t++)memcpy(qs[t],ch->q_carry[t],sizeof(qs[t]));
        for(unsigned g=0u;g<f->grid.env_count;g++)
            for(unsigned t=f->grid.env_border[g];t<f->grid.env_border[g+1u] && t<SAUTI_USAC_PVC_SLOTS+4u;t++)
                for(unsigned b=0u;b<tab->n_noise;b++)
                    for(unsigned k=tab->noise[b];k<tab->noise[b+1u];k++)qs[t][k]=f->energy.noise_factor[g][b];
        const SautiAacSbrLimiterBands pbands=lim;
        for(unsigned e=0u;e<envs;e++){
            const unsigned t=f->pvc.first_slot+e;
            if(!adjust_pvc_slot(f,ch->have_previous?&ch->previous:NULL,&pbands,xh,t,rate,pred[t],qs[t],flags_now,ch->last_sine,ch->last_sine_before,&adj[e]))return 0;
        }
        for(unsigned t=0u;t<4u;t++)memcpy(next->q_carry[t],qs[SAUTI_USAC_PVC_SLOTS+t],sizeof(next->q_carry[t]));
        memcpy(flags_last,flags_now,sizeof(flags_last));
    }else{
        next->pvc.prev_flag=0u;
        for(unsigned e=0u;e<envs;e++){
            const unsigned onset=f->transient_env;        /* 0: no onset signalled (or at the first envelope) */
            const int open=(onset==0u) || e>=onset;
            const uint8_t *flags=open?flags_now:flags_pre;
            if(!adjust_envelope(f,before,&lim,xh,e,flags,&adj[e]))return 0;
            memcpy(flags_last,flags,sizeof(flags_last));
        }
    }

    /* 5. gain/noise per slot with the smoothing history, then noise and sine on top of the carrier */
    SautiAacSbrComplex y[MAX_I*64u];
    memset(y,0,sizeof(y));
    double gseq[(4u+MAX_I)*64u],qseq[(4u+MAX_I)*64u];
    memset(gseq,0,sizeof(gseq));memset(qseq,0,sizeof(qseq));
    const unsigned total=tN-t0;
    /* A header that changes the frequency tables is an SBR reset (14496-3 4.6.18.3.1): the noise phase index starts again at 0 (the sine phase index does not) and the smoothing history restarts from the first envelope
     * (the reference decoder's resetSbrEnvelopeCalc: phaseIndex = 0, startUp = 1). */
    const SautiAacSbrHeader *ph=ch->have_previous?&ch->previous.header:NULL;
    const int header_reset=ph && (ph->start_freq!=f->header.start_freq || ph->stop_freq!=f->header.stop_freq || ph->freq_scale!=f->header.freq_scale ||
                                  ph->alter_scale!=f->header.alter_scale || ph->noise_bands!=f->header.noise_bands || ph->xover_band!=f->header.xover_band);
    if(!ch->history_valid || ch->have_previous==0u || header_reset ||
       ch->previous.tables.kx!=kx || ch->previous.tables.m!=tab->m){
        for(unsigned j=0u;j<4u;j++)
            for(unsigned k=kx;k<k2;k++){
                next->gain_history[j][k]=adj[0].gain[k];
                next->noise_history[j][k]=adj[0].noise[k];
            }
    }
    for(unsigned j=0u;j<4u;j++)
        for(unsigned k=kx;k<k2;k++){
            gseq[(size_t)j*64u+k]=next->gain_history[j][k];
            qseq[(size_t)j*64u+k]=next->noise_history[j][k];
        }
    for(unsigned e=0u;e<envs;e++)
        for(unsigned i=elo[e];i<ehi[e];i++)
            for(unsigned k=kx;k<k2;k++){
                gseq[(size_t)(4u+i-t0)*64u+k]=adj[e].gain[k];
                qseq[(size_t)(4u+i-t0)*64u+k]=adj[e].noise[k];
            }
    const unsigned smoothing=f->bs_smoothing_mode==0u;
    if(header_reset)next->index_noise=0u;                /* the sine phase index keeps running: the reference decoder clears only the noise index on a reset */
    unsigned index_noise=next->index_noise,index_sine=next->index_sine;
    const unsigned m_max=tab->m;
    for(unsigned e=0u;e<envs;e++){
        const int quiet=adj[e].delta==0u;             /* transient envelope: the sine is added, noise and smoothing are not */
        for(unsigned i=elo[e];i<ehi[e];i++){
            const size_t idx=(size_t)(4u+i-t0);
            for(unsigned k=kx;k<k2;k++){
                double g=gseq[idx*64u+k],q=qseq[idx*64u+k];
                if(smoothing && !quiet){
                    g=0.0;q=0.0;
                    for(unsigned j=0u;j<5u;j++){
                        g+=gseq[(idx-j)*64u+k]*h_smooth[j];
                        q+=qseq[(idx-j)*64u+k]*h_smooth[j];
                    }
                }
                const SautiAacSbrComplex x=xh[(size_t)i*64u+k];
                double re=(double)x.re*g,im=(double)x.im*g;
                const double s=adj[e].sine[k];
                if(s>0.0){                            /* standard: phi_re = {1,0,-1,0}, phi_im = (-1)^k {0,1,0,-1} */
                    const double amp=ldexp(s,-15);
                    const double sign=(k&1u)?-1.0:1.0;
                    switch(index_sine&3u){
                        case 0u:re+=amp;break;
                        case 1u:im+=sign*amp;break;
                        case 2u:re-=amp;break;
                        default:im-=sign*amp;break;
                    }
                }else if(!quiet && q>0.0){
                    SautiAacSbrComplex v;
                    if(!sauti_aac_sbr_noise_rom_lookup((index_noise+(k-kx)+1u)&511u,&v))return 0;
                    const double amp=ldexp(q,-15);
                    re+=(double)v.re*amp;
                    im+=(double)v.im*amp;
                }
                if(!isfinite(re) || !isfinite(im) || fabs(re)>3.0e38 || fabs(im)>3.0e38)return 0;
                y[(size_t)i*64u+k].re=(float)re;
                y[(size_t)i*64u+k].im=(float)im;
            }
            index_noise=(index_noise+m_max)&511u;
            index_sine=(index_sine+1u)&3u;
        }
    }
    next->index_noise=index_noise;
    next->index_sine=index_sine;
    for(unsigned j=0u;j<4u;j++)
        for(unsigned k=kx;k<k2;k++){
            next->gain_history[j][k]=gseq[(size_t)(total+j)*64u+k];       /* last four processed slots */
            next->noise_history[j][k]=qseq[(size_t)(total+j)*64u+k];
        }
    next->history_valid=1u;

    /* 5b. inter-subband temporal envelope shaping (USAC eSBR, bs_inter_temp_shape_mode of the envelope): the HF of every slot is scaled by the temporal envelope of the low band over the envelope,
     * gain_i = 1 + gamma (sqrt(P_low_i N / sum P_low) - 1) (at least 0.2), renormalised so that the HF energy of the envelope is unchanged */
    for(unsigned e=0u;e<envs && !pvc;e++){                /* (a PVC frame has no envelope data and so no shaping modes) */
        static const double gamma_table[4]={0.0,1.0,2.0,4.0};
        const double gamma=gamma_table[f->quant.tes_mode[e]&3u];
        if(gamma<=0.0)continue;
        const unsigned lo=elo[e],hi=ehi[e],n=hi-lo;
        double pl[MAX_I],ph[MAX_I],gain[MAX_I],tot_l=0.0,tot_h=0.0,tot_after=1.0e-6;
        for(unsigned i=lo;i<hi;i++){
            double a=0.0,b=0.0;
            for(unsigned k=0u;k<kx;k++){const SautiAacSbrComplex z=xlow[(size_t)(i+2u)*32u+k];a+=(double)z.re*z.re+(double)z.im*z.im;}
            for(unsigned k=kx;k<k2;k++){const SautiAacSbrComplex z=y[(size_t)i*64u+k];b+=(double)z.re*z.re+(double)z.im*z.im;}
            pl[i-lo]=a;ph[i-lo]=b;tot_l+=a;tot_h+=b;
        }
        for(unsigned i=0u;i<n;i++){
            double g=sqrt(pl[i]*(double)n/(tot_l+1.0e-6));
            g=1.0+gamma*(g-1.0);
            if(g<0.2)g=0.2;
            gain[i]=g;ph[i]*=g*g;tot_after+=ph[i];
        }
        const double adj=sqrt(tot_h/tot_after);
        for(unsigned i=0u;i<n;i++){
            const double g=gain[i]*adj;
            for(unsigned k=kx;k<k2;k++){
                SautiAacSbrComplex *z=&y[(size_t)(lo+i)*64u+k];
                z->re=(float)(z->re*g);z->im=(float)(z->im*g);
            }
        }
    }

    /* 6. output grid: spill from the previous call first, this frame's own slots after it */
    memset(grid,0,sizeof(SautiAacSbrComplex)*SAUTI_AAC_SBR_CHANNEL_GRID*R);
    const int continues=(ch->have_previous==0u && t0==0u) ||
                        (ch->have_previous!=0u && ch->previous_last_edge>=SLOTS && t0==ch->previous_last_edge-SLOTS);
    if(!continues)next->discontinuities++;
    for(unsigned i=0u;i<SLOTS;i++){
        SautiAacSbrComplex *row=grid+(size_t)i*64u;
        for(unsigned k=0u;k<kx;k++)row[k]=xlow[(size_t)(i+2u)*32u+k];
        if(ch->mps_lf_ahead)for(unsigned k=0u;k<3u && k<kx;k++)row[k]=xlow[(size_t)(i+8u)*32u+k];   /* six slots ahead of the other bands at every eSBR ratio (the hybrid filter delay), not HIST - 2 */
        if(i>=t0){
            for(unsigned k=kx;k<k2;k++)row[k]=y[(size_t)i*64u+k];
        }else if(continues && ch->spill_slots>i){
            for(unsigned k=ch->spill_kx;k<ch->spill_k2 && k<64u;k++)
                if(k>=kx && k<k2)row[k]=ch->spill[(size_t)i*64u+k];
        }else if(!continues && ch->have_previous && ch->previous.pvc.active && i<12u){
            /* a classic frame that starts at slot > 0 right after a PVC frame whose grid ran past its end: nothing has adjusted those slots (a PVC frame adjusts only its own 16 time slots), the reference
             * decoder passes the carrier of the previous frame through unadjusted; so do we (an encoder signals no variable length before a switch to classic frames) */
            for(unsigned k=kx;k<k2;k++)row[k]=ch->pvc_carry[(size_t)i*64u+k];
        }
        for(unsigned k=0u;k<64u;k++)if(!finite_c(row[k]))return 0;
    }
    next->spill_slots=tN-SLOTS;
    next->spill_kx=kx;next->spill_k2=k2;
    memset(next->spill,0,sizeof(next->spill));
    for(unsigned i=SLOTS;i<tN;i++)
        memcpy(next->spill+(size_t)(i-SLOTS)*64u,y+(size_t)i*64u,64u*sizeof(SautiAacSbrComplex));

    /* 7. carry the rest of the state */
    memcpy(next->low_history,xlow+(size_t)SLOTS*32u,(size_t)HIST*32u*sizeof(SautiAacSbrComplex));      /* the last HIST slots of the low band that went out (with the transposer: one frame behind the analysis) */
    if(hbe_on){
        memcpy(next->hbe_low[1],next->hbe_low[0],sizeof next->hbe_low[1]);
        memcpy(next->hbe_low[0],low,(size_t)SLOTS*32u*sizeof(SautiAacSbrComplex));
    }
    memcpy(next->last_sine_before,ch->last_sine,sizeof(next->last_sine_before));
    memcpy(next->last_sine,flags_last,sizeof(next->last_sine));
    next->previous=*f;
    next->have_previous=1u;
    next->previous_last_edge=tN;
    next->frames=ch->frames+1u;
    *ch=*next;
    return 1;
}

int sauti_aac_sbr_channel_synthesize(SautiAacSbrChannel *ch,
                                     const SautiAacSbrComplex grid[],
                                     float pcm[]){
    if(!ch || !grid || !pcm || ch->ready!=1u)return 0;
    SautiAacSbrQmfSynthWindowState synth=ch->synth;
    float out[4096];
    const unsigned half=ch->analysis_bands==16u?2u:1u;                    /* 4:1: 64 slots = two calls of 32 */
    for(unsigned h=0u;h<half;h++)
        if(!sauti_aac_sbr_qmf_synth_64(grid+(size_t)h*SAUTI_AAC_SBR_CHANNEL_GRID,SAUTI_AAC_SBR_CHANNEL_GRID,SAUTI_AAC_SBR_CHANNEL_SLOTS,out+(size_t)h*2048u,2048u,&synth))return 0;
    for(unsigned i=0u;i<2048u*half;i++)if(!isfinite(out[i]))return 0;
    ch->synth=synth;
    memcpy(pcm,out,2048u*half*sizeof(float));
    return 1;
}
