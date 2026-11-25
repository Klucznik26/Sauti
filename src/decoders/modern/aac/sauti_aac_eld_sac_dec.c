/* AAC-ELD v2, signal path of the low delay MPEG Surround 2-1-2: see include/sauti_aac_eld_sac_dec.h.  Written for Sauti; the structure follows the USAC path (sauti_usac_mps_dec.c), the decorrelator is the
 * low delay one (reverb bands 1 .. 3 only, delays 7 / 2 / 1 slots, all-pass orders 15 / 6 / 3, 23 band energy adjustment).  Arithmetic in double, state in float. */
#include "sauti_aac_eld_sac_dec.h"
#include "sauti_aac_eld_sac_tables.h"
#include "sauti_usac_mps_dec.h"
#include "sauti_usac_mps_tables.h"
#include <math.h>
#include <string.h>

typedef SautiAacSbrComplex cxf;
#define NB 23u
#define Q 64u
#define MAXS 64u
#define ABS_THR 1.0e-9
#define DECOR_ALPHA 0.8
#define DECOR_GAMMA 1.5

static const unsigned rev_offset[3][4] = { { 0, 14, 23, 64 }, { 0, 49, 64, 64 }, { 0, 14, 64, 64 } };
static const unsigned rev_delay[4] = { 8, 7, 2, 1 };
static const unsigned rev_order[4] = { 20, 15, 6, 3 };

static const float *rev_num(unsigned rb) { return rb == 1u ? sauti_aac_eld_sac_decor_num1 : rb == 2u ? sauti_aac_eld_sac_decor_num2 : sauti_aac_eld_sac_decor_num3; }

int sauti_aac_eld_sac_apply(SautiAacEldSacDec *d, const SautiAacEldSacConfig *cfg, const SautiUsacMpsFrame *f, unsigned slots, const cxf *in, cxf *out0, cxf *out1) {
    cxf diff[MAXS][Q];
    float resg[SAUTI_USAC_MPS_SETS][NB][2], decg[SAUTI_USAC_MPS_SETS][NB][2];
    unsigned slot_diff[SAUTI_USAC_MPS_SETS], i, k, n, pb, ps, row, rb, start = 0, nb = 64u;
    const uint8_t *map = 0, *map23 = sauti_aac_eld_sac_kernel(23);
    const double gain = cfg ? sauti_usac_mps_clip_gain[cfg->fixed_gain_dmx & 7u] : 1.0;
    cxf hyb[MAXS][Q];
    memset(hyb, 0, sizeof hyb);                                              /* bands above nb are never read, but keep the stack state out of the picture */
    if (cfg) nb = cfg->sample_rate < 27713u ? 32u : 64u;                     /* ISO/IEC 23003-1 6.3.3: below 27713 Hz the QMF has 32 bands (FDK: hybridBands = qmfBands, kernel tables cut to the first nb entries) */
    if (!d || !cfg || !f || !in || !out0 || !out1 || slots == 0 || slots > MAXS || cfg->sample_rate > 48000u || cfg->mps.decorr > 2u || cfg->fixed_gain_dmx > 7u || cfg->mps.temp_shape != 0u) return 0;
    map = sauti_aac_eld_sac_kernel(f->param_bands);
    if (!map || f->param_bands > NB || f->num_sets == 0 || f->num_sets > SAUTI_USAC_MPS_SETS) return 0;
    for (i = 0; i < f->num_sets; i++) {
        const unsigned prev = i ? f->param_slot[i - 1u] : 0u;
        if (f->param_slot[i] >= slots || (i && f->param_slot[i] <= prev)) return 0;
        slot_diff[i] = i ? f->param_slot[i] - prev : f->param_slot[0] + 1u;
    }
    if (f->param_slot[f->num_sets - 1u] != slots - 1u) return 0;
    for (n = 0; n < slots; n++) for (k = 0; k < nb; k++) { hyb[n][k].re = (float)(in[(size_t)n * Q + k].re * gain); hyb[n][k].im = (float)(in[(size_t)n * Q + k].im * gain); }
    /* decorrelator: reverb band 0 is empty, bands 1 .. 3 delay + all-pass */
    memset(diff, 0, sizeof diff);
    for (rb = 0; rb < 4u; rb++) {
        const unsigned stop = rev_offset[cfg->mps.decorr][rb] < nb ? rev_offset[cfg->mps.decorr][rb] : nb;
        if (rb && start < stop) {
            const unsigned delay = rev_delay[rb], order = rev_order[rb];
            const float *num = rev_num(rb);
            for (k = start; k < stop; k++) {
                cxf buf[8 + MAXS];
                for (n = 0; n < delay; n++) buf[n] = d->delay[k][n];
                for (n = 0; n < slots; n++) buf[delay + n] = hyb[n][k];
                for (n = 0; n < slots; n++) {
                    double outr = (double)d->ap_state[k][0].re + (double)buf[n].re * num[0], outi = (double)d->ap_state[k][0].im + (double)buf[n].im * num[0];
                    unsigned j;
                    for (j = 1; j <= order; j++) {
                        const double nr = (double)buf[n].re * num[j] - outr * num[order - j], ni = (double)buf[n].im * num[j] - outi * num[order - j];
                        const double sr = j < order ? (double)d->ap_state[k][j].re : 0.0, si = j < order ? (double)d->ap_state[k][j].im : 0.0;
                        d->ap_state[k][j - 1u].re = (float)(sr + nr); d->ap_state[k][j - 1u].im = (float)(si + ni);
                    }
                    diff[n][k].re = (float)outr; diff[n][k].im = (float)outi;
                }
                for (n = 0; n < delay; n++) d->delay[k][n] = buf[slots + n];
            }
        }
        if (stop > start) start = stop;
    }
    for (n = 0; n < slots; n++) {
        double in_e[NB], out_e[NB], g[NB];
        memset(in_e, 0, sizeof in_e); memset(out_e, 0, sizeof out_e);
        for (k = 0; k < nb; k++) {
            in_e[map23[k]] += (double)hyb[n][k].re * hyb[n][k].re + (double)hyb[n][k].im * hyb[n][k].im;
            out_e[map23[k]] += (double)diff[n][k].re * diff[n][k].re + (double)diff[n][k].im * diff[n][k].im;
        }
        for (pb = 0; pb < NB; pb++) {
            d->smooth_in[pb] = (float)(d->smooth_in[pb] * DECOR_ALPHA + in_e[pb] * (1.0 - DECOR_ALPHA));
            d->smooth_out[pb] = (float)(d->smooth_out[pb] * DECOR_ALPHA + out_e[pb] * (1.0 - DECOR_ALPHA));
            g[pb] = 1.0;
            if (d->smooth_out[pb] > d->smooth_in[pb] * DECOR_GAMMA) g[pb] = sqrt(d->smooth_in[pb] * DECOR_GAMMA / (d->smooth_out[pb] + ABS_THR));
            if (d->smooth_in[pb] > d->smooth_out[pb] * DECOR_GAMMA) { const double v = sqrt(d->smooth_in[pb] / (DECOR_GAMMA * d->smooth_out[pb] + ABS_THR)); g[pb] = v < 2.0 ? v : 2.0; }
        }
        for (k = 0; k < nb; k++) { diff[n][k].re = (float)(diff[n][k].re * g[map23[k]]); diff[n][k].im = (float)(diff[n][k].im * g[map23[k]]); }
    }
    /* upmix gains of every parameter set, smoothing over time (bsSmoothMode), interpolation slot by slot */
    for (ps = 0; ps < f->num_sets; ps++)
        for (pb = 0; pb < f->param_bands; pb++) {
            float rs[2], dc[2], rd[2], ii[2];
            sauti_usac_mps_set_gains(0u, f->cld_idx[ps][pb], f->icc_idx[ps][pb], 0, 0, 0, rs, dc, rd, ii);
            for (row = 0; row < 2u; row++) { resg[ps][pb][row] = rs[row]; decg[ps][pb][row] = dc[row]; }
        }
    for (ps = 0; ps < f->num_sets; ps++) {
        const float delta = (float)slot_diff[ps] * (1.0f / (float)(f->smooth_time[ps] ? f->smooth_time[ps] : 256u)), one_minus = 1.0f - delta;
        for (pb = 0; pb < f->param_bands; pb++) {
            if (!f->smooth_data[ps][pb]) continue;
            for (row = 0; row < 2u; row++) {
                const float pr = ps ? resg[ps - 1u][pb][row] : d->resid_prev[pb][row], pd = ps ? decg[ps - 1u][pb][row] : d->decor_prev[pb][row];
                resg[ps][pb][row] = delta * resg[ps][pb][row] + one_minus * pr;
                decg[ps][pb][row] = delta * decg[ps][pb][row] + one_minus * pd;
            }
        }
    }
    {
        unsigned t = 0;
        for (ps = 0; ps < f->num_sets; ps++) {
            const double inv = 1.0 / (double)slot_diff[ps];
            unsigned step;
            for (step = 1; step <= slot_diff[ps]; step++, t++) {
                double rg[NB][2], dg[NB][2];
                for (pb = 0; pb < f->param_bands; pb++)
                    for (row = 0; row < 2u; row++) {
                        const double r0 = ps ? resg[ps - 1u][pb][row] : d->resid_prev[pb][row], d0 = ps ? decg[ps - 1u][pb][row] : d->decor_prev[pb][row];
                        rg[pb][row] = r0 + (double)step * inv * (resg[ps][pb][row] - r0);
                        dg[pb][row] = d0 + (double)step * inv * (decg[ps][pb][row] - d0);
                    }
                for (k = 0; k < nb; k++) {
                    const unsigned b = map[k];
                    for (row = 0; row < 2u; row++) {
                        cxf *o = (row ? out1 : out0) + (size_t)t * Q + k;
                        o->re = (float)((double)hyb[t][k].re * rg[b][row] + (double)diff[t][k].re * dg[b][row]);
                        o->im = (float)((double)hyb[t][k].im * rg[b][row] + (double)diff[t][k].im * dg[b][row]);
                    }
                }
            }
        }
    }
    for (pb = 0; pb < f->param_bands; pb++) for (row = 0; row < 2u; row++) { d->resid_prev[pb][row] = resg[f->num_sets - 1u][pb][row]; d->decor_prev[pb][row] = decg[f->num_sets - 1u][pb][row]; }
    d->ready = 1u;
    return 1;
}
