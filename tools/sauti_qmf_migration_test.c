/* SBR-74: verify continuous QMF synthesis when Linux switches execution
 * between Intel P-cores and E-cores. Test process only: never production code.
 *
 * Build twice (automatic hybrid dispatch and forced SSE2) and compare every
 * sample of the 16-frame output. On machines without both core types in the
 * process's allowed affinity mask, run a stationary 16-frame test instead.
 */
#define _GNU_SOURCE
#include "sauti_aac_qmf_dispatch.h"
#include "sauti_aac_sbr_qmf_synth_mod.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__linux__)
#include <sched.h>
#endif

enum { FRAME_COUNT=16, SLOTS=40, BANDS=64, SAMPLES=64 };
static SautiAacSbrComplex bands[SLOTS*BANDS];
static float output[SLOTS*SAMPLES];
static SautiAacSbrQmfSynthWindowState state;

#if defined(__linux__) && SAUTI_AAC_QMF_NATIVE_ASM
static cpu_set_t saved_affinity;
static int saved_valid=0;

static int bind_cpu(int cpu){
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu,&set);
    return sched_setaffinity(0,sizeof(set),&set)==0;
}

static void restore_affinity(void){
    if(saved_valid){
        if(sched_setaffinity(0,sizeof(saved_affinity),&saved_affinity)!=0)
            fputs("WARNING: could not restore initial CPU affinity\n",stderr);
        saved_valid=0;
    }
}

static int find_hybrid_pair(int *p_cpu,int *e_cpu){
    if(sched_getaffinity(0,sizeof(saved_affinity),&saved_affinity)!=0)
        return 0;
    saved_valid=1;
    *p_cpu=-1;
    *e_cpu=-1;
    if(!__builtin_cpu_supports("avx2")){
        restore_affinity();
        return 0;
    }
    for(int cpu=0;cpu<CPU_SETSIZE;cpu++){
        if(!CPU_ISSET(cpu,&saved_affinity) || !bind_cpu(cpu))continue;
        const unsigned type=sauti_aac_qmf_current_core_type();
        if(type==SAUTI_AAC_QMF_INTEL_P_CORE && *p_cpu<0)*p_cpu=cpu;
        if(type==SAUTI_AAC_QMF_INTEL_E_CORE && *e_cpu<0)*e_cpu=cpu;
        if(*p_cpu>=0 && *e_cpu>=0)break;
    }
    /* Restore immediately after probing. Keep a copy of the original
     * affinity for the later P/E alternation and final restoration. */
    if(sched_setaffinity(0,sizeof(saved_affinity),&saved_affinity)!=0){
        fputs("FAIL: could not restore affinity after probing\n",stderr);
        saved_valid=0;
        return -1;
    }
    if(*p_cpu<0 || *e_cpu<0){
        saved_valid=0;
        return 0;
    }
    return 1;
}
#endif

static int verify_frame(unsigned frame,FILE *out){
    for(unsigned t=0;t<SLOTS;t++)
        for(unsigned k=0;k<BANDS;k++){
            const unsigned ix=t*BANDS+k;
            const int re=(int)((frame*31u+t*19u+k*13u)%71u)-35;
            const int im=(int)((frame*17u+t*7u+k*11u)%67u)-33;
            bands[ix].re=(float)re*0.0625f;
            bands[ix].im=(float)im*0.03125f;
        }
    if(!sauti_aac_sbr_qmf_synth_64(
         bands,SLOTS*BANDS,SLOTS,output,SLOTS*SAMPLES,&state))
        return 0;
    for(unsigned i=0;i<SLOTS*SAMPLES;i++)
        if(!isfinite(output[i]))return 0;
    return fwrite(output,sizeof(output[0]),SLOTS*SAMPLES,out)==SLOTS*SAMPLES;
}

int main(int argc,char **argv){
    if(argc!=2 || !argv[1] || !argv[1][0]){
        fputs("usage: sauti_qmf_migration_test FILE.f32le\n",stderr);
        return 2;
    }
    int p_cpu=-1,e_cpu=-1,has_both=0;
#if defined(__linux__) && SAUTI_AAC_QMF_NATIVE_ASM
    has_both=find_hybrid_pair(&p_cpu,&e_cpu);
    if(has_both<0)return 4;
#endif
    FILE *out=fopen(argv[1],"wb");
    if(!out){
        perror("PCM output");
#if defined(__linux__) && SAUTI_AAC_QMF_NATIVE_ASM
        restore_affinity();
#endif
        return 3;
    }
    unsigned seen_p=0,seen_e=0;
    int ok=1;
    for(unsigned frame=0;frame<FRAME_COUNT;frame++){
#if defined(__linux__) && SAUTI_AAC_QMF_NATIVE_ASM
        if(has_both){
            const int cpu=(frame&1u)?e_cpu:p_cpu;
            if(!bind_cpu(cpu)){
                ok=0;
                break;
            }
            const unsigned actual=sauti_aac_qmf_current_core_type();
            if(actual != ((frame&1u)?SAUTI_AAC_QMF_INTEL_E_CORE:
                                      SAUTI_AAC_QMF_INTEL_P_CORE)){
                ok=0;
                break;
            }
            const int chosen=sauti_aac_qmf_use_avx2();
#if defined(SAUTI_QMF_FORCE_SSE2)
            if(chosen)ok=0;
#else
            if(chosen != ((frame&1u)==0u))ok=0;
#endif
            if(!ok)break;
            if(frame&1u)seen_e++;else seen_p++;
        }
#endif
        if(!verify_frame(frame,out)){
            ok=0;
            break;
        }
    }
#if defined(__linux__) && SAUTI_AAC_QMF_NATIVE_ASM
    restore_affinity();
#endif
    if(fclose(out)!=0)ok=0;
    if(!ok){
        remove(argv[1]);
        fputs("FAIL QMF migration/PCM continuity\n",stderr);
        return 1;
    }
    if(has_both)
        printf("PASS QMF hybrid migration: P CPU %d (%u frames), E CPU %d (%u frames); %u PCM floats\n",
               p_cpu,seen_p,e_cpu,seen_e,FRAME_COUNT*SLOTS*SAMPLES);
    else
        printf("SKIP hybrid P/E migration (no accessible pair); PASS stationary %u-frame PCM stream (%u floats)\n",
               FRAME_COUNT,FRAME_COUNT*SLOTS*SAMPLES);
    return 0;
}
