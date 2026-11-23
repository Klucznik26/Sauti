/* USAC enhanced SBR syntax (usac_sbr_data), see sauti_usac_sbr.h.  Differences from the classic sbr_extension_data(): no CRC, no extension / PS data, no bs_data_extra, SbrInfo (amp_res, xover
 * band, preprocessing) in front, the header may be the default one of the configuration, up to eight envelopes, and the first time-differential flag of an independent frame is not transmitted. */
#include "sauti_usac_sbr.h"
#include "sauti_aac_sbr_huffman.h"
#include <string.h>

static int ctrl_dtdf(SautiUsacBits *b, const SautiAacSbrGrid *g, const SautiAacSbrTables *t, int indep, SautiAacSbrControl *c) {
    unsigned i;
    memset(c, 0, sizeof *c);
    c->env_count = g->env_count; c->noise_count = g->noise_count; c->noise_band_count = t->n_noise;
    for (i = 0; i < g->env_count; i++) c->df_env[i] = (indep && i == 0u) ? 0u : (uint8_t)sauti_usac_get(b, 1);
    for (i = 0; i < g->noise_count; i++) c->df_noise[i] = (indep && i == 0u) ? 0u : (uint8_t)sauti_usac_get(b, 1);
    return b->pos <= b->nbits;
}
static int ctrl_invf(SautiUsacBits *b, const SautiAacSbrTables *t, SautiAacSbrControl *c) {
    unsigned i;
    for (i = 0; i < t->n_noise; i++) c->invf_mode[i] = (uint8_t)sauti_usac_get(b, 2);
    return b->pos <= b->nbits;
}
/* the byte buffer view the classic decoders (bit-position based) need */
static size_t bytes_of(const SautiUsacBits *b) { return (b->nbits + 7u) / 8u; }


/* pvc_id of the 16 time slots: bs_div_mode 0..3 gives up to three run lengths of one id each, 4..7 a fixed grid of 2 / 4 / 8 / 16 groups each of which either repeats the id before it or carries a new one */
static int pvc_ids(SautiUsacBits *b, int indep, uint32_t prev_id, SautiUsacPvcFrame *p) {
    unsigned id[SAUTI_USAC_PVC_SLOTS + 1u], k = 1u, i, j;
    p->div_mode = sauti_usac_get(b, 3); p->ns_mode = sauti_usac_get(b, 1);
    if (p->div_mode <= 3u) {
        unsigned runs = p->div_mode, sum = 0;
        const unsigned reuse = indep ? 0u : sauti_usac_get(b, 1);
        id[0] = reuse ? prev_id : sauti_usac_get(b, 7);
        for (i = 0; i < runs; i++) {
            const unsigned bits = sum >= 13u ? 1u : sum >= 11u ? 2u : sum >= 7u ? 3u : 4u;
            const unsigned len = sauti_usac_get(b, bits) + 1u;
            sum += len;
            if (k + len - 1u > SAUTI_USAC_PVC_SLOTS) return 0;
            for (j = 1; j < len; j++, k++) id[k] = id[k - 1u];
            id[k++] = sauti_usac_get(b, 7);
        }
        for (; k < SAUTI_USAC_PVC_SLOTS; k++) id[k] = id[k - 1u];
    } else {
        static const uint8_t groups[4] = { 2, 4, 8, 16 }, span[4] = { 8, 4, 2, 1 };
        const unsigned n = groups[p->div_mode - 4u], len = span[p->div_mode - 4u];
        id[0] = (indep || sauti_usac_get(b, 1)) ? sauti_usac_get(b, 7) : prev_id;
        for (j = 1; j < len; j++, k++) id[k] = id[k - 1u];
        for (i = 1; i < n; i++) {
            if (sauti_usac_get(b, 1)) id[k++] = sauti_usac_get(b, 7); else { id[k] = id[k - 1u]; k++; }
            for (j = 1; j < len; j++, k++) id[k] = id[k - 1u];
        }
    }
    for (i = 0; i < SAUTI_USAC_PVC_SLOTS; i++) p->id[i] = (uint16_t)id[i];
    return b->pos <= b->nbits;
}

/* usac_sbr_data() of a PVC frame (single channel element): the grid is only bs_noise_position and bs_var_len_hf, there are no envelope scalefactors, the noise floor and the sine position follow the
 * pvc_id's.  Returns 1, 0 = malformed, -1 = not implemented. */
/* sbrPatchingMode (1 bit); with 0 (harmonic transposition) also sbrOversamplingFlag (1) and sbrPitchInBinsFlag (1) + sbrPitchInBins (7).  Only present when the configuration has bs_harmonicSBR. */
static void hbe_fields(const SautiUsacElement *e, SautiUsacBits *b, SautiUsacHbeFrame *h) {
    memset(h, 0, sizeof *h);
    if (!e->sbr_harmonic) return;
    h->enabled = 1u;
    if (sauti_usac_get(b, 1)) return;
    h->active = 1u;
    h->oversampling = sauti_usac_get(b, 1);
    if (sauti_usac_get(b, 1)) h->pitch_in_bins = sauti_usac_get(b, 7);
}

static int pvc_sce(const SautiUsacElement *e, SautiUsacBits *b, int indep, const SautiAacSbrTables *tables, const SautiAacSbrMaster *master, const SautiAacSbrPatches *patches,
                   size_t start, SautiUsacSbrState *st, SautiAacSbrPayloadFrame *out) {
    SautiAacSbrGrid grid; SautiAacSbrControl control; SautiAacSbrQuantFrame quant; SautiAacSbrEnergyFrame energy; SautiUsacPvcFrame p;
    uint8_t harmonic[SAUTI_AAC_SBR_MAX_MASTER_BANDS];
    const uint8_t *data = b->data; const size_t size = bytes_of(b);
    unsigned noise_pos, var_len = 0, tb0, i, any;
    SautiUsacHbeFrame hbe;
    memset(&grid, 0, sizeof grid); memset(&p, 0, sizeof p); memset(harmonic, 0, sizeof harmonic); memset(&control, 0, sizeof control);
    hbe_fields(e, b, &hbe);
    noise_pos = sauti_usac_get(b, 4);
    if (sauti_usac_get(b, 1)) { var_len = sauti_usac_get(b, 2) + 1u; if (var_len > 3u) return 0; }
    tb0 = st->prev_kind ? st->prev_last_border : 16u;
    if (tb0 < 16u) return 0;
    tb0 -= 16u;                                                               /* where the previous frame's grid ended: the first slots are already adjusted */
    grid.env_count = grid.noise_count = noise_pos ? 2u : 1u;
    grid.env_border[0] = (uint8_t)tb0;
    if (noise_pos) { if (noise_pos <= tb0) return 0; grid.env_border[1] = (uint8_t)noise_pos; }
    grid.env_border[grid.env_count] = (uint8_t)(16u + var_len);
    for (i = 0; i <= grid.noise_count; i++) grid.noise_border[i] = grid.env_border[i];
    p.active = 1; p.mode = st->header.pvc_mode; p.var_len = var_len; p.prev_mode = st->prev_kind; p.prev_freq_res = st->prev_freq_res;
    for (i = 0; i <= grid.env_count; i++) p.border[i] = grid.env_border[i];
    p.border[grid.env_count] = 16u;
    if (st->prev_kind != 2u) p.border[0] = (uint8_t)tb0; else p.border[0] = 0u;
    p.first_slot = p.border[0];
    control.env_count = grid.env_count; control.noise_count = grid.noise_count; control.noise_band_count = tables->n_noise;
    for (i = 0; i < grid.noise_count; i++) control.df_noise[i] = (indep && i == 0u) ? 0u : (uint8_t)sauti_usac_get(b, 1);
    for (i = 0; i < tables->n_noise; i++) control.invf_mode[i] = (uint8_t)sauti_usac_get(b, 2);
    if (!pvc_ids(b, indep, st->prev_pvc_id, &p)) return 0;
    if (!sauti_aac_sbr_quant_decode_noise_only(data, size, &b->pos, grid.noise_count, tables, &control, st->header.amp_res, &st->quant[0], &quant)) return 0;
    any = sauti_usac_get(b, 1);
    p.sine_position = SAUTI_USAC_PVC_ESC_SINE;
    if (any) {
        for (i = 0; i < tables->n_high; i++) harmonic[i] = (uint8_t)sauti_usac_get(b, 1);
        if (sauti_usac_get(b, 1)) p.sine_position = sauti_usac_get(b, 5);
    }
    if (b->pos > b->nbits) return 0;
    /* a sine that reaches past this frame carries over into the first slots of the next one */
    p.sin_start_cur = st->sin_start_next; p.sin_len_cur = st->sin_len_next;
    st->sin_start_next = st->sin_len_next = 0u;
    if (any && var_len) {
        st->sin_len_next = var_len;
        if (p.sine_position > 16u && p.sine_position != 31u && p.sine_position != 16u + var_len) st->sin_start_next = p.sine_position - 16u;
    }
    /* the dequantised noise floor (the envelope part of `quant` is empty and not used) */
    quant.env_count = grid.env_count;
    if (!sauti_aac_sbr_energy_dequant(&quant, &grid, tables, st->header.amp_res, 0u, &energy)) return 0;
    if (!sauti_aac_sbr_payload_frame_finish(out, &st->header, &grid, &control, &quant, &energy, master, tables, patches, harmonic, (uint32_t)(b->pos - start))) return 0;
    out->pvc = p; out->hbe = hbe;
    st->prev_kind = 2u; st->prev_last_border = 16u + var_len; st->prev_freq_res = 0u; st->prev_pvc_id = p.id[SAUTI_USAC_PVC_SLOTS - 1u];
    return 1;
}

int sauti_usac_sbr_parse(const SautiUsacElement *e, unsigned channels, SautiUsacBits *b, int indep, unsigned output_rate, SautiUsacSbrState *st, SautiAacSbrPayloadFrame out[2], unsigned *coupling) {
    SautiUsacSbrState next = *st;
    SautiAacSbrMaster master; SautiAacSbrTables tables; SautiAacSbrPatches patches;
    SautiAacSbrGrid grid[2]; SautiAacSbrControl control[2]; SautiAacSbrQuantFrame quant[2]; SautiAacSbrEnergyFrame energy[2];
    uint8_t harmonic[2][SAUTI_AAC_SBR_MAX_MASTER_BANDS];
    unsigned info_present = 1, header_present = 1, cpl = 0, c, i;
    SautiUsacHbeFrame hbe[2];
    const size_t start = b->pos;
    const uint8_t *data = b->data; const size_t size = bytes_of(b);
    memset(harmonic, 0, sizeof harmonic);
    if (!indep) { info_present = sauti_usac_get(b, 1); header_present = info_present ? sauti_usac_get(b, 1) : 0u; }
    if (info_present) {
        next.header.amp_res = sauti_usac_get(b, 1);
        next.header.xover_band = sauti_usac_get(b, 4);
        next.header.preproc = sauti_usac_get(b, 1);                           /* bs_sbr_preprocessing */
        next.header.pvc_mode = e->sbr_pvc ? sauti_usac_get(b, 2) : 0u;
    }
    if (header_present) {
        SautiAacSbrHeader *h = &next.header;
        if (sauti_usac_get(b, 1)) {                                            /* the default header of the configuration */
            h->start_freq = e->sbr_start_freq; h->stop_freq = e->sbr_stop_freq; h->freq_scale = e->sbr_freq_scale; h->alter_scale = e->sbr_alter_scale; h->noise_bands = e->sbr_noise_bands;
            h->limiter_bands = e->sbr_limiter_bands; h->limiter_gains = e->sbr_limiter_gains; h->interpol_freq = e->sbr_interpol_freq; h->smoothing_mode = e->sbr_smoothing_mode;
        } else {
            unsigned x1, x2;
            h->start_freq = sauti_usac_get(b, 4); h->stop_freq = sauti_usac_get(b, 4);
            x1 = sauti_usac_get(b, 1); x2 = sauti_usac_get(b, 1);
            h->freq_scale = 2; h->alter_scale = 1; h->noise_bands = 2;
            if (x1) { h->freq_scale = sauti_usac_get(b, 2); h->alter_scale = sauti_usac_get(b, 1); h->noise_bands = sauti_usac_get(b, 2); }
            h->limiter_bands = 2; h->limiter_gains = 2; h->interpol_freq = 1; h->smoothing_mode = 1;
            if (x2) { h->limiter_bands = sauti_usac_get(b, 2); h->limiter_gains = sauti_usac_get(b, 2); h->interpol_freq = sauti_usac_get(b, 1); h->smoothing_mode = sauti_usac_get(b, 1); }
        }
        next.have_header = 1;
    }
    if (b->pos > b->nbits || !next.have_header) return 0;
    if (!sauti_aac_sbr_payload_build(&next.header, output_rate, &master, &tables, &patches)) return 0;
    if (next.header.pvc_mode) {
        int r;
        if (channels != 1u || next.header.pvc_mode > 2u) return -1;           /* PVC is a tool of the single channel element; mode 3 is reserved */
        r = pvc_sce(e, b, indep, &tables, &master, &patches, start, &next, &out[0]);
        if (r != 1) return r;
        *coupling = 0;
        *st = next;
        return 1;
    }
    if (channels == 2u) cpl = sauti_usac_get(b, 1);
    memset(hbe, 0, sizeof hbe);
    hbe_fields(e, b, &hbe[0]);                                                /* the patching mode of a coupled pair is shared, an uncoupled pair has one for each channel */
    if (channels == 2u) { if (cpl) hbe[1] = hbe[0]; else hbe_fields(e, b, &hbe[1]); }
    for (c = 0; c < channels; c++) {                                          /* sbr_grid(): coupled pairs share the first one */
        if (c == 1u && cpl) { grid[1] = grid[0]; break; }
        if (!sauti_aac_sbr_grid_parse_max(data, size, &b->pos, 16u, SAUTI_AAC_SBR_MAX_ENV, &grid[c])) return 0;
    }
    for (c = 0; c < channels; c++) if (!ctrl_dtdf(b, &grid[c], &tables, indep, &control[c])) return 0;
    for (c = 0; c < channels; c++) control[c].inter_tes = (uint8_t)e->sbr_intertes;
    if (channels == 1u || cpl) {                                              /* one inverse filtering mode set: of the (first) channel */
        if (!ctrl_invf(b, &tables, &control[0])) return 0;
        if (channels == 2u) memcpy(control[1].invf_mode, control[0].invf_mode, sizeof control[1].invf_mode);
    } else for (c = 0; c < 2u; c++) if (!ctrl_invf(b, &tables, &control[c])) return 0;
    if (channels == 1u) {
        if (!sauti_aac_sbr_quant_decode(data, size, &b->pos, &grid[0], &tables, &control[0], next.header.amp_res, 0u, &next.quant[0], &quant[0])) return 0;
        if (!sauti_aac_sbr_energy_dequant(&quant[0], &grid[0], &tables, next.header.amp_res, 0u, &energy[0])) return 0;
    } else if (cpl) {
        if (!sauti_aac_sbr_quant_decode(data, size, &b->pos, &grid[0], &tables, &control[0], next.header.amp_res, 0u, &next.quant[0], &quant[0]) ||
            !sauti_aac_sbr_quant_decode(data, size, &b->pos, &grid[1], &tables, &control[1], next.header.amp_res, 1u, &next.quant[1], &quant[1])) return 0;
        if (!sauti_aac_sbr_coupled_dequant(&quant[0], &quant[1], &grid[0], &tables, next.header.amp_res, &energy[0], &energy[1])) return 0;
    } else {
        if (!sauti_aac_sbr_quant_decode_cpe_uncoupled(data, size, &b->pos, grid, &tables, control, next.header.amp_res, next.quant, quant)) return 0;
        for (c = 0; c < 2u; c++) if (!sauti_aac_sbr_energy_dequant(&quant[c], &grid[c], &tables, next.header.amp_res, 0u, &energy[c])) return 0;
    }
    for (c = 0; c < channels; c++) {                                          /* bs_add_harmonic_flag and the flags of the high-resolution bands */
        if (sauti_usac_get(b, 1)) for (i = 0; i < tables.n_high; i++) harmonic[c][i] = (uint8_t)sauti_usac_get(b, 1);
    }
    if (b->pos > b->nbits) return 0;
    for (c = 0; c < channels; c++)
        if (!sauti_aac_sbr_payload_frame_finish(&out[c], &next.header, &grid[c], &control[c], &quant[c], &energy[c], &master, &tables, &patches, harmonic[c], (uint32_t)(b->pos - start))) return 0;
    for (c = 0; c < channels; c++) out[c].hbe = hbe[c];
    next.prev_kind = 1u; next.prev_last_border = grid[0].env_border[grid[0].env_count]; next.prev_freq_res = grid[0].freq_res[grid[0].env_count - 1u];
    next.sin_start_next = next.sin_len_next = 0u;
    *coupling = cpl;
    *st = next;
    return 1;
}
