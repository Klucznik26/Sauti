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

#define SLOTS SAUTI_AAC_SBR_CHANNEL_SLOTS
#define BANDS SAUTI_AAC_SBR_CHANNEL_BANDS
#define MAX_I 38u            /* an envelope never ends after output slot 38 */

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
                           const SautiAacSbrComplex *xh,unsigned env,
                           const uint8_t *flag_source,   /* sine flags this envelope works with, per bin */
                           EnvAdjust *out){
    const SautiAacSbrTables *tab=&f->tables;
    const unsigned kx=tab->kx,k2=kx+tab->m;
    const unsigned lo=f->env_edges[env],hi=f->env_edges[env+1u];
    SautiAacSbrLimiterBands bands;
    if(!sauti_aac_sbr_limiter_bands_build(tab,&f->patches,f->limiter_mode,&bands))return 0;
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
                                                 bands.edges,bands.count,f->gain_mode,delta,&limited))return 0;
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

int sauti_aac_sbr_channel_frame(SautiAacSbrChannel *ch,const float core[1024],
                                const SautiAacSbrPayloadFrame *f,
                                SautiAacSbrComplex grid[SAUTI_AAC_SBR_CHANNEL_GRID]){
    if(!ch || !core || !f || !grid || ch->ready!=1u)return 0;
    const SautiAacSbrTables *tab=&f->tables;
    const unsigned kx=tab->kx,k2=kx+tab->m;
    const unsigned envs=f->grid.env_count;
    if(envs<1u || envs>SAUTI_AAC_SBR_MAX_ENV || kx<1u || kx>32u || k2<=kx || k2>64u ||
       f->transient_env>envs || f->bs_smoothing_mode>1u)return 0;
#ifdef SAUTI_SBR_CHANNEL_TRACE
    g_trace_frame=ch->frames;
#endif
    const unsigned t0=f->env_edges[0],tN=f->env_edges[envs];
    if(t0>6u || tN<SLOTS || tN>MAX_I)return 0;
    for(unsigned e=0u;e<envs;e++)
        if(f->env_edges[e+1u]<=f->env_edges[e])return 0;
    for(unsigned i=0u;i<1024u;i++)if(!isfinite(core[i]))return 0;

    SautiAacSbrChannel staged=*ch;                    /* committed only when the whole frame succeeded */
    SautiAacSbrChannel *next=&staged;

    /* 1. QMF analysis and the 40-slot low-band window */
    SautiAacSbrComplex low[SLOTS*32u];
    if(!sauti_aac_sbr_qmf_analyze_32(core,1024u,SLOTS,32u,low,SLOTS*32u,&next->analysis))return 0;
    SautiAacSbrComplex xlow[40u*32u];
    memcpy(xlow,next->low_history,sizeof(next->low_history));
    memcpy(xlow+8u*32u,low,sizeof(low));

    /* 2. inverse filtering: LPC over 38 equations of the 40-slot window, chirp */
    SautiAacSbrLpcCoeffs lpc;
    SautiAacSbrChirp chirp=next->chirp;
    const SautiAacSbrChirp chirp_before=chirp;
    /* A header that changes the number of noise bands does not clear the bandwidth factors and the previous inverse-filter modes: they are
     * kept per band index (the reference decoder never clears them on a reset, and the entries above the new count stay as they were). */
    if(chirp.ready && chirp.count!=tab->n_noise)chirp.count=tab->n_noise;
    if(!sauti_aac_sbr_lpc_estimate_2(xlow,40u*32u,38u,&lpc) ||
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
    if(!sauti_aac_sbr_hf_predict(&f->patches,tab,xlow+(size_t)t0*32u,(size_t)(tN-t0+2u)*32u,tN-t0,&lpc,&chirp,
                                 xh+(size_t)t0*64u,(size_t)(tN-t0)*64u))return 0;

    /* 4. one limiter pass per envelope. The sine map of an envelope is this frame's flags once its onset has been reached. Before
     * the onset a tone exists only if it is flagged now AND was already sounding at the end of the previous frame (it continues);
     * a tone flagged now that was not running does not start before its onset, and one that was running but is no longer flagged
     * stops. (Per bin, over 125 pre-onset cases in three streams: the reference decoder follows this rule in all of them.) */
    uint8_t flags_now[64]={0};
    for(unsigned b=0u;b<tab->n_high;b++){
        if(f->harmonic[b]>1u || tab->high[b]>=tab->high[b+1u] || tab->high[b+1u]>64u)return 0;
        if(f->harmonic[b])flags_now[(tab->high[b]+tab->high[b+1u])/2u]=1u;
    }
    EnvAdjust adj[SAUTI_AAC_SBR_MAX_ENV];
    const SautiAacSbrPayloadFrame *before=ch->have_previous?&ch->previous:NULL;
    uint8_t flags_pre[64],flags_last[64]={0};
    for(unsigned k=0u;k<64u;k++)flags_pre[k]=(uint8_t)(flags_now[k] && ch->last_sine[k]);
    for(unsigned e=0u;e<envs;e++){
        const unsigned onset=f->transient_env;        /* 0: no onset signalled (or at the first envelope) */
        const int open=(onset==0u) || e>=onset;
        const uint8_t *flags=open?flags_now:flags_pre;
        if(!adjust_envelope(f,before,xh,e,flags,&adj[e]))return 0;
        memcpy(flags_last,flags,sizeof(flags_last));
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
        for(unsigned i=f->env_edges[e];i<f->env_edges[e+1u];i++)
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
        for(unsigned i=f->env_edges[e];i<f->env_edges[e+1u];i++){
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

    /* 6. output grid: spill from the previous call first, this frame's own slots after it */
    memset(grid,0,sizeof(SautiAacSbrComplex)*SAUTI_AAC_SBR_CHANNEL_GRID);
    const int continues=(ch->have_previous==0u && t0==0u) ||
                        (ch->have_previous!=0u && ch->previous_last_edge>=SLOTS && t0==ch->previous_last_edge-SLOTS);
    if(!continues)next->discontinuities++;
    for(unsigned i=0u;i<SLOTS;i++){
        SautiAacSbrComplex *row=grid+(size_t)i*64u;
        for(unsigned k=0u;k<kx;k++)row[k]=xlow[(size_t)(i+2u)*32u+k];
        if(i>=t0){
            for(unsigned k=kx;k<k2;k++)row[k]=y[(size_t)i*64u+k];
        }else if(continues && ch->spill_slots>i){
            for(unsigned k=ch->spill_kx;k<ch->spill_k2 && k<64u;k++)
                if(k>=kx && k<k2)row[k]=ch->spill[(size_t)i*64u+k];
        }
        for(unsigned k=0u;k<64u;k++)if(!finite_c(row[k]))return 0;
    }
    next->spill_slots=tN-SLOTS;
    next->spill_kx=kx;next->spill_k2=k2;
    memset(next->spill,0,sizeof(next->spill));
    for(unsigned i=SLOTS;i<tN;i++)
        memcpy(next->spill+(size_t)(i-SLOTS)*64u,y+(size_t)i*64u,64u*sizeof(SautiAacSbrComplex));

    /* 7. carry the rest of the state */
    memcpy(next->low_history,low+24u*32u,sizeof(next->low_history));
    memcpy(next->last_sine,flags_last,sizeof(next->last_sine));
    next->previous=*f;
    next->have_previous=1u;
    next->previous_last_edge=tN;
    next->frames=ch->frames+1u;
    *ch=*next;
    return 1;
}

int sauti_aac_sbr_channel_synthesize(SautiAacSbrChannel *ch,
                                     const SautiAacSbrComplex grid[SAUTI_AAC_SBR_CHANNEL_GRID],
                                     float pcm[2048]){
    if(!ch || !grid || !pcm || ch->ready!=1u)return 0;
    SautiAacSbrQmfSynthWindowState synth=ch->synth;
    float out[2048];
    if(!sauti_aac_sbr_qmf_synth_64(grid,SAUTI_AAC_SBR_CHANNEL_GRID,SLOTS,out,2048u,&synth))return 0;
    for(unsigned i=0u;i<2048u;i++)if(!isfinite(out[i]))return 0;
    ch->synth=synth;
    memcpy(pcm,out,sizeof(out));
    return 1;
}
