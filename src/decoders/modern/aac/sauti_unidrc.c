/* MPEG-D DRC (uniDRC) bitstream parsers: see include/sauti_unidrc.h.  Written for Sauti after studying decoder/drc_src/impd_drc_static_payload.c and impd_drc_dynamic_payload.c of libxaac (Apache 2.0). */
#include "sauti_unidrc.h"
#include "sauti_unidrc_tables.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

#define G(b, n) sauti_usac_get((b), (n))
#define OVER(b) ((b)->pos > (b)->nbits)

unsigned sauti_unidrc_delta_tmin(unsigned rate) {
    const unsigned lower = (unsigned)(0.5f + 0.0005f * (float)rate);
    unsigned r = 1;
    while (r <= lower) r <<= 1;
    return r;
}

/* ---- UniDrcConfig ----------------------------------------------------------------------------------------------------------------------------------------------- */
static int parse_downmix(SautiUsacBits *b, unsigned version, unsigned base_channels, SautiUniDrcDownmix *d) {
    const uint32_t t = G(b, 23);
    d->id = (t >> 16) & 0x7f; d->target_channels = (t >> 9) & 0x7f; d->target_layout = (t >> 1) & 0xff; d->coefficients_present = t & 1;
    if (d->target_channels > SAUTI_UNIDRC_MAX_CH) return 0;
    if (d->coefficients_present) b->pos += (size_t)d->target_channels * base_channels * (version ? 5u : 4u) + (version ? 4u : 0u);   /* coefficients: kept out of the structure (no downmix is applied yet) */
    return !OVER(b);
}

static int parse_split_characteristic(SautiUsacBits *b) {
    if (!G(b, 1)) b->pos += 6 + 8 + 1;
    else { const unsigned n = G(b, 2) + 1u; b->pos += (size_t)n * (5u + 8u); }
    return !OVER(b);
}

static int parse_gain_set(SautiUsacBits *b, unsigned version, int *seq_idx, SautiUniDrcGainSet *g) {
    unsigned i;
    const uint32_t t = G(b, 6);
    g->profile = (t >> 4) & 3; g->interpolation = (t >> 3) & 1; g->full_frame = (t >> 2) & 1; g->time_alignment = (t >> 1) & 1;
    g->time_delta_min = 0;
    if (t & 1) g->time_delta_min = G(b, 11) + 1u;
    if (g->profile == 3u) {
        g->band_count = 1; (*seq_idx)++;
        g->seq_idx[0] = (unsigned)*seq_idx;
        if (*seq_idx >= (int)SAUTI_UNIDRC_MAX_SEQ) return 0;
        return 1;
    }
    g->band_count = G(b, 4);
    if (g->band_count > SAUTI_UNIDRC_MAX_BANDS || g->band_count == 0) return 0;
    {
        unsigned band_type = 0;
        if (g->band_count > 1) band_type = G(b, 1);
        for (i = 0; i < g->band_count; i++) {
            if (!version) (*seq_idx)++;
            else if (G(b, 1)) *seq_idx = (int)G(b, 6);
            else (*seq_idx)++;
            if (*seq_idx < 0 || *seq_idx >= (int)SAUTI_UNIDRC_MAX_SEQ) return 0;
            g->seq_idx[i] = (unsigned)*seq_idx;
            if (!version) (void)G(b, 7);                                          /* drcCharacteristic */
            else if (G(b, 1)) { if (G(b, 1)) (void)G(b, 7); else (void)G(b, 8); }          /* characteristic: CICP index or the left / right split indices */
        }
        if (band_type) for (i = 1; i < g->band_count; i++) (void)G(b, 4); else for (i = 1; i < g->band_count; i++) (void)G(b, 10);
    }
    return !OVER(b);
}

static int parse_coeffs(SautiUsacBits *b, unsigned version, SautiUniDrcCoeffs *c) {
    unsigned i;
    int seq_idx = -1;
    uint32_t t = G(b, 5);
    memset(c, 0, sizeof *c);
    c->version = version; c->location = (t >> 1) & 0xf;
    if (t & 1) c->frame_size = G(b, 15) + 1u;
    if (version) {
        if (G(b, 1)) { const unsigned n = G(b, 4); if (n > 8u) return -1; for (i = 0; i < n; i++) if (!parse_split_characteristic(b)) return 0; }
        if (G(b, 1)) { const unsigned n = G(b, 4); if (n > 8u) return -1; for (i = 0; i < n; i++) if (!parse_split_characteristic(b)) return 0; }
        if (G(b, 1)) { const unsigned n = G(b, 4); if (n > 8u) return -1; for (i = 0; i < n; i++) { unsigned k; for (k = 0; k < 4; k++) if (G(b, 1)) b->pos += 5; } }
        t = G(b, 12);
        c->gain_seq_count = (t >> 6) & 0x3f; c->gain_set_count = t & 0x3f;
    } else c->gain_set_count = G(b, 6);
    if (c->gain_set_count > SAUTI_UNIDRC_MAX_GAINSETS || c->gain_seq_count > SAUTI_UNIDRC_MAX_SEQ) return 0;
    for (i = 0; i < SAUTI_UNIDRC_MAX_SEQ; i++) c->gain_set_of_seq[i] = -1;
    for (i = 0; i < c->gain_set_count; i++) {
        unsigned k;
        if (!parse_gain_set(b, version, &seq_idx, &c->gain_set[i])) return 0;
        for (k = 0; k < c->gain_set[i].band_count; k++) c->gain_set_of_seq[c->gain_set[i].seq_idx[k]] = (int)i;
        if (!version) c->gain_seq_count += c->gain_set[i].band_count;
    }
    return !OVER(b);
}

static int parse_ducking_scaling(SautiUsacBits *b, float *scaling) {
    *scaling = 1.0f;
    if (G(b, 1)) { const unsigned s = G(b, 4); const float mu = (float)(s & 7u); *scaling = (s >> 3) ? 1.0f - 0.125f * (1.0f + mu) : 1.0f + 0.125f * (1.0f + mu); }
    return 1;
}

static int parse_gain_modifiers(SautiUsacBits *b, unsigned version, unsigned band_count, SautiUniDrcInstr *in, unsigned g) {
    unsigned k;
    if (version) {
        for (k = 0; k < band_count; k++) {
            if (G(b, 1)) (void)G(b, 4);                                           /* target characteristic left */
            if (G(b, 1)) (void)G(b, 4);                                           /* ... right */
            in->mod[g][k].scaling = G(b, 1);
            in->mod[g][k].attn = in->mod[g][k].ampl = 1.0f;
            if (in->mod[g][k].scaling) { const unsigned t = G(b, 8); in->mod[g][k].attn = (float)((t >> 4) & 0xf) * 0.125f; in->mod[g][k].ampl = (float)(t & 0xf) * 0.125f; }
            in->mod[g][k].offset_flag = G(b, 1);
            if (in->mod[g][k].offset_flag) { const unsigned t = G(b, 6); in->mod[g][k].offset = (1.0f + (float)(t & 0x1f)) * 0.25f * ((t >> 5) ? -1.0f : 1.0f); }
        }
        if (band_count == 1u && G(b, 1)) (void)G(b, 4);                          /* shape filter index */
    } else {
        unsigned s = G(b, 1), o;
        float attn = 1.0f, ampl = 1.0f, off = 0.0f;
        if (s) { const unsigned t = G(b, 8); attn = (float)((t >> 4) & 0xf) * 0.125f; ampl = (float)(t & 0xf) * 0.125f; }
        o = G(b, 1);
        if (o) { const unsigned t = G(b, 6); off = (1.0f + (float)(t & 0x1f)) * 0.25f * ((t >> 5) ? -1.0f : 1.0f); }
        for (k = 0; k < band_count; k++) { in->mod[g][k].scaling = s; in->mod[g][k].attn = attn; in->mod[g][k].ampl = ampl; in->mod[g][k].offset_flag = o; in->mod[g][k].offset = off; }
    }
    return !OVER(b);
}

static const SautiUniDrcCoeffs *select_coeffs(const SautiUniDrcConfig *c, unsigned location) {
    const SautiUniDrcCoeffs *v0 = 0, *v1 = 0;
    unsigned n;
    for (n = 0; n < c->coeff_count; n++) if (c->coeff[n].location == location) { if (c->coeff[n].version) v1 = &c->coeff[n]; else v0 = &c->coeff[n]; }
    return v1 ? v1 : v0;
}

static int parse_instruction(SautiUsacBits *b, unsigned version, const SautiUniDrcConfig *cfg, SautiUniDrcInstr *in) {
    unsigned i, c, dmx_present = 1, ch_cnt = cfg->base_channels;
    const SautiUniDrcCoeffs *co;
    memset(in, 0, sizeof *in);
    in->set_id = G(b, 6);
    if (version) (void)G(b, 4);                                                   /* complexity level */
    in->location = G(b, 4);
    if (version) dmx_present = G(b, 1);
    if (dmx_present) {
        in->dmx_id[0] = G(b, 7);
        if (version) in->apply_to_dwnmix = G(b, 1); else in->apply_to_dwnmix = in->dmx_id[0] != 0;
        in->dmx_id_count = 1;
        if (G(b, 1)) { const unsigned n = G(b, 3); for (i = 0; i < n; i++) in->dmx_id[i + 1] = G(b, 7); in->dmx_id_count = 1u + n; }
    } else { in->dmx_id[0] = 0; in->dmx_id_count = 1; }
    in->effect = G(b, 16);
    if (!(in->effect & (SAUTI_UNIDRC_EFFECT_DUCK_OTHER | SAUTI_UNIDRC_EFFECT_DUCK_SELF))) {
        in->limiter_present = (int)G(b, 1);
        if (in->limiter_present) in->limiter_peak = -(float)G(b, 8) * 0.125f;
    }
    in->target_loudness_present = (int)G(b, 1);
    in->target_loudness_upper = 0; in->target_loudness_lower = -63;
    if (in->target_loudness_present) { in->target_loudness_upper = (int)G(b, 6) - 63; if (G(b, 1)) in->target_loudness_lower = (int)G(b, 6) - 63; }
    in->depends_on_present = (int)G(b, 1);
    if (in->depends_on_present) in->depends_on = G(b, 6); else in->no_independent_use = G(b, 1);
    if (version) (void)G(b, 1);                                                   /* requiresEq */
    co = select_coeffs(cfg, in->location);
    if (ch_cnt > SAUTI_UNIDRC_MAX_CH || ch_cnt == 0) return 0;
    for (c = 0; c < SAUTI_UNIDRC_MAX_CH; c++) { in->gain_set_index[c] = -1; in->group_of_ch[c] = -1; in->gain_set_of_group[c] = -1; in->ducking_scaling[c] = 1.0f; }
    if (in->effect & (SAUTI_UNIDRC_EFFECT_DUCK_OTHER | SAUTI_UNIDRC_EFFECT_DUCK_SELF)) {
        float scale[SAUTI_UNIDRC_MAX_CH], uscale[SAUTI_UNIDRC_MAX_CH];
        int uidx[SAUTI_UNIDRC_MAX_CH], ducking_sequence = -1;
        unsigned g = 0;
        for (c = 0; c < SAUTI_UNIDRC_MAX_CH; c++) { uidx[c] = -10; uscale[c] = -10.0f; }
        c = 0;
        while (c < ch_cnt) {
            const unsigned gs = G(b, 6);
            if (gs > SAUTI_UNIDRC_MAX_GAINSETS) return 0;
            in->gain_set_index[c] = (int)gs - 1;
            (void)parse_ducking_scaling(b, &scale[c]);
            c++;
            if (G(b, 1)) {
                const unsigned rep = G(b, 5) + 1u;
                for (i = 0; i < rep; i++) { if (c >= SAUTI_UNIDRC_MAX_CH) return 0; in->gain_set_index[c] = in->gain_set_index[c - 1u]; scale[c] = scale[c - 1u]; c++; }
            }
            if (OVER(b)) return 0;
        }
        if (c > ch_cnt) return 0;
        if (in->effect & SAUTI_UNIDRC_EFFECT_DUCK_OTHER) {
            for (c = 0; c < ch_cnt; c++) {
                const int idx = in->gain_set_index[c];
                if (idx < 0) {
                    unsigned n; int match = 0;
                    for (n = 0; n < g; n++) if (uscale[n] == scale[c]) { match = 1; in->group_of_ch[c] = (int)n; break; }
                    if (!match) { uidx[g] = idx; uscale[g] = scale[c]; in->group_of_ch[c] = (int)g; g++; }
                } else {
                    if (ducking_sequence > 0 && ducking_sequence != idx) return 0;
                    ducking_sequence = idx; in->group_of_ch[c] = -1;
                }
            }
            if (ducking_sequence == -1) return 0;
        } else {
            for (c = 0; c < ch_cnt; c++) {
                const int idx = in->gain_set_index[c];
                if (idx >= 0) {
                    unsigned n; int match = 0;
                    for (n = 0; n < g; n++) if (uidx[n] == idx && uscale[n] == scale[c]) { match = 1; in->group_of_ch[c] = (int)n; break; }
                    if (!match) { uidx[g] = idx; uscale[g] = scale[c]; in->group_of_ch[c] = (int)g; g++; }
                } else in->group_of_ch[c] = -1;
            }
        }
        if (g > SAUTI_UNIDRC_MAX_CH) return 0;
        in->groups = g;
        for (i = 0; i < g; i++) {
            const int set = (in->effect & SAUTI_UNIDRC_EFFECT_DUCK_OTHER) ? ducking_sequence : uidx[i];
            if (set < 0) return 0;
            in->gain_set_of_group[i] = set; in->ducking_scaling[i] = uscale[i];
        }
        in->channels = ch_cnt;
    } else {
        int uidx[SAUTI_UNIDRC_MAX_CH];
        unsigned g = 0;
        if ((version == 0 || in->apply_to_dwnmix) && in->dmx_id[0] != 0 && in->dmx_id[0] != 0x7f && in->dmx_id_count == 1) {
            for (i = 0; i < cfg->dmx_count; i++) if (cfg->dmx[i].id == in->dmx_id[0]) break;
            if (i == cfg->dmx_count) return 0;
            ch_cnt = cfg->dmx[i].target_channels;
        } else if ((version == 0 || in->apply_to_dwnmix) && (in->dmx_id[0] == 0x7f || in->dmx_id_count > 1)) ch_cnt = 1;
        if (ch_cnt > SAUTI_UNIDRC_MAX_CH || ch_cnt == 0) return 0;
        c = 0;
        while (c < ch_cnt) {
            const uint32_t t = G(b, 7);
            const unsigned gs = (t >> 1) & 0x7f;
            if (gs > SAUTI_UNIDRC_MAX_GAINSETS) return 0;
            in->gain_set_index[c] = (int)gs - 1; c++;
            if (t & 1) {
                const unsigned rep = G(b, 5) + 1u;
                for (i = 0; i < rep; i++) { if (c >= SAUTI_UNIDRC_MAX_CH) return 0; in->gain_set_index[c] = (int)gs - 1; c++; }
            }
            if (OVER(b)) return 0;
        }
        if (c > ch_cnt) return 0;
        if (in->dmx_id[0] == 0x7f || in->dmx_id_count > 1) {
            if (in->gain_set_index[0] >= 0) { uidx[0] = in->gain_set_index[0]; g = 1; }
        } else {
            for (c = 0; c < ch_cnt; c++) {
                const int idx = in->gain_set_index[c];
                if (idx >= 0) {
                    unsigned n; int match = 0;
                    for (n = 0; n < g; n++) if (uidx[n] == idx) { match = 1; in->group_of_ch[c] = (int)n; break; }
                    if (!match) { uidx[g] = idx; in->group_of_ch[c] = (int)g; g++; }
                } else in->group_of_ch[c] = -1;
            }
        }
        if (g > SAUTI_UNIDRC_MAX_CH) return 0;
        in->groups = g; in->channels = ch_cnt;
        for (i = 0; i < g; i++) {
            const int set = uidx[i];
            unsigned bands = 1;
            if (set < 0) return 0;
            in->gain_set_of_group[i] = set;
            if (co && (unsigned)set < co->gain_set_count) bands = co->gain_set[set].band_count;
            if (!parse_gain_modifiers(b, version, bands, in, i)) return 0;
        }
    }
    return !OVER(b);
}

/* extensions of the configuration: 4 bit type, size (4 bit length of the length - 4, then size - 1 bits); type 2 (version 1) is parsed, the others skipped by their size */
static int parse_config_ext(SautiUsacBits *b, SautiUniDrcConfig *c) {
    for (;;) {
        const unsigned type = G(b, 4);
        unsigned len, bits;
        size_t end;
        if (type == 0) return 1;
        len = G(b, 4) + 4u; bits = G(b, len) + 1u;
        if (OVER(b) || (size_t)bits > b->nbits - b->pos) return 0;
        end = b->pos + bits;
        if (type == 1u) c->unsupported |= 1u;                                    /* parametric DRC */
        else if (type == 2u) {                                                   /* version 1 */
            unsigned n, i;
            if (G(b, 1)) {                                                       /* downmix instructions v1 */
                n = G(b, 7);
                if (c->dmx_count + n > SAUTI_UNIDRC_MAX_DMX) return 0;
                for (i = 0; i < n; i++) if (!parse_downmix(b, 1, c->base_channels, &c->dmx[c->dmx_count + i])) return 0;
                c->dmx_count += n;
            }
            if (G(b, 1)) {
                n = G(b, 3);
                if (c->coeff_count + n > SAUTI_UNIDRC_MAX_COEFFS) return 0;
                for (i = 0; i < n; i++) { const int r = parse_coeffs(b, 1, &c->coeff[c->coeff_count + i]); if (r != 1) return r; }
                c->coeff_count += n;
                n = G(b, 6);
                if (c->instr_count + n > SAUTI_UNIDRC_MAX_INSTR) return 0;
                for (i = 0; i < n; i++) if (!parse_instruction(b, 1, c, &c->instr[c->instr_count + i])) return 0;
                c->instr_count += n;
            }
            if (G(b, 1) || G(b, 1)) c->unsupported |= 2u;                           /* loudness EQ / EQ present: skipped with the rest of the extension */
        } else c->unsupported |= 2u;
        if (b->pos > end) return 0;
        b->pos = end;
        if (OVER(b)) return 0;
    }
}

int sauti_unidrc_config_parse(SautiUsacBits *b, SautiUniDrcConfig *out) {
    unsigned i, t, basic_present, basic_coeffs = 0, basic_instr = 0, coeffs, instrs, dmx;
    if (!b || !out) return 0;
    memset(out, 0, sizeof *out);
    if (G(b, 1)) out->sample_rate = G(b, 18) + 1000u;
    t = G(b, 8); dmx = (t >> 1) & 0x7f; basic_present = t & 1;
    if (dmx > SAUTI_UNIDRC_MAX_DMX) return 0;
    if (basic_present) { t = G(b, 7); basic_coeffs = (t >> 4) & 7; basic_instr = t & 0xf; }
    t = G(b, 9); coeffs = (t >> 6) & 7; instrs = t & 0x3f;
    if (instrs > SAUTI_UNIDRC_MAX_INSTR) return 0;
    out->base_channels = G(b, 7);
    if (out->base_channels == 0 || out->base_channels > SAUTI_UNIDRC_MAX_CH) return -1;
    if (G(b, 1)) { if (G(b, 8) == 0) b->pos += (size_t)7u * out->base_channels; }
    for (i = 0; i < dmx; i++) if (!parse_downmix(b, 0, out->base_channels, &out->dmx[i])) return 0;
    out->dmx_count = dmx;
    b->pos += (size_t)basic_coeffs * 11u;
    for (i = 0; i < basic_instr; i++) {                                          /* DrcInstructionsBasic: parsed over */
        t = G(b, 18);
        if (t & 1) { const unsigned n = G(b, 3); b->pos += (size_t)n * 7u; }
        { const unsigned effect = G(b, 16); if (!(effect & (SAUTI_UNIDRC_EFFECT_DUCK_OTHER | SAUTI_UNIDRC_EFFECT_DUCK_SELF)) && G(b, 1)) (void)G(b, 8); }
        if (G(b, 1)) { (void)G(b, 6); if (G(b, 1)) (void)G(b, 6); }
        if (OVER(b)) return 0;
    }
    for (i = 0; i < coeffs; i++) { const int r = parse_coeffs(b, 0, &out->coeff[i]); if (r != 1) return r; }
    out->coeff_count = coeffs;
    for (i = 0; i < instrs; i++) if (!parse_instruction(b, 0, out, &out->instr[i])) return 0;
    out->instr_count = instrs;
    if (G(b, 1) && !parse_config_ext(b, out)) return 0;
    return !OVER(b);
}

/* ---- LoudnessInfoSet ---------------------------------------------------------------------------------------------------------------------------------------------- */
static int method_value(SautiUsacBits *b, unsigned def, float *v) {
    unsigned t;
    switch (def) {
    case 0: case 1: case 2: case 3: case 4: case 5: t = G(b, 8); *v = -57.75f + (float)t * 0.25f; return 1;
    case 6: t = G(b, 8); *v = t == 0 ? 0.0f : t <= 128 ? (float)t * 0.25f : t <= 204 ? 0.5f * (float)t - 32.0f : (float)t - 134.0f; return 1;
    case 7: t = G(b, 5); *v = (float)t + 80.0f; return 1;
    case 8: t = G(b, 2); *v = (float)t; return 1;
    case 9: t = G(b, 8); *v = -116.0f + (float)t * 0.5f; return 1;
    default: return 0;
    }
}

static int parse_loud_info(SautiUsacBits *b, unsigned version, SautiUniDrcLoudness *l) {
    unsigned i, t;
    memset(l, 0, sizeof *l);
    l->set_id = G(b, 6);
    if (version) (void)G(b, 6);
    t = G(b, 8); l->downmix_id = (t >> 1) & 0x7f; l->sample_peak_present = (int)(t & 1);
    if (l->sample_peak_present) { t = G(b, 12); if (!t) l->sample_peak_present = 0; else l->sample_peak = 20.0f - (float)t * 0.03125f; }
    l->true_peak_present = (int)G(b, 1);
    if (l->true_peak_present) { t = G(b, 12); if (!t) l->true_peak_present = 0; else l->true_peak = 20.0f - (float)t * 0.03125f; (void)G(b, 6); }
    l->measures = G(b, 4);
    for (i = 0; i < l->measures; i++) {
        l->measure[i].method = G(b, 4);
        if (!method_value(b, l->measure[i].method, &l->measure[i].value)) return 0;
        t = G(b, 6); l->measure[i].system = (t >> 2) & 0xf; l->measure[i].reliability = t & 3;
    }
    return !OVER(b);
}

int sauti_unidrc_loudness_parse(SautiUsacBits *b, SautiUniDrcLoudnessSet *out) {
    unsigned i, t, albums, infos;
    if (!b || !out) return 0;
    memset(out, 0, sizeof *out);
    t = G(b, 12); albums = (t >> 6) & 0x3f; infos = t & 0x3f;
    if (albums > SAUTI_UNIDRC_MAX_LOUD || infos > SAUTI_UNIDRC_MAX_LOUD) return 0;
    for (i = 0; i < albums; i++) if (!parse_loud_info(b, 0, &out->album[i])) return 0;
    out->album_count = albums;
    for (i = 0; i < infos; i++) if (!parse_loud_info(b, 0, &out->info[i])) return 0;
    out->count = infos;
    if (G(b, 1)) {                                                               /* extensions: type 4 bits, size len 4 bits + 4, size - 1 bits, skipped */
        for (;;) {
            const unsigned type = G(b, 4);
            unsigned len, bits;
            if (type == 0) break;
            len = G(b, 4) + 4u; bits = G(b, len) + 1u;
            if (OVER(b) || (size_t)bits > b->nbits - b->pos) return 0;
            b->pos += bits;
        }
    }
    return !OVER(b);
}

/* ---- UniDrcGain --------------------------------------------------------------------------------------------------------------------------------------------------- */
static int read_code(SautiUsacBits *b, const void *tab, unsigned entries, size_t stride, size_t off_size, size_t off_code, size_t off_value, float *value) {
    unsigned nb = 0, code = 0, e;
    for (e = 0; e < entries; e++) {
        const uint8_t *row = (const uint8_t *)tab + e * stride;
        const unsigned size = row[off_size], rc = *(const uint16_t *)(const void *)(row + off_code);
        while (nb < size) { code = (code << 1) | G(b, 1); nb++; }
        if (code == rc && nb == size) { *value = *(const float *)(const void *)(row + off_value); return !OVER(b); }
        if (OVER(b)) return 0;
    }
    return 0;
}
static int read_gain_code(SautiUsacBits *b, unsigned profile, float *v) {
    return profile == 2u ? read_code(b, sauti_unidrc_gain2, SAUTI_UNIDRC_GAIN2_ENTRIES, sizeof sauti_unidrc_gain2[0], offsetof(SautiUniDrcGainCode, size), offsetof(SautiUniDrcGainCode, code), offsetof(SautiUniDrcGainCode, value), v)
                         : read_code(b, sauti_unidrc_gain01, SAUTI_UNIDRC_GAIN01_ENTRIES, sizeof sauti_unidrc_gain01[0], offsetof(SautiUniDrcGainCode, size), offsetof(SautiUniDrcGainCode, code), offsetof(SautiUniDrcGainCode, value), v);
}

static int initial_gain(SautiUsacBits *b, unsigned profile, float *g) {
    switch (profile) {
    case 0: { const unsigned s = G(b, 1), m = G(b, 8); *g = (float)m * 0.125f; if (s) *g = -*g; return 1; }
    case 1: case 2: { const unsigned s = G(b, 1); if (!s) *g = 0.0f; else *g = -((float)G(b, profile == 1u ? 10u : 8u) + 1.0f) * 0.125f; return 1; }
    default: return 0;
    }
}

/* delta time code (the table of the standard is generated from the largest number of gain values per frame): value = the index of the entry */
static int read_delta_time(SautiUsacBits *b, unsigned max_values, int *value) {
    unsigned Z = 1, nb = 0, code = 0, e, last;
    while ((1u << Z) < 2u * max_values) Z++;
    last = 2u * max_values > 13u ? 2u * max_values : 13u;
    for (e = 1; e <= last; e++) {
        unsigned size, rc;
        if (e == 1) { size = 2; rc = 0; }
        else if (e <= 5) { size = 4; rc = 0x4u + (e - 2u); }
        else if (e <= 13) { size = 5; rc = 0x10u + (e - 6u); }
        else { size = 2u + Z; rc = (3u << Z) + (e - 14u); }
        while (nb < size) { code = (code << 1) | G(b, 1); nb++; }
        if (code == rc) { *value = (int)e; return !OVER(b); }
        if (OVER(b)) return 0;
    }
    return 0;
}

static int parse_sequence(SautiUsacBits *b, const SautiUniDrcGainSet *gs, unsigned frame_size, unsigned rate, SautiUniDrcNodes *n) {
    unsigned k, delta_tmin, max_values;
    int time_offset;
    memset(n, 0, sizeof *n);
    delta_tmin = gs->time_delta_min ? gs->time_delta_min : sauti_unidrc_delta_tmin(rate);
    if (delta_tmin > frame_size) return 0;
    max_values = frame_size / delta_tmin;
    if (max_values > 255u) max_values = 255u;
    if (!gs->time_alignment) time_offset = -1; else time_offset = -(int)delta_tmin + ((int)delta_tmin - 1) / 2;
    n->coding_mode = G(b, 1);
    if (n->coding_mode == 0) {
        n->count = 1;
        if (!initial_gain(b, gs->profile, &n->gain_db[0])) return 0;
        n->slope[0] = 0.0f; n->time[0] = (int)frame_size + time_offset;
        return !OVER(b);
    }
    /* slopes: a unary node count, then (spline interpolation) a slope code per node */
    n->count = 0;
    do { n->count++; if (n->count > SAUTI_UNIDRC_MAX_NODES) return 0; } while (!G(b, 1) && !OVER(b));
    for (k = 0; k < n->count; k++) {
        n->slope[k] = 0.0f;
        if (gs->interpolation == 0u) {
            if (!read_code(b, sauti_unidrc_slope, SAUTI_UNIDRC_SLOPE_ENTRIES, sizeof sauti_unidrc_slope[0], offsetof(SautiUniDrcSlopeCode, size), offsetof(SautiUniDrcSlopeCode, code), offsetof(SautiUniDrcSlopeCode, value), &n->slope[k])) return 0;
        }
    }
    {   /* times */
        int offs = time_offset, td;
        unsigned frame_end = 1;
        if (!gs->full_frame) frame_end = G(b, 1);
        if (frame_end) {
            int reserve = 0;
            for (k = 0; k + 1u < n->count; k++) {
                int t;
                if (!read_delta_time(b, max_values, &td)) return 0;
                t = offs + td * (int)delta_tmin;
                if (t >= 8192 - (int)frame_size) return 0;
                if (t > (int)frame_size + time_offset) { if (!reserve) { n->time[k] = (int)frame_size + time_offset; reserve = 1; } n->time[k + 1u] = t; }
                else n->time[k] = t;
                offs = t;
            }
            if (!reserve) n->time[k] = (int)frame_size + time_offset;
        } else {
            for (k = 0; k < n->count; k++) {
                if (!read_delta_time(b, max_values, &td)) return 0;
                offs += td * (int)delta_tmin;
                if (offs >= 8192 - (int)frame_size) return 0;
                n->time[k] = offs;
            }
        }
    }
    if (!initial_gain(b, gs->profile, &n->gain_db[0])) return 0;                  /* gains: the first by the profile, the rest as deltas */
    for (k = 1; k < n->count; k++) { float d; if (!read_gain_code(b, gs->profile, &d)) return 0; n->gain_db[k] = n->gain_db[k - 1u] + d; }
    return !OVER(b);
}

int sauti_unidrc_gain_parse(const SautiUniDrcConfig *cfg, unsigned frame_size, unsigned rate, SautiUsacBits *b, SautiUniDrcGain *out) {
    unsigned seq;
    const SautiUniDrcCoeffs *co;
    if (!cfg || !b || !out || cfg->coeff_count == 0 || frame_size == 0 || frame_size > 4096u || rate < 1000u) return 0;
    memset(out, 0, sizeof *out);
    co = &cfg->coeff[0];
    out->seq_count = co->gain_seq_count;
    for (seq = 0; seq < co->gain_seq_count; seq++) {
        const int gsi = co->gain_set_of_seq[seq];
        if (gsi < 0) continue;                                                    /* a sequence without a gain set: not coded */
        if (co->gain_set[gsi].profile == 3u) { out->seq[seq].count = 1; out->seq[seq].time[0] = (int)frame_size - 1; continue; }
        if (!parse_sequence(b, &co->gain_set[gsi], frame_size, rate, &out->seq[seq])) return 0;
    }
    if (G(b, 1)) {                                                                /* uniDrcGainExtension: type 4 bits, size len 3 bits + 4, size - 1 bits */
        for (;;) {
            const unsigned type = G(b, 4);
            unsigned len, bits;
            if (type == 0) break;
            len = G(b, 3) + 4u; bits = G(b, len) + 1u;
            if (OVER(b) || (size_t)bits > b->nbits - b->pos) return 0;
            b->pos += bits;
        }
    }
    return !OVER(b);
}

/* ---- gain curve (stage U3) ------------------------------------------------------------------------------------------------------------------------------------- */
#define SLOPE_FACTOR 0.1151f

static void to_linear(const SautiUniDrcCurveParams *p, float db, float slope_db, float *lin, float *lin_slope) {
    float ratio = 1.0f;
    if (db < 0.0f) ratio *= p->compress; else ratio *= p->boost;
    if (p->scaling_flag) ratio *= db < 0.0f ? p->attn : p->ampl;
    if (p->ducking_flag) ratio *= p->ducking_scaling;
    *lin = (float)pow(2.0, (double)(ratio * db / 6.0f));
    *lin_slope = SLOPE_FACTOR * ratio * *lin * slope_db;
    if (p->offset_flag) *lin *= (float)pow(2.0, (double)(p->offset_db / 6.0f));
    if (p->limiter_present) {
        const double extra = -(double)p->limiter_peak - (double)p->norm_gain_db;
        *lin *= (float)pow(2.0, (extra > 0.0 ? extra : 0.0) / 6.0);
        if (*lin >= 1.0f) { *lin = 1.0f; *lin_slope = 0.0f; }
    }
}

/* the values at the samples 0 .. steps of one segment (r[0] = g1, r[steps] = g2) */
static void interpolate(unsigned spline, unsigned steps, float g1, float g2, float s1, float s2, float *r) {
    unsigned n;
    r[0] = g1; r[steps] = g2;
    if (!spline) {
        const float a = (g2 - g1) / (float)steps;
        for (n = 1; n < steps; n++) r[n] = a * (float)n + g1;
        return;
    }
    {
        int cubic = 1;
        if (fabsf(s1) > fabsf(s2)) {
            const float ins_f = 2.0f * (g2 - g1 - s2 * (float)steps) / (s1 - s2);
            const int ins = (int)(0.5f + ins_f);
            if (ins >= 0 && ins < (int)steps) {
                const float a = 0.5f * (s2 - s1) / ins_f;
                cubic = 0;
                for (n = 1; (int)n < ins; n++) { const float t = (float)n; r[n] = (a * t + s1) * t + g1; if (r[n] < 0.0f) r[n] = 0.0f; }
                for (; n < steps; n++) { const float t = (float)((int)n - (int)steps); r[n] = s2 * t + g2; }
            }
        } else if (fabsf(s1) < fabsf(s2)) {
            float ins_f = 2.0f * (g1 - g2 + s1 * (float)steps) / (s1 - s2);
            int ins;
            ins_f = (float)steps - ins_f;
            ins = (int)(0.5f + ins_f);
            if (ins >= 0 && ins < (int)steps) {
                const float a = (s2 - s1) / (2.0f * ((float)steps - ins_f));
                cubic = 0;
                for (n = 1; (int)n < ins; n++) r[n] = s1 * (float)n + g1;
                for (; n < steps; n++) { const float t = (float)((int)steps - (int)n); r[n] = (a * t - s2) * t + g2; if (r[n] < 0.0f) r[n] = 0.0f; }
            }
        }
        if (cubic) {
            const float inv = 1.0f / (float)steps, inv2 = inv * inv, k1 = (g2 - g1) * inv2, k2 = s2 + s1;
            const float a = inv * (inv * k2 - 2.0f * k1), b = 3.0f * k1 - inv * (k2 + s1);
            for (n = 1; n < steps; n++) { const float t = (float)n; r[n] = ((a * t + b) * t + s1) * t + g1; if (r[n] < 0.0f) r[n] = 0.0f; }
        }
    }
}

int sauti_unidrc_curve(const SautiUniDrcGainSet *gs, const SautiUniDrcNodes *nd, unsigned frame_size, unsigned rate, const SautiUniDrcCurveParams *p, SautiUniDrcSeqState *st, float *lin) {
    static float seg[8192 + 8];
    float next[4096];
    unsigned k, delta_tmin;
    int time_prev, i;
    float g_prev, s_prev;
    if (!gs || !nd || !p || !st || !lin || frame_size == 0 || frame_size > 4096u || nd->count == 0) return 0;
    delta_tmin = gs->time_delta_min ? gs->time_delta_min : sauti_unidrc_delta_tmin(rate);
    if (!st->have) { st->have = 1; st->time = -1; st->gain_db = 0.0f; st->slope = 0.0f; for (i = 0; i < 4096; i++) st->open[i] = 1.0f; }
    time_prev = st->time; g_prev = st->gain_db; s_prev = st->slope;
    for (i = 0; i < (int)frame_size; i++) next[i] = 1.0f;
    for (k = 0; k < nd->count; k++) {
        const int t = nd->time[k];
        float l1, l2, sl1, sl2;
        int steps = t - time_prev, s;
        if (steps <= 0 || steps > 8192 || t >= (int)frame_size + 8 || time_prev < -(int)frame_size) return 0;
        to_linear(p, g_prev, s_prev, &l1, &sl1);
        to_linear(p, nd->gain_db[k], nd->slope[k], &l2, &sl2);
        if (gs->interpolation == 0u) { sl1 /= (float)delta_tmin; sl2 /= (float)delta_tmin; }
        interpolate(gs->interpolation == 0u, (unsigned)steps, l1, l2, sl1, sl2, seg);
        for (s = 1; s <= steps; s++) {
            const int at = time_prev + s;
            if (at < 0) { if (at + (int)frame_size >= 0) st->open[at + (int)frame_size] = seg[s]; }      /* the tail of the previous frame */
            else if (at < (int)frame_size) next[at] = seg[s];
        }
        time_prev = t; g_prev = nd->gain_db[k]; s_prev = nd->slope[k];
    }
    {   /* the output is the open frame seen `lookahead` samples early: its last samples come from the frame just parsed */
        const unsigned la = p->lookahead < frame_size ? p->lookahead : 0u;
        memcpy(lin, st->open + la, sizeof(float) * (frame_size - la));
        memcpy(lin + (frame_size - la), next, sizeof(float) * la);
    }
    memcpy(st->open, next, sizeof(float) * frame_size);
    st->time = time_prev - (int)frame_size; st->gain_db = g_prev; st->slope = s_prev;
    return 1;
}
