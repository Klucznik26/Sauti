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

int sauti_usac_sbr_parse(const SautiUsacElement *e, unsigned channels, SautiUsacBits *b, int indep, unsigned output_rate, SautiUsacSbrState *st, SautiAacSbrPayloadFrame out[2], unsigned *coupling) {
    SautiUsacSbrState next = *st;
    SautiAacSbrMaster master; SautiAacSbrTables tables; SautiAacSbrPatches patches;
    SautiAacSbrGrid grid[2]; SautiAacSbrControl control[2]; SautiAacSbrQuantFrame quant[2]; SautiAacSbrEnergyFrame energy[2];
    uint8_t harmonic[2][SAUTI_AAC_SBR_MAX_MASTER_BANDS];
    unsigned info_present = 1, header_present = 1, cpl = 0, c, i;
    const size_t start = b->pos;
    const uint8_t *data = b->data; const size_t size = bytes_of(b);
    if (e->sbr_harmonic || e->sbr_pvc || e->sbr_intertes) return -1;
    memset(harmonic, 0, sizeof harmonic);
    if (!indep) { info_present = sauti_usac_get(b, 1); header_present = info_present ? sauti_usac_get(b, 1) : 0u; }
    if (info_present) {
        next.header.amp_res = sauti_usac_get(b, 1);
        next.header.xover_band = sauti_usac_get(b, 4);
        if (sauti_usac_get(b, 1)) return -1;                                  /* bs_sbr_preprocessing */
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
    if (channels == 2u) cpl = sauti_usac_get(b, 1);
    for (c = 0; c < channels; c++) {                                          /* sbr_grid(): coupled pairs share the first one */
        if (c == 1u && cpl) { grid[1] = grid[0]; break; }
        if (!sauti_aac_sbr_grid_parse_max(data, size, &b->pos, 16u, SAUTI_AAC_SBR_MAX_ENV, &grid[c])) return 0;
    }
    for (c = 0; c < channels; c++) if (!ctrl_dtdf(b, &grid[c], &tables, indep, &control[c])) return 0;
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
    *coupling = cpl;
    *st = next;
    return 1;
}
