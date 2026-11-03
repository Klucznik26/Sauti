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

    const unsigned effective_amp=
        (grid->frame_class==0u && grid->env_count==1u) ? 0u : amp_res;
    const unsigned factor=balanced?2u:1u;
    const unsigned bits=effective_amp?(balanced?5u:6u):(balanced?6u:7u);
    const SautiAacSbrHuffmanBook env_t=
        (SautiAacSbrHuffmanBook)(
        (effective_amp?SAUTI_SBR_HUFF_ENV_3_0DB_T:
                       SAUTI_SBR_HUFF_ENV_1_5DB_T)+(balanced?2u:0u));
    const SautiAacSbrHuffmanBook env_f=
        (SautiAacSbrHuffmanBook)(env_t+1u);
    const SautiAacSbrHuffmanBook noise_t=balanced
        ? SAUTI_SBR_HUFF_NOISE_BAL_3_0DB_T
        : SAUTI_SBR_HUFF_NOISE_3_0DB_T;
    const SautiAacSbrHuffmanBook noise_f=balanced
        ? SAUTI_SBR_HUFF_ENV_BAL_3_0DB_F
        : SAUTI_SBR_HUFF_ENV_3_0DB_F;
    const int geometry_ok=tables_match(tables,&state->prev_tables);
    const int mode_ok=state->balanced==balanced &&
                      state->amp_res==effective_amp;
    size_t pos=*bit_offset;
    SautiAacSbrQuantFrame next;
    SautiAacSbrQuantState history=*state;
    memset(&next,0,sizeof(next));
    next.env_count=grid->env_count;
    next.noise_count=grid->noise_count;

    for(unsigned e=0;e<grid->env_count;e++) {
        const unsigned res=grid->freq_res[e];
        const unsigned n=res?tables->n_high:tables->n_low;
        next.freq_res[e]=(uint8_t)res;
        if(control->df_env[e]) {
            if(e==0u && (!state->has_env || !geometry_ok || !mode_ok))
                return 0;
            const unsigned prev_res=e ? grid->freq_res[e-1u]:
                                              state->prev_freq_res;
            const uint8_t *prev=e?next.env[e-1u]:state->prev_env;
            const unsigned prev_n=prev_res?tables->n_high:tables->n_low;
            for(unsigned j=0;j<n;j++) {
                unsigned ref=previous_band(j,res,prev_res,tables->n_high);
                int32_t delta=0;
                if(ref>=prev_n ||
                   !sauti_aac_sbr_huffman_decode(data,size,&pos,
                                                  env_t,&delta))
                    return 0;
                const int value=(int)prev[ref]+(int)factor*(int)delta;
                if(value<0 || value>127)return 0;
                next.env[e][j]=(uint8_t)value;
            }
        } else {
            uint32_t start=0;
            if(!read_bits(data,size,&pos,bits,&start))return 0;
            unsigned initial=start*factor;
            if(initial>127u)return 0;
            next.env[e][0]=(uint8_t)initial;
            for(unsigned j=1;j<n;j++) {
                int32_t delta=0;
                if(!sauti_aac_sbr_huffman_decode(data,size,&pos,
                                                  env_f,&delta))
                    return 0;
                int value=(int)next.env[e][j-1u]+(int)factor*(int)delta;
                if(value<0 || value>127)return 0;
                next.env[e][j]=(uint8_t)value;
            }
        }
    }

    for(unsigned e=0;e<grid->noise_count;e++) {
        const unsigned n=tables->n_noise;
        if(control->df_noise[e]) {
            if(e==0u && (!state->has_noise || !geometry_ok ||
                         state->balanced!=balanced))return 0;
            const uint8_t *prev=e?next.noise[e-1u]:state->prev_noise;
            for(unsigned j=0;j<n;j++) {
                int32_t delta=0;
                if(!sauti_aac_sbr_huffman_decode(data,size,&pos,
                                                  noise_t,&delta))
                    return 0;
                const int value=(int)prev[j]+(int)factor*(int)delta;
                if(value<0 || value>30)return 0;
                next.noise[e][j]=(uint8_t)value;
            }
        } else {
            uint32_t first=0;
            if(!read_bits(data,size,&pos,5u,&first))return 0;
            const unsigned initial=first*factor;
            if(initial>30u)return 0;
            next.noise[e][0]=(uint8_t)initial;
            for(unsigned j=1;j<n;j++) {
                int32_t delta=0;
                if(!sauti_aac_sbr_huffman_decode(data,size,&pos,
                                                  noise_f,&delta))
                    return 0;
                int value=(int)next.noise[e][j-1u]+(int)factor*(int)delta;
                if(value<0 || value>30)return 0;
                next.noise[e][j]=(uint8_t)value;
            }
        }
    }

    history.has_env=1u;
    history.has_noise=1u;
    history.prev_freq_res=grid->freq_res[grid->env_count-1u];
    history.amp_res=effective_amp;
    history.balanced=balanced;
    history.prev_tables=*tables;
    memset(history.prev_env,0,sizeof(history.prev_env));
    memcpy(history.prev_env,next.env[grid->env_count-1u],
           (history.prev_freq_res?tables->n_high:tables->n_low));
    memset(history.prev_noise,0,sizeof(history.prev_noise));
    memcpy(history.prev_noise,next.noise[grid->noise_count-1u],
           tables->n_noise);
    *state=history;
    *out=next;
    *bit_offset=pos;
    return 1;
}
