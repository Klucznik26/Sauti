/* USAC LPD, syntax layer (stage L0): lpd_channel_stream(), lpc_data(), fac_data(), the ACELP and TCX parameter fields.  ISO/IEC 23003-3 clause 5.3 (syntax tables of the LPD core).
 * Everything is stored as raw indices; the decoding stages (sauti_usac_lpd_*.c, L1 onwards) work on SautiUsacLpdFrame.  Stream layout and field widths checked against streams of an independent encoder (libxaac, docs SBR-110). */
#include "sauti_usac_lpd.h"
#include <string.h>

#define MAX_QN 35u                                      /* qn > 35 would need Voronoi extensions of more than 16 bits per line: no encoder produces it */

/* qn of an AVQ block: a unary code (number of 1 bits before the first 0), +1 when it is not zero */
static int unary_qn(SautiUsacBits *b, unsigned *qn) {
    unsigned n = 0;
    while (sauti_usac_get(b, 1)) if (++n > MAX_QN) return 0;
    *qn = n ? n + 1u : 0u;
    return 1;
}
static int avq_block(SautiUsacBits *b, unsigned qn, SautiUsacLpdAvq *v) {
    unsigned nk = 0, n = qn, j;
    if (qn > MAX_QN) return 0;
    v->qn = (uint16_t)qn; v->index = 0;
    memset(v->kv, 0, sizeof v->kv);
    if (qn > 4u) { nk = (qn - 3u) >> 1; n = qn - nk * 2u; }
    v->index = sauti_usac_get(b, 4u * n);
    for (j = 0; j < 8u; j++) v->kv[j] = (uint16_t)sauti_usac_get(b, nk);
    return 1;
}

int sauti_usac_lpd_fac_parse(SautiUsacBits *b, unsigned length, int with_gain, SautiUsacLpdFac *fac) {
    unsigned i;
    memset(fac, 0, sizeof *fac);
    if (!length || length > SAUTI_USAC_LPD_FAC_MAX || (length & 7u)) return 0;
    fac->present = 1; fac->length = length;
    if (with_gain) { fac->gain = sauti_usac_get(b, 7); }
    for (i = 0; i < length; i += 8u) {
        unsigned qn;
        if (!unary_qn(b, &qn) || !avq_block(b, qn, &fac->blk[fac->count++])) return 0;
        if (b->pos > b->nbits) return 0;
    }
    return 1;
}

/* the mode of an LSF set (which quantiser the set uses): lpc_set 0 = LPC0, 1 = LPC1, 2 = LPC2, 3 = LPC3, 4 = LPC4 */
static unsigned lpc_mode(unsigned lpc_set, SautiUsacBits *b, unsigned *nk_mode) {
    unsigned mode = 0;
    switch (lpc_set) {
    case 4: break;
    case 0: case 2:
        mode = sauti_usac_get(b, 1);
        if (mode) *nk_mode = 3;
        break;
    case 1:
        if (!sauti_usac_get(b, 1)) mode = *nk_mode = 2;
        else mode = *nk_mode = sauti_usac_get(b, 1) ? 1u : 0u;
        break;
    default:                                                            /* 3 */
        if (!sauti_usac_get(b, 1)) mode = *nk_mode = 1;
        else if (!sauti_usac_get(b, 1)) mode = *nk_mode = 0;
        else { mode = sauti_usac_get(b, 1) ? 3u : 2u; *nk_mode = 2; }
        break;
    }
    return mode;
}
/* qn[0], qn[1] of an LSF set (the codebook numbers of the two 8-line halves of the AVQ residual) */
static int lsf_qn(SautiUsacBits *b, unsigned nk_mode, unsigned qn[2]) {
    unsigned k;
    qn[0] = qn[1] = 0;
    if (nk_mode == 1u) {
        for (k = 0; k < 2u; k++) if (!unary_qn(b, &qn[k])) return 0;
        return 1;
    }
    for (k = 0; k < 2u; k++) qn[k] = 2u + sauti_usac_get(b, 2);
    if (nk_mode == 2u) {
        for (k = 0; k < 2u; k++) {
            if (qn[k] > 4u) {
                unsigned n = 0;
                while (sauti_usac_get(b, 1)) if (++n > MAX_QN) return 0;
                qn[k] = n ? n + 4u : 0u;
            }
        }
    } else {                                                            /* nk_mode 0 and 3 */
        for (k = 0; k < 2u; k++) {
            if (qn[k] > 4u) {
                unsigned e = 0;
                while (sauti_usac_get(b, 1)) if (++e > MAX_QN) return 0;
                qn[k] = e == 0 ? 5u : e == 1 ? 6u : e == 2 ? 0u : e + 4u;
            }
        }
    }
    return 1;
}
static int lsf_indices(SautiUsacBits *b, unsigned nk_mode, SautiUsacLpdLsf *s) {
    unsigned qn[2], k;
    if (!lsf_qn(b, nk_mode, qn)) return 0;
    for (k = 0; k < 2u; k++) {
        SautiUsacLpdAvq v;
        s->qn[k] = (uint16_t)qn[k]; s->index[k] = 0; memset(s->kv[k], 0, sizeof s->kv[k]);
        if (!qn[k]) continue;
        if (!avq_block(b, qn[k], &v)) return 0;
        s->index[k] = v.index; memcpy(s->kv[k], v.kv, sizeof v.kv);
    }
    return b->pos <= b->nbits;
}
/* one LSF set: its mode, the optional 8-bit first stage and the AVQ indices */
static int lsf_set(SautiUsacBits *b, unsigned lpc_set, SautiUsacLpdLsf *s, int skip_indices_on_mode1) {
    unsigned nk_mode = 0;
    memset(s, 0, sizeof *s);
    s->present = 1;
    s->mode_lpc = lpc_mode(lpc_set, b, &nk_mode);
    s->nk_mode = nk_mode;
    if (lpc_set == 4u || s->mode_lpc == 0u) { s->first_stage = sauti_usac_get(b, 8); s->has_first_stage = 1; }
    if (skip_indices_on_mode1 && s->mode_lpc == 1u) return 1;           /* LPC1 in mode 1 is interpolated: no AVQ data */
    return lsf_indices(b, nk_mode, s);
}

static int lpc_data(SautiUsacBits *b, SautiUsacLpdFrame *f) {
    if (!lsf_set(b, 4, &f->lsf[4], 0)) return 0;
    if (f->first_lpd_flag && !lsf_set(b, 0, &f->lsf[0], 0)) return 0;
    if (f->mod[0] < 3u && !lsf_set(b, 2, &f->lsf[2], 0)) return 0;
    if (f->mod[0] < 2u && !lsf_set(b, 1, &f->lsf[1], 1)) return 0;
    if (f->mod[2] < 2u && !lsf_set(b, 3, &f->lsf[3], 0)) return 0;
    return b->pos <= b->nbits;
}

static const uint8_t icb_bits[8][4] = { {5, 5, 5, 5}, {9, 9, 5, 5}, {9, 9, 9, 9}, {13, 13, 9, 9}, {13, 13, 13, 13}, {16, 16, 16, 16}, {1, 5, 1, 5}, {1, 5, 5, 5} };

int sauti_usac_lpd_parse(SautiUsacBits *b, unsigned ccfl, int indep, SautiUsacAcState *ac, SautiUsacLpdFrame *f) {
    const unsigned len_subfrm = ccfl / 4u, nsub = len_subfrm / 64u;
    unsigned k = 0, first_tcx = 1, last_mod = 0, qoff = 0;
    if ((ccfl != 1024u && ccfl != 768u) || !f || !ac) return 0;
    memset(f, 0, sizeof *f);
    f->acelp_core_mode = sauti_usac_get(b, 3);
    f->lpd_mode = sauti_usac_get(b, 5);
    if (f->lpd_mode > 25u) return 0;
    if (f->lpd_mode == 25u) f->mod[0] = f->mod[1] = f->mod[2] = f->mod[3] = 3;
    else if (f->lpd_mode == 24u) f->mod[0] = f->mod[1] = f->mod[2] = f->mod[3] = 2;
    else if (f->lpd_mode >= 20u) { f->mod[0] = f->lpd_mode & 1u; f->mod[1] = (f->lpd_mode >> 1) & 1u; f->mod[2] = f->mod[3] = 2; }
    else if (f->lpd_mode >= 16u) { f->mod[0] = f->mod[1] = 2; f->mod[2] = f->lpd_mode & 1u; f->mod[3] = (f->lpd_mode >> 1) & 1u; }
    else { f->mod[0] = f->lpd_mode & 1u; f->mod[1] = (f->lpd_mode >> 1) & 1u; f->mod[2] = (f->lpd_mode >> 2) & 1u; f->mod[3] = (f->lpd_mode >> 3) & 1u; }
    f->bpf_control_info = sauti_usac_get(b, 1);
    f->core_mode_last = sauti_usac_get(b, 1);
    f->fac_data_present = sauti_usac_get(b, 1);
    f->first_lpd_flag = f->core_mode_last == 0u;
    while (k < 4u) {
        const unsigned m = f->mod[k];
        if (k == 0u) {
            if (f->core_mode_last && f->fac_data_present && !sauti_usac_lpd_fac_parse(b, len_subfrm / 2u, 0, &f->fac[0])) return 0;
        } else if ((last_mod == 0u && m > 0u) || (last_mod > 0u && m == 0u)) {
            if (!sauti_usac_lpd_fac_parse(b, len_subfrm / 2u, 0, &f->fac[k])) return 0;
        }
        if (m == 0u) {                                                  /* ACELP: one quarter */
            SautiUsacLpdAcelp *a = &f->acelp[k];
            unsigned s, j;
            a->mean_energy = sauti_usac_get(b, 2);
            for (s = 0; s < nsub; s++) {
                a->acb_index[s] = sauti_usac_get(b, (s == 0u || (nsub == 4u && s == 2u)) ? 9u : 6u);
                a->ltp_filtering[s] = sauti_usac_get(b, 1);
                if (f->acelp_core_mode == 5u) {
                    for (j = 0; j < 4u; j++) a->icb_index[s][j] = sauti_usac_get(b, 2);
                    for (j = 4; j < 8u; j++) a->icb_index[s][j] = sauti_usac_get(b, 14);
                } else {
                    for (j = 0; j < 4u; j++) a->icb_index[s][j] = sauti_usac_get(b, icb_bits[f->acelp_core_mode][j]);
                }
                a->gain[s] = sauti_usac_get(b, 7);
            }
            last_mod = 0;
            f->tcx_len[k] = 0;
            k += 1u;
        } else {                                                        /* TCX 256 / 512 / 1024 */
            const unsigned len = len_subfrm << (m - 1u), cnt = 1u << (m - 1u);
            unsigned j;
            f->noise_factor[k] = sauti_usac_get(b, 3);
            f->global_gain[k] = sauti_usac_get(b, 7);
            f->tcx_len[k] = len;
            if (first_tcx) f->arith_reset = indep ? 1u : sauti_usac_get(b, 1);
            if (qoff + len > sizeof f->quant / sizeof f->quant[0]) return 0;
            {
                /* arithmetic decoding of the spectrum: the same context coder as the FD windows; the whole TCX length is coded, no lines are left out */
                int32_t *q = f->quant + qoff;
                int32_t tmp[1024];
                if (!sauti_usac_ac_decode(b, ac, first_tcx && f->arith_reset, len, len, tmp)) return 0;
                memcpy(q, tmp, sizeof(int32_t) * len);
            }
            qoff += len;
            last_mod = m;
            for (j = 1; j < cnt; j++) f->tcx_len[k + j] = 0;
            k += cnt;
            first_tcx = 0;
        }
        if (b->pos > b->nbits) return 0;
    }
    if (!lpc_data(b, f)) return 0;
    if (!f->core_mode_last && f->fac_data_present) {
        const unsigned flen = (f->short_fac_flag = sauti_usac_get(b, 1)) ? ccfl / 16u : ccfl / 8u;
        if (!sauti_usac_lpd_fac_parse(b, flen, 1, &f->fac_end)) return 0;
    }
    return b->pos <= b->nbits;
}
