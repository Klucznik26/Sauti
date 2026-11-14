#ifndef SAUTI_AAC_SBR_PATCH_H
#define SAUTI_AAC_SBR_PATCH_H

#include "sauti_aac_sbr_master.h"
#include "sauti_aac_sbr_tables.h"
#include <stdint.h>

/* SBR-11: Classic MPEG-4 HE-AAC high-frequency patch CONSTRUCTION.
 * These are QMF subband INDICES, not QMF-filter output or audio.
 * A conforming SBR encoder typically uses <= 5 patches; some
 * conformance streams contain six, so the hard bound is six. */
#define SAUTI_AAC_SBR_MAX_PATCHES 6u

typedef struct {
    uint8_t count;                            /* 1..6 */
    uint8_t start[SAUTI_AAC_SBR_MAX_PATCHES]; /* QMF SOURCE start */
    uint8_t target[SAUTI_AAC_SBR_MAX_PATCHES];/* QMF TARGET start */
    uint8_t width[SAUTI_AAC_SBR_MAX_PATCHES]; /* QMF number of subbands */
    int8_t source_for_target[SAUTI_AAC_SBR_QMF_BANDS];
    /* -1 for unmapped subbands. All other values are source QMF indices.
     * The last patch may be discarded when narrower than three subbands,
     * intentionally leaving its narrow high-frequency tail unmapped. */
} SautiAacSbrPatches;

/* sample_rate: SBR OUTPUT rate (16..192 kHz supported ISO values).
 * master and high-resolution derived frequency tables must be mutually
 * consistent. On any error, *out remains bit-identical. No allocation,
 * QMF transforms, spectral prediction, or output PCM. */
int sauti_aac_sbr_patch_build(unsigned sample_rate,
                               const SautiAacSbrMaster *master,
                               const SautiAacSbrTables *tables,
                               SautiAacSbrPatches *out);
#endif
