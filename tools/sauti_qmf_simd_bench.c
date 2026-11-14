/* Optional local microbenchmark; this measures QMF kernels, NOT full HE-AAC.
 * Run on the target CPU after verifying regression tests. */
#define _POSIX_C_SOURCE 200809L
#include "sauti_aac_sbr_qmf_synth_mod.h"
#include "sauti_aac_sbr_qmf_synth_window.h"
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#if defined(__x86_64__) && !defined(_WIN32) && \
    (defined(__GNUC__) || defined(__clang__))
static SautiAacSbrComplex g_bands[64];
static float g_coeff[640],g_hist[10][64],g_out[128];
static const float *g_ptrs[10];
static volatile float g_sink;
static uint64_t clock_ns(void){
    struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)!=0)return 0;
    return (uint64_t)t.tv_sec*1000000000ULL+(uint64_t)t.tv_nsec;
}
static double bench(int avx2,int modulation){
    const unsigned loops=modulation?4000U:30000U;
    uint64_t samples[7];
    for(unsigned rep=0;rep<7;rep++){
        uint64_t t0=clock_ns();
        for(unsigned i=0;i<loops;i++){
            if(modulation){
                if(avx2) sauti_aac_sbr_qmf_synth_mod_slot_avx2(g_out,g_bands);
                else sauti_aac_sbr_qmf_synth_mod_slot_asm(g_out,g_bands);
            }else{
                if(avx2==2) sauti_aac_sbr_qmf_synth_window_slot_avx2_unrolled(g_out,g_coeff,g_ptrs);
                else if(avx2==1) sauti_aac_sbr_qmf_synth_window_slot_avx2(g_out,g_coeff,g_ptrs);
                else sauti_aac_sbr_qmf_synth_window_slot_asm(g_out,g_coeff,g_ptrs);
            }
        }
        uint64_t end=clock_ns();
        samples[rep]=end>=t0?end-t0:0ULL;
    }
    for(unsigned i=1;i<7;i++){
        uint64_t v=samples[i];unsigned j=i;
        while(j>0 && samples[j-1]>v){samples[j]=samples[j-1];j--;}
        samples[j]=v;
    }
    g_sink=g_out[0];
    return (double)samples[3]/(double)loops;
}
#endif

int main(void){
#if defined(__x86_64__) && !defined(_WIN32) && \
    (defined(__GNUC__) || defined(__clang__))
    if(!__builtin_cpu_supports("avx2")){
        puts("SKIP: AVX2/OSXSAVE unavailable");return 0;
    }
    for(unsigned k=0;k<64;k++){
        g_bands[k].re=(float)((int)(k%17)-8)*0.125f;
        g_bands[k].im=(float)((int)(k%13)-6)*0.0625f;
    }
    for(unsigned k=0;k<640;k++)
        if(!sauti_aac_sbr_qmf_synth_window_coefficient(k,&g_coeff[k]))return 1;
    for(unsigned j=0;j<10;j++){
        g_ptrs[j]=g_hist[j];
        for(unsigned n=0;n<64;n++)
            g_hist[j][n]=(float)((int)((j*11+n*7)%31)-15)*0.125f;
    }
    double m_sse=bench(0,1),m_avx=bench(1,1);
    double w_sse=bench(0,0),w_avx=bench(1,0),w_unrolled=bench(2,0);
    printf("QMF modulation: SSE2 %.1f ns/slot, AVX2 %.1f ns/slot; %.2fx\n",
        m_sse,m_avx,m_avx>0?m_sse/m_avx:0.0);
    printf("QMF window:     SSE2 %.1f ns, AVX2 loop %.1f ns, AVX2 unrolled %.1f ns per slot\n",
        w_sse,w_avx,w_unrolled);
    printf("Window A/B: AVX2 loop %.2fx SSE2; unrolled %.2fx AVX2 loop (experimental)\n",
        w_avx>0?w_sse/w_avx:0.0,w_unrolled>0?w_avx/w_unrolled:0.0);
    puts("Median of seven batches. Microbenchmark only; verify full decoding separately.");
#else
    puts("SKIP QMF SIMD benchmark: System V x86-64 only");
#endif
    return 0;
}
