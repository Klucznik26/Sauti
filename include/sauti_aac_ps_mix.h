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

/* SBR-84: the mixing matrix is not constant over an envelope. For the slots of envelope e (first..last, `len` of them) the coefficient moves
 * linearly from the value at the END of the previous envelope to this envelope's value, reaching it in the last slot:
 *   h(n) = start + (n - first + 1) / len * (matrix[e] - start),  start = matrix[e-1] (e > 0) or `previous` (e == 0).
 * `previous` holds the last envelope's coefficients of the previous frame per parameter band (all zero before the first frame). The
 * special imaginary-part sign of the lowest hybrid bands is applied to start and end alike. Left/right are distinct; atomic. */
int sauti_aac_ps_mix_interpolated_apply(const SautiAacPsHybridSchedule *schedule,
                                        const SautiAacPsComplexMatrix *matrix,
                                        const SautiAacPsComplexMatrixCoefficients *previous,
                                        const SautiAacSbrComplex *direct,size_t direct_count,
                                        const SautiAacSbrComplex *decorrelated,size_t decorrelated_count,
                                        SautiAacSbrComplex *left,size_t left_count,
                                        SautiAacSbrComplex *right,size_t right_count);
#endif
