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
    /* CPUID + OSXSAVE check is performed by the compiler built-in; the
     * implementation does not execute AVX instructions before dispatch.
     * Evaluate once per frame, not for every 64-band time slot.
     * Compiling with SAUTI_QMF_FORCE_SSE2 selects the reference SIMD path. */
#if defined(__x86_64__) && !defined(_WIN32) && \
    (defined(__GNUC__) || defined(__clang__)) && !defined(SAUTI_QMF_DISABLE_ASM)
#if defined(SAUTI_QMF_FORCE_SSE2)
    const int use_avx2=0;
#else
    const int use_avx2=__builtin_cpu_supports("avx2")!=0;
#endif
#endif
    for(unsigned t=0;t<slots;t++){
        float *slot_out=stage+(size_t)t*SAUTI_AAC_SBR_QMF_MOD_VALUES;
#if defined(__x86_64__) && !defined(_WIN32) && \
    (defined(__GNUC__) || defined(__clang__)) && !defined(SAUTI_QMF_DISABLE_ASM)
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
