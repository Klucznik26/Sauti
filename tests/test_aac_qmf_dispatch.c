/* SBR-74 hybrid-core QMF SIMD policy: deterministic and host-CPU checks. */
#include "sauti_aac_qmf_dispatch.h"
#include <assert.h>
#include <stdio.h>

static void test_pure_policy(void) {
    /* No AVX2 support: SSE2 on every CPU class. */
    for(unsigned type=0;type<=0x40;type+=0x20) {
        assert(!sauti_aac_qmf_avx2_policy(0,0,type));
        assert(!sauti_aac_qmf_avx2_policy(0,1,type));
    }
    /* Existing behavior retained on homogeneous Intel/AMD x86-64. */
    assert(sauti_aac_qmf_avx2_policy(1,0,0u));
    assert(sauti_aac_qmf_avx2_policy(1,0,0x20u));
    assert(sauti_aac_qmf_avx2_policy(1,0,0x40u));
    /* Hybrid Intel: measurement-derived policy for Alder Lake. */
    assert(sauti_aac_qmf_avx2_policy(1,1,SAUTI_AAC_QMF_INTEL_P_CORE));
    assert(!sauti_aac_qmf_avx2_policy(1,1,SAUTI_AAC_QMF_INTEL_E_CORE));
    assert(!sauti_aac_qmf_avx2_policy(1,1,0xffu));
    assert(!sauti_aac_qmf_avx2_policy(1,1,0u));
    assert(!sauti_aac_qmf_avx2_policy(1,1,0x30u));
    puts("PASS QMF hybrid SIMD policy P/AVX2 E/SSE2 unknown/SSE2");
}

static void test_live_dispatch(void) {
#if SAUTI_AAC_QMF_NATIVE_ASM
    const int has_avx2=__builtin_cpu_supports("avx2")!=0;
    const unsigned type_before=sauti_aac_qmf_current_core_type();
    const int selected=sauti_aac_qmf_use_avx2();
    const unsigned type_after=sauti_aac_qmf_current_core_type();
    assert(!selected || has_avx2);
#if defined(SAUTI_QMF_FORCE_SSE2)
    assert(!selected);
#elif defined(SAUTI_QMF_FORCE_AVX2)
    assert(selected==has_avx2);
#else
    /* On hybrid CPUs Linux can migrate the thread between CPUID calls.
     * Only assert when both observations agree. Use taskset for an
     * intentionally pinned, deterministic hardware check. */
    if(type_before==type_after)
        assert(selected==sauti_aac_qmf_avx2_policy(
            has_avx2,type_before!=0u,type_before));
#endif
    /* Migration makes an unpinned E-core sample non-authoritative.
     * The test's synthetic policy checks E/SSE2 deterministically. */
    printf("PASS QMF actual dispatch: AVX2=%d core_type_before=0x%02x "
           "core_type_after=0x%02x path=%s\n",
           has_avx2,type_before,type_after,selected?"AVX2":"SSE2");
#else
    puts("PASS QMF non-native: C11 fallback");
#endif
}

int main(void) {
    test_pure_policy();
    test_live_dispatch();
    return 0;
}
