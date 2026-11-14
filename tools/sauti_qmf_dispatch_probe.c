/* Read-only diagnostics for the current logical CPU of the QMF thread.
 * For Intel hybrid laptops:
 *    taskset -c 4 ./build/sauti_qmf_dispatch_probe
 *    taskset -c 12 ./build/sauti_qmf_dispatch_probe
 * No affinity changes or runtime file access are performed by this probe.
 */
#include "sauti_aac_qmf_dispatch.h"
#include <stdio.h>

int main(void) {
#if SAUTI_AAC_QMF_NATIVE_ASM
    const int has_avx2=__builtin_cpu_supports("avx2")!=0;
    const unsigned type=sauti_aac_qmf_current_core_type();
    const char *label=type==SAUTI_AAC_QMF_INTEL_P_CORE?"Intel P-core":
                      type==SAUTI_AAC_QMF_INTEL_E_CORE?"Intel E-core":
                      type==0xffu?"Intel hybrid, unknown core type":
                      "homogeneous/other CPU";
    printf("Sauti QMF CPU: %s; core_type=0x%02x; AVX2+OS=%s; selected=%s\n",
           label,type,has_avx2?"yes":"no",
           sauti_aac_qmf_use_avx2()?"AVX2":"SSE2");
#else
    puts("Sauti QMF CPU: portable C11 reference (native ASM unavailable)");
#endif
    return 0;
}
