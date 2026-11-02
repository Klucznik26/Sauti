#include "sauti_aac_pns.h"
#include "sauti_aac_bands.h"
#include <math.h>
#include <string.h>

static uint32_t advance(uint32_t *seed){
    *seed=(*seed)*1664525u+1013904223u;
    return *seed;
}
int sauti_aac_pns_apply(const SautiAacIcsInfo *ics,
                        unsigned rate,const SautiAacSections *sec,
                        const SautiAacScaleFactors *sf,
                        uint32_t *seed,float spectral[1024]){
    if(!ics||!sec||!sf||!seed||!spectral||ics->window_sequence>3 ||
       ics->group_count==0 || ics->group_count>8 ||
       sec->section_count>SAUTI_AAC_MAX_SECTIONS ||
       ics->group_count!=sec->group_count ||
       ics->max_sfb!=sec->max_sfb)return 0;
    const unsigned short_win=ics->window_sequence==2u;
    if(ics->window_count!=(short_win?8u:1u))return 0;
    const uint16_t *offsets=NULL;unsigned bands=0;
    if(!sauti_aac_band_offsets(rate,short_win,&offsets,&bands) ||
       ics->max_sfb>bands)return 0;
    unsigned starts[8]={0},windows=0,expected_group=0,expected_band=0;
    for(unsigned g=0;g<ics->group_count;g++){
        starts[g]=windows;
        if(ics->group_length[g]==0 ||
           windows+ics->group_length[g]>ics->window_count)return 0;
        windows+=ics->group_length[g];
    }
    if(windows!=ics->window_count)return 0;
    for(unsigned k=0;k<sec->section_count;k++){
        const SautiAacSection *s=&sec->sections[k];
        if(s->group!=expected_group || s->first_sfb!=expected_band ||
           s->end_sfb<=s->first_sfb || s->end_sfb>ics->max_sfb ||
           s->codebook==12 || s->codebook>15)return 0;
        /* Intensity bands carry no PNS data. Stereo CPE reconstructs
         * these bands separately; mono synthesis still rejects them. */
        for(unsigned band=s->first_sfb;band<s->end_sfb;band++)
            if(sf->codebook[s->group][band]!=s->codebook)return 0;
        expected_band=s->end_sfb;
        if(expected_band==ics->max_sfb){expected_band=0;expected_group++;}
    }
    if(ics->max_sfb && expected_group!=ics->group_count)return 0;
    if(!ics->max_sfb && sec->section_count)return 0;
    float next[1024];memcpy(next,spectral,sizeof(next));
    for(unsigned i=0;i<1024;i++)if(!isfinite(next[i]))return 0;
    uint32_t rng=*seed;
    for(unsigned k=0;k<sec->section_count;k++){
        const SautiAacSection *s=&sec->sections[k];
        if(s->codebook!=13)continue;
        for(unsigned band=s->first_sfb;band<s->end_sfb;band++){
            unsigned from=offsets[band],to=offsets[band+1];
            if(to<=from || to>(short_win?128u:1024u))return 0;
            const int32_t scale=sf->value[s->group][band];
            if(scale< -512 || scale>512)return 0;
            /* NOISE_BT scalefactor is already an exponent offset from
             * global_gain-90. Unlike spectral codebooks, PNS does NOT
             * subtract the spectral reference offset 100.
             * FFmpeg float: sf = -pow2sf_tab[sfo + POW_SF2_ZERO]
             *             = -2^(sfo/4). */
            const double gain=exp2((double)scale/4.0);
            for(unsigned w=starts[s->group];
                w<starts[s->group]+ics->group_length[s->group];w++){
                unsigned base=w*(short_win?128u:1024u);
                double energy=0.0;
                for(unsigned i=from;i<to;i++){
                    /* Convert deterministic 32-bit noise to [-1,1). */
                    int32_t v=(int32_t)advance(&rng);
                    double sample=(double)v/2147483648.0;
                    next[base+i]=(float)sample;
                    energy+=sample*sample;
                }
                if(!(energy>0.0))return 0;
                /* Normalized noise has the signed sample direction
                 * of our LCG conversion. Compare against FFmpeg PCM,
                 * not just band energies: negating the band doubles
                 * the reconstruction residual on PNS-heavy frames. */
                const double factor=gain/sqrt(energy);
                for(unsigned i=from;i<to;i++){
                    double scaled=(double)next[base+i]*factor;
                    if(!isfinite(scaled) || fabs(scaled)>3.402823466e38)
                        return 0;
#ifdef SAUTI_AAC_DIAG_PNS_ZERO
                    next[base+i]=0.0f;
#else
                    next[base+i]=(float)scaled;
#endif
                }
            }
        }
    }
    memcpy(spectral,next,sizeof(next));
    *seed=rng;
    return 1;
}
