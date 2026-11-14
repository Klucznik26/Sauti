/* Differential test of the actual SSE2 and AVX2 64-band synthesis kernels.
 * The existing public QMF tests separately exercise transactional rollback,
 * history and a high-precision mathematical oracle. */
#include "sauti_aac_sbr_qmf_synth_mod.h"
#include "sauti_aac_sbr_qmf_synth_window.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#if defined(__x86_64__) && !defined(_WIN32) && \
    (defined(__GNUC__) || defined(__clang__))
static void test_modulation(void) {
    SautiAacSbrComplex bands[64];
    float baseline[130], optimized[130];
    for (unsigned iteration=0; iteration<48; ++iteration) {
        for (unsigned k=0; k<64; ++k) {
            int r=(int)((iteration*19U+k*13U)%71U)-35;
            int i=(int)((iteration*7U+k*11U)%67U)-33;
            bands[k].re=(float)r*0.0625f;
            bands[k].im=(float)i*0.03125f;
        }
        if (iteration<4) {
            memset(bands,0,sizeof(bands));
            bands[iteration*17U].re=1.0f;
            bands[iteration*17U].im=(iteration&1U)?1.0f:0.0f;
        }
        baseline[128]=optimized[128]=12345.0f;
        baseline[129]=optimized[129]=-23456.0f;
        sauti_aac_sbr_qmf_synth_mod_slot_asm(baseline,bands);
        sauti_aac_sbr_qmf_synth_mod_slot_avx2(optimized,bands);
        assert(baseline[128]==optimized[128] && baseline[129]==optimized[129]);
        assert(baseline[128]==12345.0f && baseline[129]==-23456.0f);
        for (unsigned n=0;n<128;n++) {
            double a=baseline[n], b=optimized[n];
            assert(isfinite(a) && isfinite(b));
            /* AVX2 sums the same 64 float products in wider groups.
             * Different summation order permits limited roundoff. */
            assert(fabs(a-b)<=3e-6*fmax(1.0,fabs(a)));
        }
    }
    puts("PASS QMF AVX2 modulation vs SSE2 (48 input sets, 128 samples)");
}

static void test_polyphase(void) {
    /* Prefix offsets exercise unaligned (but float-aligned) buffers and
     * guard samples. The assembly must use VMOVUPS, never VMOVAPS here. */
    float coeff_storage[641], history[10][65];
    float sse_storage[67], avx_storage[67], unrolled_storage[67];
    float *sse=sse_storage+1, *avx=avx_storage+1;
    float *unrolled=unrolled_storage+1;
    const float *ptrs[10];
    for (unsigned ix=0;ix<640;ix++)
        assert(sauti_aac_sbr_qmf_synth_window_coefficient(ix,&coeff_storage[ix+1]));
    for (unsigned j=0;j<10;j++) ptrs[j]=history[j]+1;
    for (unsigned iteration=0;iteration<256;iteration++) {
        for (unsigned j=0;j<10;j++)
            for (unsigned n=0;n<64;n++) {
                int q=(int)((iteration*23U+j*17U+n*5U)%83U)-41;
                history[j][n+1]=(float)q*0.0625f;
            }
        sse[-1]=avx[-1]=unrolled[-1]=6789.0f;
        sse[64]=avx[64]=unrolled[64]=12345.0f;
        sse[65]=avx[65]=unrolled[65]=-23456.0f;
        if(iteration==0) fprintf(stderr,"window: SSE2 start\n");
        sauti_aac_sbr_qmf_synth_window_slot_asm(sse,coeff_storage+1,ptrs);
        if(iteration==0) fprintf(stderr,"window: SSE2 done\n");
        if(iteration==0) fprintf(stderr,"window: AVX2 loop start\n");
        sauti_aac_sbr_qmf_synth_window_slot_avx2(avx,coeff_storage+1,ptrs);
        if(iteration==0) fprintf(stderr,"window: AVX2 loop done\n");
        if(iteration==0) fprintf(stderr,"window: AVX2 unrolled start\n");
        sauti_aac_sbr_qmf_synth_window_slot_avx2_unrolled(unrolled,coeff_storage+1,ptrs);
        if(iteration==0) fprintf(stderr,"window: AVX2 unrolled done\n");
        /* Identical floating-point operation order; no FMA in any path. */
        assert(memcmp(sse,avx,64*sizeof(float))==0);
        assert(memcmp(sse,unrolled,64*sizeof(float))==0);
        assert(sse[-1]==6789.0f && sse[64]==12345.0f && sse[65]==-23456.0f);
        assert(avx[-1]==6789.0f && avx[64]==12345.0f && avx[65]==-23456.0f);
        assert(unrolled[-1]==6789.0f && unrolled[64]==12345.0f && unrolled[65]==-23456.0f);
    }
    puts("PASS QMF AVX2 loop and unrolled polyphase vs SSE2 (256 unaligned sets, bit-identical)");
}
#endif

int main(void) {
#if defined(__x86_64__) && !defined(_WIN32) && \
    (defined(__GNUC__) || defined(__clang__))
    if (!__builtin_cpu_supports("avx2")) {
        puts("SKIP QMF AVX2: processor/OS does not support AVX2");
        return 0;
    }
    test_modulation();
    test_polyphase();
    puts("PASS SBR-74 QMF native SSE2/AVX2 differential tests");
#else
    puts("SKIP QMF SIMD: only System V x86-64 uses these NASM kernels");
#endif
    return 0;
}
