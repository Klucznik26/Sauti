/* HE-AAC SBR-26: mathematical (non-FFT) complex QMF synthesis
 * modulation and atomic connection to SBR-25 polyphase window.
 *
 * Reference kernel: Real part of complex synthesis-QMF modulation:
 *   V[n] = -(1/64) * sum_{k=0..63} (
 *             Re(X[k])*cos(theta) - Im(X[k])*sin(theta)),
 *   theta=pi*(k+1/2)*(2*n+1)/128, n=0..127.
 *
 * The direct kernel is deliberately simple to test independently
 * before an optimized DCT-IV/MDCT/ASM path is added. The following
 * window stage is exactly SBR-25 and retains ten modulation blocks.
 */
#include "sauti_aac_sbr_qmf_synth_mod.h"
#include "sauti_aac_qmf_dispatch.h"
#include <float.h>
#include <math.h>
#include <string.h>

#define PI 3.14159265358979323846264338327950288

int sauti_aac_sbr_qmf_synth_modulate_64(
    const SautiAacSbrComplex *bands,
    size_t bands_count,
    unsigned slots,
    float *modulation,
    size_t modulation_count){
    if(!bands || !modulation ||
       slots<1u || slots>SAUTI_AAC_SBR_QMF_MOD_MAX_SLOTS ||
       bands_count<(size_t)slots*SAUTI_AAC_SBR_QMF_MOD_BANDS ||
       modulation_count<(size_t)slots*SAUTI_AAC_SBR_QMF_MOD_VALUES)
        return 0;

    /* Check all inputs first, including a NaN in the LAST complex bin.
     * No partial overwrite can occur, including when buffers alias. */
    for(unsigned t=0;t<slots;t++)
        for(unsigned k=0;k<SAUTI_AAC_SBR_QMF_MOD_BANDS;k++){
            const SautiAacSbrComplex x=
                bands[(size_t)t*SAUTI_AAC_SBR_QMF_MOD_BANDS+k];
            if(!isfinite(x.re) || !isfinite(x.im))return 0;
        }

    float stage[SAUTI_AAC_SBR_QMF_MOD_MAX_SLOTS*
                SAUTI_AAC_SBR_QMF_MOD_VALUES];
    /* Select SIMD once per batch. The pure policy prefers AVX2 on
     * Intel hybrid P-cores and SSE2 on E-cores; on homogeneous CPUs the
     * existing AVX2-capability dispatch is preserved. */
#if SAUTI_AAC_QMF_NATIVE_ASM
    const int use_avx2=sauti_aac_qmf_use_avx2();
#endif
    for(unsigned t=0;t<slots;t++){
        float *slot_out=stage+(size_t)t*SAUTI_AAC_SBR_QMF_MOD_VALUES;
#if SAUTI_AAC_QMF_NATIVE_ASM
        const SautiAacSbrComplex *slot_bands=bands+(size_t)t*SAUTI_AAC_SBR_QMF_MOD_BANDS;
        if(use_avx2)
            sauti_aac_sbr_qmf_synth_mod_slot_avx2(slot_out,slot_bands);
        else
            sauti_aac_sbr_qmf_synth_mod_slot_asm(slot_out,slot_bands);
#else
        for(unsigned n=0;n<SAUTI_AAC_SBR_QMF_MOD_VALUES;n++){
            const double offset=(double)(2u*n)+1.0;
            double sum=0.0;
            for(unsigned k=0;k<SAUTI_AAC_SBR_QMF_MOD_BANDS;k++){
                const SautiAacSbrComplex x=
                    bands[(size_t)t*SAUTI_AAC_SBR_QMF_MOD_BANDS+k];
                const double theta=(PI/128.0)*
                                   ((double)k+0.5)*offset;
                sum+=(double)x.re*cos(theta)-
                     (double)x.im*sin(theta);
            }
            slot_out[n]=(float)(-sum/64.0);
        }
#endif
        for(unsigned n=0;n<SAUTI_AAC_SBR_QMF_MOD_VALUES;n++){
            if(!isfinite(slot_out[n]) || fabsf(slot_out[n])>FLT_MAX)return 0;
        }
    }
    memcpy(modulation,stage,
        (size_t)slots*SAUTI_AAC_SBR_QMF_MOD_VALUES*sizeof(float));
    return 1;
}

int sauti_aac_sbr_qmf_synth_64(
    const SautiAacSbrComplex *bands,
    size_t bands_count,
    unsigned slots,
    float *output,
    size_t output_count,
    SautiAacSbrQmfSynthWindowState *state){
    if(!bands || !output || !state ||
       slots<1u || slots>SAUTI_AAC_SBR_QMF_MOD_MAX_SLOTS ||
       bands_count<(size_t)slots*SAUTI_AAC_SBR_QMF_MOD_BANDS ||
       output_count<(size_t)slots*SAUTI_AAC_SBR_QMF_SYNTH_WINDOW_OUTPUT)
        return 0;

    float modulated[SAUTI_AAC_SBR_QMF_MOD_MAX_SLOTS*
                    SAUTI_AAC_SBR_QMF_MOD_VALUES];
    if(!sauti_aac_sbr_qmf_synth_modulate_64(
           bands,bands_count,slots,
           modulated,(size_t)slots*SAUTI_AAC_SBR_QMF_MOD_VALUES))
        return 0;

    float rendered[SAUTI_AAC_SBR_QMF_MOD_MAX_SLOTS*
                   SAUTI_AAC_SBR_QMF_SYNTH_WINDOW_OUTPUT];
    SautiAacSbrQmfSynthWindowState staged=*state;
    if(!sauti_aac_sbr_qmf_synth_window_64(
           modulated,(size_t)slots*SAUTI_AAC_SBR_QMF_MOD_VALUES,
           slots,rendered,
           (size_t)slots*SAUTI_AAC_SBR_QMF_SYNTH_WINDOW_OUTPUT,
           &staged))
        return 0;
    memcpy(output,rendered,
        (size_t)slots*SAUTI_AAC_SBR_QMF_SYNTH_WINDOW_OUTPUT*
        sizeof(float));
    *state=staged;
    return 1;
}

/* ---- 32-band synthesis for SBR at the core rate ("downsampled" SBR, ISO/IEC 14496-3 4.6.18.4): the same structure as the 64-band bank on half the size.  The modulation has 64 values per slot,
 * V[n] = -(1/64) Re(sum(k = 0..31, X[k] exp(+i pi (k + 0.5)(2n + 1) / 64))), the window is the 640-coefficient prototype taken at every second tap (c[2i]), 32 output samples per slot. */
int sauti_aac_sbr_qmf_synth_32(const SautiAacSbrComplex *bands,size_t stride,unsigned slots,float *output,SautiAacSbrQmfSynthWindowState *state){
    if(!bands||!output||!state||slots<1u||slots>SAUTI_AAC_SBR_QMF_MOD_MAX_SLOTS||stride<32u||state->ready>1u||state->head>=10u)return 0;
    static double cs[32][64],sn[32][64];
    static int init;
    if(!init){
        for(unsigned k=0;k<32u;k++)for(unsigned n=0;n<64u;n++){
            const double ph=3.14159265358979323846*((double)k+0.5)*(double)(2u*n+1u)/64.0;
            cs[k][n]=cos(ph);sn[k][n]=sin(ph);
        }
        init=1;
    }
    SautiAacSbrQmfSynthWindowState next=*state;
    float stage[SAUTI_AAC_SBR_QMF_MOD_MAX_SLOTS*32u];
    for(unsigned t=0;t<slots;t++){
        float v[64];
        for(unsigned n=0;n<64u;n++){
            double sum=0.0;
            for(unsigned k=0;k<32u;k++){
                const SautiAacSbrComplex *x=&bands[(size_t)t*stride+k];
                if(!isfinite(x->re)||!isfinite(x->im))return 0;
                sum+=(double)x->re*cs[k][n]-(double)x->im*sn[k][n];
            }
            v[n]=(float)(-sum/64.0);
        }
        if(next.ready)next.head=(next.head+9u)%10u;else{next.ready=1u;next.head=0u;}
        memcpy(next.history[next.head],v,sizeof v);
        for(unsigned n=0;n<32u;n++){
            double sum=0.0;
            for(unsigned j=0;j<10u;j++){
                float c;
                const unsigned ix=(next.head+j)%10u;
                if(!sauti_aac_sbr_qmf_synth_window_coefficient(2u*(j*32u+n),&c))return 0;
                sum+=(double)c*(double)next.history[ix][(j&1u)?32u+n:n];
            }
            stage[(size_t)t*32u+n]=(float)sum;
        }
    }
    memcpy(output,stage,(size_t)slots*32u*sizeof(float));
    *state=next;
    return 1;
}
