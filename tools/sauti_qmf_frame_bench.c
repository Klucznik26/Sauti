/* SBR-74 40-slot QMF frame benchmark.
 * Measures public SBR-26 complex modulation + SBR-25 polyphase window,
 * including validation, staging, state/rollback and runtime dispatch.
 * This is NOT a benchmark of the complete HE-AAC decoder.
 *
 * Compile twice: native dispatch and -DSAUTI_QMF_FORCE_SSE2.
 * A separate process per variant avoids cross-variant CPU-state effects.
 */
#define _POSIX_C_SOURCE 200809L
#include "sauti_aac_sbr_qmf_synth_mod.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

enum { FRAMES = 7, WARMUP = 32, ROUNDS = 300 };

static SautiAacSbrComplex bands[SAUTI_AAC_SBR_QMF_MOD_MAX_SLOTS*
                                SAUTI_AAC_SBR_QMF_MOD_BANDS];
static float pcm[SAUTI_AAC_SBR_QMF_MOD_MAX_SLOTS*
                 SAUTI_AAC_SBR_QMF_SYNTH_WINDOW_OUTPUT];
static SautiAacSbrQmfSynthWindowState state;
static volatile double sink;

static uint64_t monotonic_ns(void) {
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return 0;
    return (uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
}

static int process_frame(void) {
    return sauti_aac_sbr_qmf_synth_64(
        bands,SAUTI_AAC_SBR_QMF_MOD_MAX_SLOTS*
              SAUTI_AAC_SBR_QMF_MOD_BANDS,
        SAUTI_AAC_SBR_QMF_MOD_MAX_SLOTS,
        pcm,SAUTI_AAC_SBR_QMF_MOD_MAX_SLOTS*
            SAUTI_AAC_SBR_QMF_SYNTH_WINDOW_OUTPUT,&state);
}

int main(void) {
    for(unsigned t=0;t<SAUTI_AAC_SBR_QMF_MOD_MAX_SLOTS;t++)
        for(unsigned k=0;k<SAUTI_AAC_SBR_QMF_MOD_BANDS;k++) {
            const size_t i=(size_t)t*SAUTI_AAC_SBR_QMF_MOD_BANDS+k;
            const int re=(int)((t*19u+k*13u)%71u)-35;
            const int im=(int)((t*7u+k*11u)%67u)-33;
            bands[i].re=(float)re*0.0625f;
            bands[i].im=(float)im*0.03125f;
        }

    for(unsigned n=0;n<WARMUP;n++)
        if(!process_frame())return 2;

    uint64_t samples[FRAMES];
    for(unsigned rep=0;rep<FRAMES;rep++) {
        uint64_t start=monotonic_ns();
        if(!start)return 3;
        for(unsigned n=0;n<ROUNDS;n++)
            if(!process_frame())return 4;
        uint64_t end=monotonic_ns();
        if(end<start)return 5;
        samples[rep]=end-start;
    }
    for(unsigned i=1;i<FRAMES;i++){
        uint64_t value=samples[i];unsigned j=i;
        while(j>0&&samples[j-1]>value){samples[j]=samples[j-1];--j;}
        samples[j]=value;
    }
    /* Read result outside timing to prevent dead-code elimination and
     * catch accidental nonfinite outputs. */
    double checksum=0.0;
    for(unsigned i=0;i<sizeof(pcm)/sizeof(pcm[0]);i++){
        if(!isfinite(pcm[i]))return 6;
        checksum+=(double)pcm[i]*(double)((i%17u)+1u);
    }
    sink=checksum;
#if defined(SAUTI_QMF_FORCE_SSE2)
    const char *mode="forced-sse2";
#elif defined(SAUTI_QMF_DISABLE_ASM)
    const char *mode="c11";
#else
    const char *mode="auto";
#endif
    printf("QMF frame 40 slots %s: median %.1f us/frame, pmax %.1f us/frame, checksum %.9g\n",
        mode, (double)samples[FRAMES/2]/(double)ROUNDS/1000.0,
        (double)samples[FRAMES-1]/(double)ROUNDS/1000.0,checksum);
    return 0;
}
