#ifndef SAUTI_UNIDRC_RUN_H
#define SAUTI_UNIDRC_RUN_H
/* MPEG-D DRC applied to the PCM of a USAC decoder (stages U2 and U4 of docs/UNIDRC_PLAN.md): the selection of the DRC sets the reference makes for the default request (the ducking set of the base layout, plus the
 * compression set of a requested effect), the gain curves of the selected sets (single band sets, time domain) and the optional loudness normalisation.  OFF by default: it is an output option. */
#include "sauti_unidrc.h"
#include "sauti_usac.h"

typedef struct {
    unsigned enable;                 /* the plan is made at all */
    unsigned drc;                    /* apply the DRC sets (the dynamic gains) */
    unsigned effect;                 /* requested effect type: 0 none, 1 night, 2 noisy, 3 limited, 4 low level, 5 dialog, 6 general compression, 7 expand, 8 artistic */
    int loudness_normalise;          /* 1 = apply (target - measured loudness) of the stream, the FDK behaviour */
    float loudness_target;           /* LUFS, default -24 */
    float boost, compress;           /* factors of the positive / negative gains, default 1 */
} SautiUniDrcOptions;

typedef struct {
    int ok;                          /* a usable DRC description was found */
    SautiUniDrcOptions opt;
    SautiUniDrcConfig cfg;
    SautiUniDrcLoudnessSet loud;
    unsigned have_loud;
    int sel[SAUTI_UNIDRC_RUN_SETS];  /* instruction index of the selected sets, -1 = none: [0] the compression set of the request, [1] the ducking set */
    float norm_gain_db, norm_gain_lin;
} SautiUniDrcPlan;

/* 1 = a plan was made (plan->ok), 0 = the stream carries no (usable) DRC data or an option is off */
int sauti_unidrc_plan(SautiUniDrcPlan *plan, const uint8_t *asc, size_t asc_len, const SautiUsacConfig *usac, const SautiUniDrcOptions *opt);
/* multiplies the interleaved PCM of one access unit (usac->out_frame_len frames, usac->out_channels channels) by the gains; 1 = ok, 0 = malformed gain payload (the PCM is left unchanged) */
int sauti_unidrc_process(const SautiUniDrcPlan *plan, SautiUniDrcRun *run, const SautiUsacConfig *usac, const uint8_t *au, size_t au_len, size_t gain_pos, unsigned gain_len, float *pcm);
#endif
