/* HE-AAC SBR-27 — direct 32-channel complex QMF analysis.
 * Input PCM is chronological in 32-sample hops, while the 320-sample
 * analysis history is newest-first (x[0] is newest PCM sample).
 * Polyphase window comes from SBR-25's 640-tap MPEG-4 prototype,
 * using every EVEN coefficient as specified for 32-band analysis.
 *
 * The explicit cosine/sine modulation is an independently testable
 * reference path before a more efficient DCT-IV/ASM implementation.
 * The sign, phase and factor of two follow MPEG-4 SBR Figure 4.42.
 * The complete AAC-LC -> SBR frame driver remains a separate stage.
 */
#include "sauti_aac_sbr_qmf_analysis.h"
#include "sauti_aac_sbr_qmf_synth_window.h"
#include <float.h>
#include <math.h>
#include <string.h>

#define SAUTI_PI 3.14159265358979323846264338327950288

static int state_valid(const SautiAacSbrQmfAnalysisState *state) {
    if(!state || state->ready>1u ||
       (!state->ready && state->samples_consumed!=0u) ||
       (state->samples_consumed%32u)!=0u)
        return 0;
    for(unsigned i=0;i<SAUTI_AAC_SBR_QMF_ANALYSIS_HISTORY;i++){
        const float v=state->history[i];
        if(!isfinite(v) || (!state->ready && v!=0.0f))
            return 0;
    }
    return 1;
}

int sauti_aac_sbr_qmf_analyze_32(
    const float *pcm,
    size_t pcm_count,
    unsigned slots,
    unsigned active_bands,
    SautiAacSbrComplex *output,
    size_t output_count,
    SautiAacSbrQmfAnalysisState *state) {
    if(!pcm || !output || !state_valid(state) ||
       slots<1u || slots>SAUTI_AAC_SBR_QMF_ANALYSIS_SLOTS ||
       active_bands<1u ||
       active_bands>SAUTI_AAC_SBR_QMF_ANALYSIS_BANDS ||
       pcm_count<(size_t)slots*SAUTI_AAC_SBR_QMF_ANALYSIS_INPUT ||
       output_count<(size_t)slots*SAUTI_AAC_SBR_QMF_ANALYSIS_BANDS ||
       state->samples_consumed>UINT64_MAX-(uint64_t)slots*32u)
        return 0;

    float taps[SAUTI_AAC_SBR_QMF_ANALYSIS_HISTORY];
    for(unsigned i=0;i<SAUTI_AAC_SBR_QMF_ANALYSIS_HISTORY;i++){
        if(!sauti_aac_sbr_qmf_synth_window_coefficient(
                2u*i,&taps[i]))return 0;
    }
    /* Compute the immutable modulation matrix once per call.
     * Real and imaginary parts are the real Fourier projection. */
    double phase_re[SAUTI_AAC_SBR_QMF_ANALYSIS_BANDS][64];
    double phase_im[SAUTI_AAC_SBR_QMF_ANALYSIS_BANDS][64];
    for(unsigned k=0;k<active_bands;k++)
        for(unsigned n=0;n<64u;n++){
            const double theta=(SAUTI_PI/64.0)*
                    ((double)k+0.5)*(2.0*(double)n-0.5);
            phase_re[k][n]=2.0*cos(theta);
            phase_im[k][n]=2.0*sin(theta);
        }

    SautiAacSbrQmfAnalysisState next=*state;
    SautiAacSbrComplex staged[
        SAUTI_AAC_SBR_QMF_ANALYSIS_SLOTS*
        SAUTI_AAC_SBR_QMF_ANALYSIS_BANDS];
    memset(staged,0,sizeof(staged));
    for(unsigned t=0;t<slots;t++) {
        const float *block=pcm+(size_t)t*32u;
        for(unsigned i=0;i<32u;i++)
            if(!isfinite(block[i]))return 0;
        memmove(next.history+32u,next.history,
                (320u-32u)*sizeof(float));
        for(unsigned i=0;i<32u;i++)
            next.history[31u-i]=block[i];
        next.ready=1u;

        double u[64];
        for(unsigned n=0;n<64u;n++){
            double sum=0.0;
            for(unsigned j=0;j<5u;j++){
                const unsigned index=n+64u*j;
                sum+=(double)next.history[index]*(double)taps[index];
            }
            if(!isfinite(sum))return 0;
            u[n]=sum;
        }
        for(unsigned k=0;k<active_bands;k++){
            double re=0.0,im=0.0;
            for(unsigned n=0;n<64u;n++){
                re+=u[n]*phase_re[k][n];
                im+=u[n]*phase_im[k][n];
            }
            if(!isfinite(re) || !isfinite(im) ||
               fabs(re)>FLT_MAX || fabs(im)>FLT_MAX)
                return 0;
            staged[(size_t)t*32u+k].re=(float)re;
            staged[(size_t)t*32u+k].im=(float)im;
        }
    }
    next.samples_consumed+=(uint64_t)slots*32u;
    memcpy(output,staged,(size_t)slots*32u*sizeof(staged[0]));
    *state=next;
    return 1;
}
