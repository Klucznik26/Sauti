#ifndef SAUTI_AAC_PS_MIX_H
#define SAUTI_AAC_PS_MIX_H

#include "sauti_aac_ps_hybrid.h"
#include "sauti_aac_ps_phase.h"
#include "sauti_aac_sbr_hf.h"

#include <stddef.h>

/* Applies the real IID/ICC matrix to 32 slots of hybrid complex samples.
 * direct and decorrelated are time-major [32][hybrid_bands].  The function
 * is atomic for output prefixes and permits either output to alias an input;
 * left and right themselves must be distinct.  IPD/OPD complex rotation and
 * the PS decorrelator are deliberately separate stages. */
int sauti_aac_ps_mix_real_apply(const SautiAacPsHybridSchedule *schedule,
                                const SautiAacPsMatrix *matrix,
                                const SautiAacSbrComplex *direct,size_t direct_count,
                                const SautiAacSbrComplex *decorrelated,size_t decorrelated_count,
                                SautiAacSbrComplex *left,size_t left_count,
                                SautiAacSbrComplex *right,size_t right_count);

/* Full complex variant used when PS IPD/OPD is present.  The special hybrid
 * phase sign is applied at the normatively selected subbands. */
int sauti_aac_ps_mix_complex_apply(const SautiAacPsHybridSchedule *schedule,
                                   const SautiAacPsComplexMatrix *matrix,
                                   const SautiAacSbrComplex *direct,size_t direct_count,
                                   const SautiAacSbrComplex *decorrelated,size_t decorrelated_count,
                                   SautiAacSbrComplex *left,size_t left_count,
                                   SautiAacSbrComplex *right,size_t right_count);

#endif
