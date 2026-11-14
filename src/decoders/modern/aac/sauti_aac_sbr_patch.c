/* HE-AAC v1 SBR-11: construct high-frequency transposition patches.
 * Based on the MPEG-4 SBR patch-construction recurrence (4.46),
 * independently implemented with strict bounds/rollback in Sauti.
 * Only frequency indices: no external decoder, QMF, gain or PCM. */
#include "sauti_aac_sbr_patch.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int output_rate_supported(unsigned rate) {
    switch(rate) {
    case 16000u: case 22050u: case 24000u: case 32000u:
    case 44100u: case 48000u: case 64000u: case 88200u:
    case 96000u: case 128000u: case 176400u: case 192000u:
        return 1;
    default: return 0;
    }
}

static int inputs_valid(unsigned sample_rate,
                        const SautiAacSbrMaster *master,
                        const SautiAacSbrTables *tables) {
    if (!master || !tables || !output_rate_supported(sample_rate) ||
        master->count < 2u || master->count > 48u ||
        (master->count & 1u) ||
        master->crossover_band >= master->count ||
        master->crossover_band > 7u ||
        master->k0 < 1u || master->k0 >= master->k2 ||
        master->k0 > 32u || master->k2 > SAUTI_AAC_SBR_QMF_BANDS ||
        master->edges[0] != master->k0 ||
        master->edges[master->count] != master->k2 ||
        master->kx != master->edges[master->crossover_band] ||
        tables->kx != master->kx ||
        tables->m != master->k2 - master->kx ||
        tables->kx < 1u || tables->kx > 32u ||
        tables->m < 1u ||
        tables->n_high != master->count - master->crossover_band ||
        tables->n_low != (tables->n_high+1u)/2u ||
        tables->n_noise < 1u || tables->n_noise > 5u ||
        tables->n_noise > tables->n_low ||
        tables->high[0] != master->kx ||
        tables->high[tables->n_high] != master->k2 ||
        tables->low[0] != master->kx ||
        tables->low[tables->n_low] != master->k2 ||
        tables->noise[0] != master->kx ||
        tables->noise[tables->n_noise] != master->k2)
        return 0;
    for (unsigned i=1u;i<=master->count;i++)
        if(master->edges[i]<=master->edges[i-1u])return 0;
    for(unsigned i=0u;i<=tables->n_high;i++)
        if(tables->high[i]!=master->edges[master->crossover_band+i])
            return 0;
    const unsigned odd=tables->n_high&1u;
    for(unsigned i=0u;i<=tables->n_low;i++) {
        const unsigned j=i?2u*i-odd:0u;
        if(j>tables->n_high || tables->low[i]!=tables->high[j])
            return 0;
    }
    for(unsigned i=1u;i<=tables->n_noise;i++) {
        unsigned match=0u;
        if(tables->noise[i]<=tables->noise[i-1u])return 0;
        for(unsigned j=1u;j<=tables->n_low;j++)
            if(tables->noise[i]==tables->low[j])match=1u;
        if(!match)return 0;
    }
    return 1;
}

int sauti_aac_sbr_patch_build(unsigned sample_rate,
                               const SautiAacSbrMaster *master,
                               const SautiAacSbrTables *tables,
                               SautiAacSbrPatches *out) {
    if(!out || !inputs_valid(sample_rate,master,tables))return 0;

    SautiAacSbrPatches next;
    memset(&next,0,sizeof(next));
    for(unsigned j=0u;j<SAUTI_AAC_SBR_QMF_BANDS;j++)
        next.source_for_target[j]=-1;

    const int k0=(int)master->k0, k2=(int)master->k2;
    const int kx=(int)master->kx;
    const int goal=(2048000+(int)sample_rate/2)/(int)sample_rate;
    int k=(int)master->count;
    if(goal<k2) {
        for(unsigned i=0u;i<=master->count;i++) {
            if((int)master->edges[i]>=goal) {k=(int)i;break;}
        }
    }

    int msb=k0, usb=kx, sb=-1;
    int last_k=-1,last_msb=-1;
    /* Non-progress or out-of-range returns 0 without publishing a map. */
    for(unsigned attempts=0u;attempts<16u;attempts++) {
        if(k==last_k && msb==last_msb)return 0;
        last_k=k;last_msb=msb;

        int odd=0;
        int idx=k;
        do {
            if(idx<0 || idx>(int)master->count)return 0;
            sb=(int)master->edges[(unsigned)idx];
            odd=(sb+k0)&1;
            --idx;
        } while(sb>k0-1+msb-odd);

        const int width=sb>usb?sb-usb:0;
        const int start=k0-odd-width;
        if(width>0) {
            if(next.count>=SAUTI_AAC_SBR_MAX_PATCHES ||
               width>64 || start<0 || start+width>k0 ||
               usb<kx || sb>k2 || usb+width>64)
                return 0;
            const unsigned p=next.count++;
            next.start[p]=(uint8_t)start;
            next.target[p]=(uint8_t)usb;
            next.width[p]=(uint8_t)width;
            for(int j=0;j<width;j++){
                const unsigned target=(unsigned)(usb+j);
                if(next.source_for_target[target]!=-1)return 0;
                next.source_for_target[target]=(int8_t)(start+j);
            }
            usb=sb;
            msb=sb;
        } else {
            msb=kx;
        }
        if((int)master->edges[(unsigned)k]-sb<3)
            k=(int)master->count;
        if(sb==k2) {
            /* Drop a final patch shorter than 3 QMF subbands. */
            if(next.count>1u && next.width[next.count-1u]<3u){
                const unsigned p=--next.count;
                for(unsigned j=0;j<next.width[p];j++)
                    next.source_for_target[next.target[p]+j]=-1;
                next.start[p]=next.target[p]=next.width[p]=0;
            }
            if(next.count==0u)return 0;
            *out=next;
            return 1;
        }
    }
    return 0;
}
