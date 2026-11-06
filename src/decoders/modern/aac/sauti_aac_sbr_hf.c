/* HE-AAC SBR-12: elementary complex-QMF high-frequency transposition.
 * This is only the source-bin term in the normative HF generator;
 * predictor/chirp/envelope/QMF synthesis will be added separately.
 * All updates to caller-owned output are atomic. */
#include "sauti_aac_sbr_hf.h"
#include <math.h>
#include <string.h>

int sauti_aac_sbr_hf_map_valid(const SautiAacSbrPatches *p,
                     const SautiAacSbrTables *t) {
    if(!p || !t || p->count<1u ||
       p->count>SAUTI_AAC_SBR_MAX_PATCHES ||
       t->kx<1u || t->kx>SAUTI_AAC_SBR_HF_LOW_BANDS ||
       t->m<1u || t->m>SAUTI_AAC_SBR_HF_HIGH_BANDS-t->kx ||
       t->n_high<1u || t->n_high>SAUTI_AAC_SBR_MAX_MASTER_BANDS ||
       t->n_low!=(t->n_high+1u)/2u ||
       t->n_noise<1u || t->n_noise>SAUTI_AAC_SBR_MAX_NOISE_BANDS ||
       t->n_noise>t->n_low ||
       t->high[0]!=t->kx || t->high[t->n_high]!=t->kx+t->m ||
       t->low[0]!=t->kx || t->low[t->n_low]!=t->kx+t->m ||
       t->noise[0]!=t->kx || t->noise[t->n_noise]!=t->kx+t->m)
        return 0;
    for(unsigned j=1u;j<=t->n_high;j++)
        if(t->high[j]<=t->high[j-1u])return 0;
    const unsigned odd=t->n_high&1u;
    for(unsigned j=0;j<=t->n_low;j++) {
        const unsigned idx=j?2u*j-odd:0u;
        if(idx>t->n_high || t->low[j]!=t->high[idx])return 0;
    }
    for(unsigned j=1u;j<=t->n_noise;j++) {
        if(t->noise[j]<=t->noise[j-1u])return 0;
        int found=0;
        for(unsigned k=1u;k<=t->n_low;k++)
            if(t->noise[j]==t->low[k])found=1;
        if(!found)return 0;
    }

    /* Compare the compact patch intervals to the independent mapping.
     * In particular, reject any source that could escape low[32] or
     * any attempted patch into unrelated low or high frequency bins. */
    int8_t expected[SAUTI_AAC_SBR_HF_HIGH_BANDS];
    memset(expected,0xff,sizeof(expected));
    unsigned end=t->kx;
    for(unsigned pidx=0;pidx<p->count;pidx++) {
        const unsigned source=p->start[pidx];
        const unsigned target=p->target[pidx];
        const unsigned width=p->width[pidx];
        if(width==0u || target!=end || target<t->kx ||
           target+width>t->kx+t->m ||
           source>=t->kx || source+width>t->kx ||
           source+width>SAUTI_AAC_SBR_HF_LOW_BANDS)
            return 0;
        for(unsigned b=0;b<width;b++)
            expected[target+b]=(int8_t)(source+b);
        end=target+width;
    }
    /* At most two final subbands may be omitted by the SBR-11
     * last-patch-shorter-than-three rule. */
    if(t->kx+t->m-end>2u)return 0;
    for(unsigned k=0;k<SAUTI_AAC_SBR_HF_HIGH_BANDS;k++)
        if(expected[k]!=p->source_for_target[k])return 0;
    return 1;
}

int sauti_aac_sbr_hf_transpose(const SautiAacSbrPatches *patches,
                                const SautiAacSbrTables *tables,
                                const SautiAacSbrComplex *low,
                                size_t low_count,
                                unsigned slots,
                                SautiAacSbrComplex *high,
                                size_t high_count) {
    if(!low || !high || slots<1u ||
       slots>SAUTI_AAC_SBR_HF_MAX_SLOTS ||
       low_count<(size_t)slots*SAUTI_AAC_SBR_HF_LOW_BANDS ||
       high_count<(size_t)slots*SAUTI_AAC_SBR_HF_HIGH_BANDS ||
       !sauti_aac_sbr_hf_map_valid(patches,tables))return 0;

    /* A bounded local staging buffer means that malformed input can
     * never partially corrupt the output, and aliasing is safe. */
    SautiAacSbrComplex staged[
        SAUTI_AAC_SBR_HF_MAX_SLOTS*SAUTI_AAC_SBR_HF_HIGH_BANDS];
    memset(staged,0,(size_t)slots*SAUTI_AAC_SBR_HF_HIGH_BANDS*
                    sizeof(staged[0]));

    for(unsigned time=0u;time<slots;time++) {
        for(unsigned k=tables->kx;k<tables->kx+tables->m;k++) {
            const int src=patches->source_for_target[k];
            if(src<0)continue; /* an intentionally omitted short tail */
            const SautiAacSbrComplex x=
                low[(size_t)time*SAUTI_AAC_SBR_HF_LOW_BANDS+(unsigned)src];
            if(!isfinite(x.re) || !isfinite(x.im))return 0;
            staged[(size_t)time*SAUTI_AAC_SBR_HF_HIGH_BANDS+k]=x;
        }
    }
    memcpy(high,staged,(size_t)slots*SAUTI_AAC_SBR_HF_HIGH_BANDS*
                       sizeof(staged[0]));
    return 1;
}
