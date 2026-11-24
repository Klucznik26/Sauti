/* MPEG-D DRC applied to USAC PCM: see include/sauti_unidrc_run.h.  Behaviour measured against libxaac (decoder/drc_src, instrumented copy) and FDK; written for Sauti. */
#include "sauti_unidrc_run.h"
#include <math.h>
#include <string.h>

static int ducking_set(const SautiUniDrcConfig *c, unsigned dmx) {
    int found = -1;
    unsigned n, k;
    for (n = 0; n < c->instr_count; n++) {                       /* the reference keeps the LAST match */
        const SautiUniDrcInstr *in = &c->instr[n];
        if (!(in->effect & (SAUTI_UNIDRC_EFFECT_DUCK_OTHER | SAUTI_UNIDRC_EFFECT_DUCK_SELF))) continue;
        for (k = 0; k < in->dmx_id_count; k++) if (in->dmx_id[k] == dmx) found = (int)n;
    }
    return found;
}

static int compression_set(const SautiUniDrcConfig *c, unsigned effect) {
    int found = -1;
    unsigned n;
    if (effect == 0 || effect > 8u) return -1;
    for (n = 0; n < c->instr_count; n++) {
        const SautiUniDrcInstr *in = &c->instr[n];
        if ((in->effect & (SAUTI_UNIDRC_EFFECT_DUCK_OTHER | SAUTI_UNIDRC_EFFECT_DUCK_SELF)) || in->dmx_id[0] != 0u) continue;
        if (in->effect & (1u << (effect - 1u))) { found = (int)n; break; }
    }
    return found;
}

static int usable(const SautiUniDrcConfig *c, const SautiUniDrcInstr *in, unsigned channels) {
    unsigned ch;
    const SautiUniDrcCoeffs *co = c->coeff_count ? &c->coeff[0] : 0;
    if (!co || in->channels != channels || in->groups > SAUTI_UNIDRC_RUN_GROUPS) return 0;
    for (ch = 0; ch < in->groups; ch++) {
        const int gs = in->gain_set_of_group[ch];
        if (gs < 0 || (unsigned)gs >= co->gain_set_count || co->gain_set[gs].band_count != 1u) return 0;   /* multi band sets are not applied */
    }
    return 1;
}

int sauti_unidrc_plan(SautiUniDrcPlan *plan, const uint8_t *asc, size_t asc_len, const SautiUsacConfig *usac, const SautiUniDrcOptions *opt) {
    SautiUsacBits b;
    unsigned i;
    int d;
    if (!plan || !asc || !usac || !opt) return 0;
    memset(plan, 0, sizeof *plan);
    plan->opt = *opt;
    plan->sel[0] = plan->sel[1] = -1;
    plan->norm_gain_lin = 1.0f;
    if (!opt->enable) return 0;
    if (usac->drc_cfg_len && (size_t)usac->drc_cfg_pos + (size_t)usac->drc_cfg_len * 8u <= asc_len * 8u) {
        b.data = asc; b.nbits = asc_len * 8u; b.pos = usac->drc_cfg_pos;
        if (sauti_unidrc_config_parse(&b, &plan->cfg) != 1 || plan->cfg.coeff_count == 0) memset(&plan->cfg, 0, sizeof plan->cfg);
    }
    if (usac->loud_len && (size_t)usac->loud_pos + (size_t)usac->loud_len * 8u <= asc_len * 8u) {
        b.data = asc; b.nbits = asc_len * 8u; b.pos = usac->loud_pos;
        plan->have_loud = sauti_unidrc_loudness_parse(&b, &plan->loud) == 1;
    }
    d = opt->drc ? compression_set(&plan->cfg, opt->effect) : -1;
    if (d >= 0 && usable(&plan->cfg, &plan->cfg.instr[d], usac->out_channels)) plan->sel[0] = d;
    d = opt->drc ? ducking_set(&plan->cfg, 0u) : -1;
    if (d >= 0 && usable(&plan->cfg, &plan->cfg.instr[d], usac->out_channels)) plan->sel[1] = d;
    if (opt->loudness_normalise && plan->have_loud) {                             /* gain = target - the program loudness measured for the selected set (else for the set 0) */
        const SautiUniDrcLoudness *best = 0;
        const unsigned want = plan->sel[0] >= 0 ? plan->cfg.instr[plan->sel[0]].set_id : (plan->sel[1] >= 0 ? plan->cfg.instr[plan->sel[1]].set_id : 0u);
        for (i = 0; i < plan->loud.count; i++) if (plan->loud.info[i].downmix_id == 0u && plan->loud.info[i].set_id == want) best = &plan->loud.info[i];      /* the loudness of the SELECTED set (0 = no DRC): a leveling set carries its own, none = no normalisation (FDK) */
        if (best) {
            unsigned m;
            for (m = 0; m < best->measures; m++) if (best->measure[m].method == 1u || best->measure[m].method == 2u) { plan->norm_gain_db = opt->loudness_target - best->measure[m].value; break; }
            plan->norm_gain_lin = (float)pow(10.0, (double)plan->norm_gain_db / 20.0);
        }
    }
    plan->ok = plan->sel[0] >= 0 || plan->sel[1] >= 0 || plan->norm_gain_lin != 1.0f;
    return plan->ok;
}

int sauti_unidrc_process(const SautiUniDrcPlan *plan, SautiUniDrcRun *run, const SautiUsacConfig *usac, const uint8_t *au, size_t au_len, size_t gain_pos, unsigned gain_len, float *pcm) {
    static SautiUniDrcGain g;
    static float lin[SAUTI_UNIDRC_RUN_SETS][SAUTI_UNIDRC_RUN_GROUPS][4096];
    static float chain[4096];
    unsigned set, grp, i, c;
    const unsigned frame = usac->out_frame_len, channels = usac->out_channels;
    const SautiUniDrcCoeffs *co;
    int have_gain = 0;
    if (!plan || !plan->ok || !run || !pcm || frame == 0 || frame > 4096u) return 0;
    if (plan->sel[0] < 0 && plan->sel[1] < 0) {                                       /* normalisation only */
        unsigned n;
        for (n = 0; n < frame * channels; n++) pcm[n] *= plan->norm_gain_lin;
        return 1;
    }
    co = &plan->cfg.coeff[0];
    if (gain_len && plan->cfg.coeff_count) {
        SautiUsacBits b;
        b.data = au; b.nbits = au_len * 8u; b.pos = gain_pos;
        if (gain_pos + (size_t)gain_len * 8u > au_len * 8u || !sauti_unidrc_gain_parse(&plan->cfg, frame, usac->sample_rate, &b, &g)) return 0;
        have_gain = 1;
    }
    for (set = 0; set < SAUTI_UNIDRC_RUN_SETS; set++) {
        const SautiUniDrcInstr *in;
        if (plan->sel[set] < 0) continue;
        in = &plan->cfg.instr[plan->sel[set]];
        for (grp = 0; grp < in->groups; grp++) {
            const SautiUniDrcGainSet *gs = &co->gain_set[in->gain_set_of_group[grp]];
            SautiUniDrcCurveParams cp;
            SautiUniDrcNodes hold;
            const SautiUniDrcNodes *nodes;
            memset(&cp, 0, sizeof cp);
            cp.delay = plan->opt.delay;
            cp.boost = plan->opt.boost > 0.0f ? plan->opt.boost : 1.0f; cp.compress = plan->opt.compress > 0.0f ? plan->opt.compress : 1.0f;
            if (in->effect & (SAUTI_UNIDRC_EFFECT_DUCK_OTHER | SAUTI_UNIDRC_EFFECT_DUCK_SELF)) { cp.boost = cp.compress = 1.0f; if (in->ducking_scaling[grp] != 1.0f) { cp.ducking_flag = 1; cp.ducking_scaling = in->ducking_scaling[grp]; } }
            if (in->mod[grp][0].scaling) { cp.scaling_flag = 1; cp.attn = in->mod[grp][0].attn; cp.ampl = in->mod[grp][0].ampl; }
            if (in->mod[grp][0].offset_flag) { cp.offset_flag = 1; cp.offset_db = in->mod[grp][0].offset; }
            if (have_gain) nodes = &g.seq[gs->seq_idx[0]];
            else { memset(&hold, 0, sizeof hold); hold.count = 1; hold.time[0] = (int)frame - 1 + (gs->time_alignment ? 0 : 0); hold.gain_db[0] = run->seq[set][grp].have ? run->seq[set][grp].gain_db : 0.0f; nodes = &hold; }
            if (have_gain && plan->opt.node_limit && nodes->count > 16u) {      /* FDK: node 15 takes the gain of the 16th node at the end of the frame, the others are read and dropped */
                static SautiUniDrcNodes cut;
                const unsigned dt = gs->time_delta_min ? gs->time_delta_min : sauti_unidrc_delta_tmin(usac->sample_rate);
                const int end = (int)frame + (gs->time_alignment ? -(int)dt + ((int)dt - 1) / 2 : -1);
                cut = *nodes; cut.count = 16u;
                if (cut.time[14] < end) cut.time[15] = end;
                nodes = &cut;
            }
            if (nodes->count == 0 || !sauti_unidrc_curve(gs, nodes, frame, usac->sample_rate, &cp, &run->seq[set][grp], lin[set][grp])) return 0;
        }
    }
    for (c = 0; c < channels; c++) {
        for (i = 0; i < frame; i++) chain[i] = plan->norm_gain_lin;
        for (set = 0; set < SAUTI_UNIDRC_RUN_SETS; set++) {
            const SautiUniDrcInstr *in;
            if (plan->sel[set] < 0) continue;
            in = &plan->cfg.instr[plan->sel[set]];
            if (in->group_of_ch[c] < 0) continue;
            for (i = 0; i < frame; i++) chain[i] *= lin[set][in->group_of_ch[c]][i];
        }
        for (i = 0; i < frame; i++) pcm[(size_t)i * channels + c] *= chain[i];
    }
    return 1;
}
