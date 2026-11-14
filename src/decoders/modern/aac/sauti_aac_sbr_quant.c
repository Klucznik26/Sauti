/* SBR-08: HE-AAC classical quantized envelope/noise reconstruction.
 * Uses the Sauti NASM bit reader and native SBR-07 Huffman decoder.
 * Decodes frequency-differential and time-differential quantized values
 * without noise dequantization, QMF, PS or audio reconstruction.
 * Changes to state, user buffer and bit position commit atomically. */
#include "sauti_aac_sbr_quant.h"
#include "sauti_aac_headers.h"
#include <stdint.h>
#include <string.h>

static int read_bits(const uint8_t *data, size_t size, size_t *pos,
                     unsigned bits, uint32_t *out) {
    return sauti_aac_bits_read_asm(data,size,pos,bits,out);
}

/* Convert a band index from current envelope frequency resolution to
 * an index in the previous envelope, respecting odd N_high. */
static unsigned previous_band(unsigned index, unsigned current_res,
                              unsigned previous_res, unsigned n_high) {
    const unsigned odd=n_high&1u;
    if (current_res==previous_res) return index;
    if (current_res) return (index+odd)/2u;     /* low -> high */
    return index ? 2u*index-odd : 0u;           /* high -> low */
}

static int tables_match(const SautiAacSbrTables *a,
                        const SautiAacSbrTables *b) {
    if (a->kx!=b->kx || a->m!=b->m ||
        a->n_high!=b->n_high || a->n_low!=b->n_low ||
        a->n_noise!=b->n_noise) return 0;
    for (unsigned i=0;i<=a->n_high;i++)
        if (a->high[i]!=b->high[i]) return 0;
    for (unsigned i=0;i<=a->n_low;i++)
        if (a->low[i]!=b->low[i]) return 0;
    for (unsigned i=0;i<=a->n_noise;i++)
        if (a->noise[i]!=b->noise[i]) return 0;
    return 1;
}

static int validate(const SautiAacSbrGrid *g,
                    const SautiAacSbrTables *t,
                    const SautiAacSbrControl *c,
                    const SautiAacSbrQuantState *s,
                    unsigned amp, unsigned balanced) {
    if (!g || !t || !c || !s || amp>1u || balanced>1u ||
        s->has_env>1u || s->has_noise>1u ||
        (s->has_env && (s->prev_freq_res>1u ||
                       s->amp_res>1u || s->balanced>1u)) ||
        g->frame_class>3u ||
        g->env_count<1u || g->env_count>SAUTI_AAC_SBR_MAX_ENV ||
        g->noise_count!=(g->env_count==1u?1u:2u) ||
        c->env_count!=g->env_count || c->noise_count!=g->noise_count ||
        c->noise_band_count!=t->n_noise ||
        t->n_high<1u || t->n_high>SAUTI_AAC_SBR_MAX_MASTER_BANDS ||
        t->n_low!=(t->n_high+1u)/2u ||
        t->n_noise<1u || t->n_noise>SAUTI_AAC_SBR_MAX_NOISE_BANDS ||
        t->n_noise>t->n_low ||
        t->kx<1u || t->kx>32u ||
        t->m<1u || t->m>64u-t->kx ||
        t->high[0]!=t->kx || t->high[t->n_high]!=t->kx+t->m ||
        t->low[0]!=t->kx || t->low[t->n_low]!=t->kx+t->m ||
        t->noise[0]!=t->kx || t->noise[t->n_noise]!=t->kx+t->m)
        return 0;
    for(unsigned i=1u;i<=t->n_high;i++)
        if(t->high[i]<=t->high[i-1u])return 0;
    for(unsigned i=1u;i<=t->n_low;i++)
        if(t->low[i]<=t->low[i-1u])return 0;
    for(unsigned i=1u;i<=t->n_noise;i++)
        if(t->noise[i]<=t->noise[i-1u])return 0;
    for(unsigned i=0;i<g->env_count;i++)
        if(g->freq_res[i]>1u || c->df_env[i]>1u)return 0;
    for(unsigned i=0;i<g->noise_count;i++)
        if(c->df_noise[i]>1u)return 0;
    for(unsigned i=0;i<t->n_noise;i++)
        if(c->invf_mode[i]>3u)return 0;
    return 1;
}

typedef struct {
    unsigned effective_amp,factor,bits;
    SautiAacSbrHuffmanBook env_t,env_f,noise_t,noise_f;
    int geometry_ok,mode_ok;
} QuantMode;
static void quant_mode(const SautiAacSbrGrid *grid,
                       const SautiAacSbrTables *tables,
                       const SautiAacSbrQuantState *state,
                       unsigned amp_res,unsigned balanced,QuantMode *mode){
    mode->effective_amp=(grid->frame_class==0u && grid->env_count==1u)
        ? 0u : amp_res;
    mode->factor=balanced?2u:1u;
    mode->bits=mode->effective_amp?(balanced?5u:6u):(balanced?6u:7u);
    mode->env_t=(SautiAacSbrHuffmanBook)(
        (mode->effective_amp?SAUTI_SBR_HUFF_ENV_3_0DB_T:
         SAUTI_SBR_HUFF_ENV_1_5DB_T)+(balanced?2u:0u));
    mode->env_f=(SautiAacSbrHuffmanBook)(mode->env_t+1u);
    mode->noise_t=balanced?SAUTI_SBR_HUFF_NOISE_BAL_3_0DB_T:
        SAUTI_SBR_HUFF_NOISE_3_0DB_T;
    mode->noise_f=balanced?SAUTI_SBR_HUFF_ENV_BAL_3_0DB_F:
        SAUTI_SBR_HUFF_ENV_3_0DB_F;
    mode->geometry_ok=tables_match(tables,&state->prev_tables);
    mode->mode_ok=state->balanced==balanced && state->amp_res==mode->effective_amp;
}
static int decode_envelopes(const uint8_t *data,size_t size,size_t *pos,
                            const SautiAacSbrGrid *grid,
                            const SautiAacSbrTables *tables,
                            const SautiAacSbrControl *control,
                            const SautiAacSbrQuantState *state,
                            const QuantMode *mode,SautiAacSbrQuantFrame *next){
    for(unsigned e=0u;e<grid->env_count;e++){
        const unsigned res=grid->freq_res[e];
        const unsigned n=res?tables->n_high:tables->n_low;
        next->freq_res[e]=(uint8_t)res;
        if(control->df_env[e]){
            if(e==0u && (!state->has_env || !mode->geometry_ok || !mode->mode_ok))return 0;
            const unsigned prev_res=e?grid->freq_res[e-1u]:state->prev_freq_res;
            const uint8_t *prev=e?next->env[e-1u]:state->prev_env;
            const unsigned prev_n=prev_res?tables->n_high:tables->n_low;
            for(unsigned j=0u;j<n;j++){
                int32_t delta=0;const unsigned ref=previous_band(j,res,prev_res,tables->n_high);
                if(ref>=prev_n || !sauti_aac_sbr_huffman_decode(data,size,pos,mode->env_t,&delta))return 0;
                const int value=(int)prev[ref]+(int)mode->factor*(int)delta;
                if(value<0 || value>127)return 0;
                next->env[e][j]=(uint8_t)value;
            }
        }else{
            uint32_t start=0u;if(!read_bits(data,size,pos,mode->bits,&start))return 0;
            const unsigned initial=start*mode->factor;if(initial>127u)return 0;
            next->env[e][0]=(uint8_t)initial;
            for(unsigned j=1u;j<n;j++){
                int32_t delta=0;if(!sauti_aac_sbr_huffman_decode(data,size,pos,mode->env_f,&delta))return 0;
                const int value=(int)next->env[e][j-1u]+(int)mode->factor*(int)delta;
                if(value<0 || value>127)return 0;
                next->env[e][j]=(uint8_t)value;
            }
        }
    }
    return 1;
}
static int decode_noise(const uint8_t *data,size_t size,size_t *pos,
                        const SautiAacSbrGrid *grid,const SautiAacSbrTables *tables,
                        const SautiAacSbrControl *control,const SautiAacSbrQuantState *state,
                        const QuantMode *mode,SautiAacSbrQuantFrame *next){
    for(unsigned e=0u;e<grid->noise_count;e++){
        const unsigned n=tables->n_noise;
        if(control->df_noise[e]){
            if(e==0u && (!state->has_noise || !mode->geometry_ok || state->balanced!=(mode->factor==2u)))return 0;
            const uint8_t *prev=e?next->noise[e-1u]:state->prev_noise;
            for(unsigned j=0u;j<n;j++){
                int32_t delta=0;if(!sauti_aac_sbr_huffman_decode(data,size,pos,mode->noise_t,&delta))return 0;
                const int value=(int)prev[j]+(int)mode->factor*(int)delta;
                if(value<0 || value>30)return 0;
                next->noise[e][j]=(uint8_t)value;
            }
        }else{
            uint32_t first=0u;if(!read_bits(data,size,pos,5u,&first))return 0;
            const unsigned initial=first*mode->factor;if(initial>30u)return 0;
            next->noise[e][0]=(uint8_t)initial;
            for(unsigned j=1u;j<n;j++){
                int32_t delta=0;if(!sauti_aac_sbr_huffman_decode(data,size,pos,mode->noise_f,&delta))return 0;
                const int value=(int)next->noise[e][j-1u]+(int)mode->factor*(int)delta;
                if(value<0 || value>30)return 0;
                next->noise[e][j]=(uint8_t)value;
            }
        }
    }
    return 1;
}
static void quant_commit(SautiAacSbrQuantState *state,const SautiAacSbrTables *tables,
                         const SautiAacSbrGrid *grid,const QuantMode *mode,
                         unsigned balanced,const SautiAacSbrQuantFrame *next){
    SautiAacSbrQuantState history=*state;
    history.has_env=1u;history.has_noise=1u;
    history.prev_freq_res=grid->freq_res[grid->env_count-1u];
    history.amp_res=mode->effective_amp;history.balanced=balanced;history.prev_tables=*tables;
    memset(history.prev_env,0,sizeof(history.prev_env));
    memcpy(history.prev_env,next->env[grid->env_count-1u],
           history.prev_freq_res?tables->n_high:tables->n_low);
    memset(history.prev_noise,0,sizeof(history.prev_noise));
    memcpy(history.prev_noise,next->noise[grid->noise_count-1u],tables->n_noise);
    *state=history;
}

int sauti_aac_sbr_quant_decode(const uint8_t *data, size_t size,
                                size_t *bit_offset,
                                const SautiAacSbrGrid *grid,
                                const SautiAacSbrTables *tables,
                                const SautiAacSbrControl *control,
                                unsigned amp_res, unsigned balanced,
                                SautiAacSbrQuantState *state,
                                SautiAacSbrQuantFrame *out) {
    if (!data || !bit_offset || !state || !out ||
        size>SIZE_MAX/8u || *bit_offset>size*8u ||
        !validate(grid,tables,control,state,amp_res,balanced))
        return 0;

    size_t pos=*bit_offset;
    SautiAacSbrQuantFrame next;
    memset(&next,0,sizeof(next));
    next.env_count=grid->env_count;
    next.noise_count=grid->noise_count;
    QuantMode mode;quant_mode(grid,tables,state,amp_res,balanced,&mode);
    if(!decode_envelopes(data,size,&pos,grid,tables,control,state,&mode,&next) ||
       !decode_noise(data,size,&pos,grid,tables,control,state,&mode,&next))return 0;
    quant_commit(state,tables,grid,&mode,balanced,&next);
    *out=next;
    *bit_offset=pos;
    return 1;
}

int sauti_aac_sbr_quant_decode_cpe_uncoupled(
    const uint8_t *data,size_t size,size_t *bit_offset,
    const SautiAacSbrGrid grid[2],const SautiAacSbrTables *tables,
    const SautiAacSbrControl control[2],unsigned amp_res,
    SautiAacSbrQuantState state[2],SautiAacSbrQuantFrame out[2]){
    if(!data || !bit_offset || !grid || !tables || !control || !state || !out ||
       size>SIZE_MAX/8u || *bit_offset>size*8u ||
       !validate(&grid[0],tables,&control[0],&state[0],amp_res,0u) ||
       !validate(&grid[1],tables,&control[1],&state[1],amp_res,0u))return 0;
    size_t pos=*bit_offset;SautiAacSbrQuantState next_state[2]={state[0],state[1]};
    SautiAacSbrQuantFrame next[2]={{0}};QuantMode mode[2];
    for(unsigned ch=0u;ch<2u;ch++){
        next[ch].env_count=grid[ch].env_count;next[ch].noise_count=grid[ch].noise_count;
        quant_mode(&grid[ch],tables,&state[ch],amp_res,0u,&mode[ch]);
        if(!decode_envelopes(data,size,&pos,&grid[ch],tables,&control[ch],&state[ch],&mode[ch],&next[ch]))return 0;
    }
    for(unsigned ch=0u;ch<2u;ch++)
        if(!decode_noise(data,size,&pos,&grid[ch],tables,&control[ch],&state[ch],&mode[ch],&next[ch]))return 0;
    for(unsigned ch=0u;ch<2u;ch++)quant_commit(&next_state[ch],tables,&grid[ch],&mode[ch],0u,&next[ch]);
    state[0]=next_state[0];state[1]=next_state[1];out[0]=next[0];out[1]=next[1];*bit_offset=pos;
    return 1;
}
