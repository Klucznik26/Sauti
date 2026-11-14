/* SBR-74: private x86-64 QMF SIMD selection policy.
 *
 * This header is intentionally independent of the platform UI and does not
 * require pthreads, Linux syscalls, affinity changes, or mutable caches.
 * CPUID 0x1A describes the logical processor executing the instruction.
 * Scheduler migration after the probe can only affect performance: SSE2
 * and AVX2 are both instruction-safe on the AVX2-capable Intel hybrids
 * supported by this policy.
 */
#ifndef SAUTI_AAC_QMF_DISPATCH_H
#define SAUTI_AAC_QMF_DISPATCH_H

#if defined(__x86_64__) && defined(__ELF__) && !defined(_WIN32) && \
    (defined(__GNUC__) || defined(__clang__)) && !defined(SAUTI_QMF_DISABLE_ASM)
#define SAUTI_AAC_QMF_NATIVE_ASM 1
#include <cpuid.h>
#else
#define SAUTI_AAC_QMF_NATIVE_ASM 0
#endif

#if defined(SAUTI_QMF_FORCE_SSE2) && defined(SAUTI_QMF_FORCE_AVX2)
#error "SAUTI_QMF_FORCE_SSE2 and SAUTI_QMF_FORCE_AVX2 are mutually exclusive"
#endif

#define SAUTI_AAC_QMF_INTEL_E_CORE 0x20u
#define SAUTI_AAC_QMF_INTEL_P_CORE 0x40u

/* Pure policy, executable on any host and testable with synthetic CPUID data.
 * For an unknown Intel hybrid type, conservatively prefer SSE2. Elsewhere
 * retain the pre-existing AVX2-if-supported policy. */
static inline int sauti_aac_qmf_avx2_policy(
    int avx2_usable, int intel_hybrid, unsigned core_type) {
    if(!avx2_usable)return 0;
    if(!intel_hybrid)return 1;
    return core_type==SAUTI_AAC_QMF_INTEL_P_CORE;
}

#if SAUTI_AAC_QMF_NATIVE_ASM

/* 0: homogeneous/non-Intel, 0x20: E, 0x40: P,
 * 0xff: Intel hybrid with unavailable/unknown native core-type leaf. */
static inline unsigned sauti_aac_qmf_current_core_type(void) {
    unsigned eax,ebx,ecx,edx;
    __cpuid(0,eax,ebx,ecx,edx);
    const unsigned max_basic=eax;
    /* "GenuineIntel" CPUID vendor signature. */
    if(ebx!=0x756e6547u || edx!=0x49656e69u || ecx!=0x6c65746eu ||
       max_basic<7u)
        return 0u;
    __cpuid_count(7,0,eax,ebx,ecx,edx);
    if((edx & (1u<<15))==0u)return 0u;
    if(max_basic<0x1au)return 0xffu;
    __cpuid_count(0x1a,0,eax,ebx,ecx,edx);
    const unsigned type=eax>>24;
    return type==SAUTI_AAC_QMF_INTEL_E_CORE ||
           type==SAUTI_AAC_QMF_INTEL_P_CORE?type:0xffu;
}

/* Evaluate at the start of each public 40-slot batch, not per time slot.
 * An OS thread may migrate between the CPUID probe and its QMF work;
 * this changes only the chosen optimization, never the output contract.
 * Unlike pinning threads, this leaves scheduling policy to the OS. */
static inline int sauti_aac_qmf_use_avx2(void) {
#if defined(SAUTI_QMF_FORCE_SSE2)
    return 0;
#else
    if(!__builtin_cpu_supports("avx2"))return 0;
#if defined(SAUTI_QMF_FORCE_AVX2)
    return 1;
#else
    const unsigned type=sauti_aac_qmf_current_core_type();
    return sauti_aac_qmf_avx2_policy(1,type!=0u,type);
#endif
#endif
}
#endif
#endif
