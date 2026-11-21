/* Test tool (not a product): a generator of USAC (xHE-AAC) frequency-domain streams.  There is no encoder for these streams on the development machines, so the repository carries its own:
 * random quantised spectra written with the arithmetic coder of ISO/IEC 23003-3 (the encoder is the mirror of src/decoders/modern/aac/sauti_usac_ac.c, whose context functions it includes),
 * scale factors with the AAC Huffman code, noise filling data, TNS filters, M/S masks, window sequences.  Output: PREFIX.asc (AudioSpecificConfig, object type 42) and PREFIX.aus (access units,
 * 4-byte big-endian length each), the pair tools/mux_raw_aus_mp4.py wraps into an MP4.
 *   --channels 1|2   --rate USAC_INDEX (3 = 48 kHz)   --len 1024|768   --frames N   --noise-fill   --ms   --common   --tns   --short   --kbd   --full-sfb   --seed S   --gain G   --amp A
 *   --dense   (more large values: escape levels up to 6)                                                                                                                                        */
#define sauti_usac_ac_decode sauti_usac_gen_unused_decode      /* the decoder entry point of the included file is not used here (and must not clash with the library) */
#include "../src/decoders/modern/aac/sauti_usac_ac.c"
#include "sauti_aac_bands.h"
#include "sauti_aac_tables.h"
#include "sauti_usac_tables.h"
#include "sauti_usac_sbr.h"
#include "sauti_aac_sbr_huffman.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t b[1 << 18]; size_t n; } Bw;
static void put(Bw *w, uint32_t v, unsigned bits) { while (bits--) { if ((w->n & 7u) == 0) w->b[w->n >> 3] = 0; if ((v >> bits) & 1u) w->b[w->n >> 3] |= (uint8_t)(0x80u >> (w->n & 7u)); w->n++; } }
static FILE *g_elem_fu[8];
static int g_lfe_short;                                                      /* diagnostic: the LFE uses an eight-short window from the second frame on (illegal: an LFE has long windows only) */
static int g_probe_ch = -1, g_probe_line = 100;
/* complex stereo prediction: the encoder-side mirror of the decoder's alpha state */
static int16_t g_prev_re[8][64], g_prev_im[8][64];
static unsigned g_prev_ngroups, g_last_seq;
static uint32_t g_rng = 1;
static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static int rr(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }

/* ---- arithmetic encoder (mirror of symbol() in sauti_usac_ac.c) ---------------------------------------------------------------------------------------------------------------------- */
typedef struct { uint32_t low, high; unsigned follow; Bw *w; } Enc;
static void bit_follow(Enc *e, unsigned bit) { put(e->w, bit, 1); while (e->follow) { put(e->w, bit ^ 1u, 1); e->follow--; } }
static void enc_symbol(Enc *e, const uint16_t *cdf, unsigned sym) {
    const uint32_t rng = e->high - e->low + 1u;
    if (sym) e->high = e->low + ((rng * cdf[sym - 1u]) >> 14) - 1u;
    e->low += (rng * cdf[sym]) >> 14;
    for (;;) {
        if (e->high < 32768u) bit_follow(e, 0);
        else if (e->low >= 32768u) { bit_follow(e, 1); e->low -= 32768u; e->high -= 32768u; }
        else if (e->low >= 16384u && e->high < 49152u) { e->follow++; e->low -= 16384u; e->high -= 16384u; }
        else break;
        e->low += e->low; e->high += e->high + 1u;
    }
}
static void put_spectrum(Bw *w, SautiUsacAcState *s, int reset, unsigned lg, unsigned n, const int *q) {
    Enc e; unsigned i, k;
    context_map(s, reset, n);
    if (!lg) { for (i = 0; i < n / 2u; i++) s->last[i] = 1; for (; i < sizeof s->last; i++) s->last[i] = 0; return; }
    e.low = 0; e.high = 65535u; e.follow = 0; e.w = w;
    for (i = 0; i < lg / 2u; i++) {
        const unsigned a0 = (unsigned)abs(q[2u * i]), b0 = (unsigned)abs(q[2u * i + 1u]);
        const uint32_t c = context_get(s, i);
        unsigned lvl = 0, a, b, m;
        while (((a0 >> lvl) > 3u) || ((b0 >> lvl) > 3u)) lvl++;
        for (k = 0; k < lvl; k++) enc_symbol(&e, sauti_usac_ac_msb_cdf[model_get(c + ((uint32_t)(k > 7u ? 7u : k) << 17))], 16);
        m = (((b0 >> lvl) << 2) | (a0 >> lvl));
        enc_symbol(&e, sauti_usac_ac_msb_cdf[model_get(c + ((uint32_t)(lvl > 7u ? 7u : lvl) << 17))], m);
        a = a0 >> lvl; b = b0 >> lvl;
        for (k = lvl; k > 0; k--) {
            const unsigned idx = !a ? 1u : (!b ? 0u : 2u), bita = (a0 >> (k - 1u)) & 1u, bitb = (b0 >> (k - 1u)) & 1u;
            enc_symbol(&e, sauti_usac_ac_lsb_cdf[idx], (bitb << 1) | bita);
            a = (a << 1) | bita; b = (b << 1) | bitb;
        }
        context_update(s, i, a0, b0);
    }
    e.follow++;
    if (e.low < 16384u) bit_follow(&e, 0); else bit_follow(&e, 1);
    for (i = lg / 2u; i < n / 2u; i++) s->last[i] = 1;
    for (i = (lg / 2u > n / 2u ? lg / 2u : n / 2u); i < sizeof s->last; i++) s->last[i] = 0;
    for (i = 0; i < lg; i++) if (q[i]) put(w, q[i] > 0 ? 1u : 0u, 1);
}

/* ---- stream model --------------------------------------------------------------------------------------------------------------------------------------------------------------- */
typedef struct { unsigned sbr, sbr_start, sbr_stop, core_idx, out_rate; unsigned config, channels, rate_idx, len, frames, noise_fill, ms, cplx, common, tns, shorts, kbd, kbd_first, full_sfb, dense, zero_bands; int gain, amp; } Opt;
typedef struct {
    unsigned seq, shape, max_sfb, grouping, ngroups, glen[8], nwin, wlen, stride, nswb, tns_max;
    const uint16_t *swb;
    int gain, sf[8][64];
    int q[8][1024];
    unsigned noise_level, noise_offset, tns_data;
    unsigned tns_len, tns_order, tns_dir; int tns_coef[20];
} Chan;
static const unsigned aac_index[13] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };

static void setup(const Opt *o, Chan *c) {
    const unsigned is_short = c->seq == 2u;
    unsigned count = 0, j;
    if (is_short) {
        if (o->len == 768u) { c->swb = sauti_usac_swb96[o->core_idx]; count = sauti_usac_num_swb96[o->core_idx]; }
        else sauti_aac_band_offsets(o->core_idx, 1, &c->swb, &count);
        c->tns_max = sauti_usac_tns_max_bands_128[o->core_idx]; c->wlen = o->len / 8u; c->stride = 128u; c->nwin = 8u;
        c->ngroups = 0; for (j = 0; j < 8u; j++) c->glen[j] = 0; c->glen[0] = 1;
        for (j = 0; j < 7u; j++) { if (c->grouping & (1u << (6u - j))) c->glen[c->ngroups]++; else { c->ngroups++; c->glen[c->ngroups] = 1; } }
        c->ngroups++;
    } else {
        if (o->len == 768u) { c->swb = sauti_usac_swb768[o->core_idx]; count = sauti_usac_num_swb768[o->core_idx]; }
        else sauti_aac_band_offsets(o->core_idx, 0, &c->swb, &count);
        c->tns_max = sauti_usac_tns_max_bands_1024[o->core_idx]; c->wlen = o->len; c->stride = o->len; c->nwin = 1u; c->ngroups = 1u; c->glen[0] = 1u;
    }
    c->nswb = count;
}
static unsigned next_seq(unsigned prev, const Opt *o) {
    if (!o->shorts) return 0;
    switch (prev) {
    case 0: case 3: return rnd() % 4u == 0 ? 1u : 0u;
    case 1: return 2u;
    default: return rnd() % 3u == 0 ? 3u : 2u;
    }
}
static void fill_content(const Opt *o, Chan *c, int keep_ics) {
    unsigned g, b, w, i, win = 0, first = 1;
    if (!keep_ics) {
        c->max_sfb = o->full_sfb ? c->nswb : (unsigned)rr((int)(c->nswb / 2u), (int)c->nswb);
    }
    c->gain = o->gain + rr(-4, 4);
    {   int off = c->gain;
        for (g = 0; g < c->ngroups; g++) for (b = 0; b < c->max_sfb; b++) {
            if (!(g == 0 && b == 0)) { off += rr(-3, 3); }
            if (off < 0) off = 0;
            if (off > 255) off = 255;
            c->sf[g][b] = off;
        }
    }
    memset(c->q, 0, sizeof c->q);
    for (g = 0; g < c->ngroups; g++) {
        for (w = 0; w < c->glen[g]; w++, win++) {
            for (b = 0; b < c->max_sfb; b++) {
                const int env = o->amp * (c->nswb - b + 4) / (int)(c->nswb + 4);
                for (i = c->swb[b]; i < c->swb[b + 1]; i++) {
                    int v = rr(-env, env) / 2;
                    if (o->dense && rr(0, 40) == 0) v = rr(-3000, 3000);
                    else if (rr(0, 12) == 0) v = rr(-env * 3, env * 3);
                    c->q[win][i] = v;
                }
            }
        }
    }
    if (o->zero_bands) {                                                  /* some bands are entirely zero: noise filling then fills them whole and applies noise_offset to their scale factor */
        unsigned zw = 0;
        for (g = 0; g < c->ngroups; g++) { for (w = 0; w < c->glen[g]; w++, zw++) for (b = 0; b < c->max_sfb; b++) if (((b * 7u + g * 3u + (unsigned)c->gain) % 3u) == 0u) for (i = c->swb[b]; i < c->swb[b + 1]; i++) c->q[zw][i] = 0; }
    }
    (void)first;
    c->noise_level = o->noise_fill ? (rnd() % 3u == 0 ? 0u : (unsigned)rr(1, 7)) : 0u; c->noise_offset = o->noise_fill ? (unsigned)rr(0, 31) : 0u;
    c->tns_data = o->tns && rnd() % 2u;
    c->tns_len = (unsigned)rr(2, c->seq == 2u ? 7 : 30); c->tns_order = (unsigned)rr(1, c->seq == 2u ? 7 : 12); c->tns_dir = rnd() & 1u;
    for (i = 0; i < 20; i++) c->tns_coef[i] = rr(-3, 3);
}
static void apply_probe(Chan *c, int channel) {                           /* a diagnostic: only one line of one channel is nonzero (a value of 20 at line --probe-line) */
    if (g_probe_ch < 0) return;
    memset(c->q, 0, sizeof c->q);
    if (channel == g_probe_ch && c->seq != 2u) c->q[0][g_probe_line] = 20;
}
static void put_codeword(Bw *w, unsigned book, unsigned symbol);
static void put_cplx(Bw *w, const Chan *c0, unsigned mss, int indep) {
    int16_t re[8][64], im[8][64]; uint8_t used[8][64]; unsigned g, sfb, all, pred_dir, complex_coef, use_prev = 0, delta = 0;
    memset(re, 0, sizeof re); memset(im, 0, sizeof im); memset(used, 0, sizeof used);
    if ((c0->seq == 2u && g_last_seq != 2u) || (g_last_seq == 2u && c0->seq != 2u)) { memset(g_prev_re, 0, sizeof g_prev_re); memset(g_prev_im, 0, sizeof g_prev_im); }
    all = rnd() % 3u == 0; put(w, all ? 1u : 0u, 1);
    for (g = 0; g < c0->ngroups; g++) for (sfb = 0; sfb < mss; sfb += 2u) {
        unsigned v = 1;
        if (!all) { v = rnd() % 3u != 0; put(w, v, 1); }
        used[g][sfb] = (uint8_t)v; if (sfb + 1u < mss) used[g][sfb + 1u] = (uint8_t)v;
    }
    pred_dir = rnd() & 1u; complex_coef = rnd() % 4u != 0;
    put(w, pred_dir, 1); put(w, complex_coef, 1);
    if (complex_coef && !indep) { use_prev = rnd() & 1u; put(w, use_prev, 1); }
    if (!indep) { delta = rnd() & 1u; put(w, delta, 1); }
    if (delta && g_prev_ngroups == 0 && c0->seq == 2u && g_last_seq == 2u) { fprintf(stderr, "generator: delta coding with no previous groups\n"); exit(1); }
    for (g = 0; g < c0->ngroups; g++) for (sfb = 0; sfb < mss; sfb += 2u) {
        int last_re = 0, last_im = 0;
        if (delta) {
            if (g) { last_re = re[g - 1u][sfb]; last_im = im[g - 1u][sfb]; }
            else if (c0->seq == 2u && g_last_seq == 2u) { last_re = g_prev_re[g_prev_ngroups - 1u][sfb]; last_im = g_prev_im[g_prev_ngroups - 1u][sfb]; }
            else { last_re = g_prev_re[g][sfb]; last_im = g_prev_im[g][sfb]; }
        } else if (sfb) { last_re = re[g][sfb - 1u]; last_im = im[g][sfb - 1u]; }
        if (used[g][sfb]) {
            const int tre = rr(-8, 8), tim = rr(-8, 8);
            put_codeword(w, 0, (unsigned)(last_re - tre + 60)); re[g][sfb] = (int16_t)tre;           /* the decoder reads d and sets alpha = last - d: index = d + 60 */
            if (complex_coef) { put_codeword(w, 0, (unsigned)(last_im - tim + 60)); im[g][sfb] = (int16_t)tim; }
        }
        if (sfb + 1u < mss) { re[g][sfb + 1u] = re[g][sfb]; im[g][sfb + 1u] = im[g][sfb]; }
    }
    memset(g_prev_re, 0, sizeof g_prev_re); memset(g_prev_im, 0, sizeof g_prev_im);
    for (g = 0; g < c0->ngroups; g++) for (sfb = 0; sfb < mss; sfb += 2u) { g_prev_re[g][sfb] = re[g][sfb]; g_prev_im[g][sfb] = im[g][sfb]; }
}
static void put_ics_info(Bw *w, const Chan *c) {
    put(w, c->seq, 2); put(w, c->shape, 1);
    if (c->seq == 2u) { put(w, c->max_sfb, 4); put(w, c->grouping, 7); } else put(w, c->max_sfb, 6);
}
static int g_tns_neutral, g_force_on_lr = -1, g_tns_compress;
static void put_tns(Bw *w, const Chan *c) {
    unsigned i, win;
    if (g_tns_neutral) { if (c->seq != 2u) put(w, 0, 2); else for (win = 0; win < 8; win++) put(w, 0, 1); return; }
    if (c->seq != 2u) {
        const unsigned compress = g_tns_compress ? (rnd() & 1u) : 0u, width = 4u - compress;
        put(w, 1, 2); put(w, 1, 1); put(w, c->tns_len, 6); put(w, c->tns_order, 4); put(w, c->tns_dir, 1); put(w, compress, 1);
        for (i = 0; i < c->tns_order; i++) put(w, (uint32_t)(c->tns_coef[i] & ((1 << width) - 1)), width);
        return;
    }
    for (win = 0; win < 8; win++) {
        const unsigned compress = g_tns_compress ? (rnd() & 1u) : 0u, width = 4u - compress;
        put(w, 1, 1); put(w, 1, 1); put(w, c->tns_len, 4); put(w, c->tns_order, 3); put(w, c->tns_dir, 1); put(w, compress, 1);
        for (i = 0; i < c->tns_order; i++) put(w, (uint32_t)(c->tns_coef[i] & ((1 << width) - 1)), width);
    }
}
static void put_codeword(Bw *w, unsigned book, unsigned symbol) {
    uint32_t code; uint8_t bits;
    if (!sauti_aac_standard_codeword(book, symbol, &code, &bits)) { fprintf(stderr, "no codeword book %u symbol %u\n", book, symbol); exit(1); }
    put(w, code, bits);
}
static void put_sf(Bw *w, const Chan *c) {
    int prev = c->gain; unsigned g, b;
    for (g = 0; g < c->ngroups; g++) for (b = 0; b < c->max_sfb; b++) {
        if (g || b) {
            uint32_t code; uint8_t bits;
            if (!sauti_aac_standard_codeword(0, (unsigned)(c->sf[g][b] - prev + 60), &code, &bits)) { fprintf(stderr, "sf delta out of range\n"); exit(1); }
            put(w, code, bits);
        }
        prev = c->sf[g][b];
    }
}
static void put_fd(Bw *w, const Opt *o, Chan *c, SautiUsacAcState *ac, int common_window, int indep, int tns_in_channel) {
    unsigned win; const int reset = indep ? 1 : (rnd() % 8u == 0);
    if (getenv("SAUTI_USAC_GEN_TRACE")) fprintf(stderr, "gain %d noise %u/%u seq %u max_sfb %u\n", c->gain, c->noise_level, c->noise_offset, c->seq, c->max_sfb);
    put(w, (uint32_t)c->gain, 8);
    if (o->noise_fill) { put(w, c->noise_level, 3); put(w, c->noise_offset, 5); }
    if (!common_window) put_ics_info(w, c);
    put_sf(w, c);
    if (tns_in_channel && c->tns_data) put_tns(w, c);
    if (!indep) put(w, (uint32_t)reset, 1);
    for (win = 0; win < c->nwin; win++) put_spectrum(w, ac, reset && win == 0, c->swb[c->max_sfb], c->wlen, c->q[win]);
    put(w, 0, 1);                                                          /* fac_data_present */
}

/* ---- eSBR (usac_sbr_data): random valid data.  The candidate is written into a scratch buffer and checked by the decoder's own parser on a copy of its state (retry until it is accepted),
 * so every frame is valid by construction; the harmonic transposer, PVC, inter-TES and preprocessing are never used. -------------------------------------------------------------------------- */
static unsigned g_sbr_simple;                                                 /* diagnostic bits: 4096 header / SbrInfo repeated unchanged after the first frame, 1 only FIXFIX, 2 no time-differential coding, 4 no sinusoids, 8 default header only and never changed, 16 explicit header on the first frame */
static SautiUsacSbrState g_sbrst;
static SautiUsacElement g_sbr_el;
static unsigned g_sbr_trail[2];                                               /* trailing border offset (the grid ends at 16 + t) of the previous frame, per channel */
static int put_sbr_grid(Bw *sb, unsigned trail, SautiAacSbrGrid *g) {
    const size_t start = sb->n; size_t pos = start;
    unsigned cls = trail ? 2u + (rnd() & 1u) : rnd() % 4u, env, i, nl = 0, nt = 0, ptr_bits;
    if (g_sbr_simple & 1u) cls = 0u;
    put(sb, cls, 2);
    if (cls == 0u) { const unsigned v = rnd() % 4u; put(sb, v, 2); put(sb, rnd() & 1u, 1); }
    else {
        if (cls == 1u) { nt = rnd() % 4u; put(sb, rnd() % 4u, 2); put(sb, nt, 2); }
        else if (cls == 2u) { nl = rnd() % 4u; put(sb, trail, 2); put(sb, nl, 2); }
        else { nl = rnd() % 4u; nt = rnd() % 4u; put(sb, trail, 2); put(sb, rnd() % 4u, 2); put(sb, nl, 2); put(sb, nt, 2); }
        env = nl + nt + 1u;
        for (i = 0; i < nl + nt; i++) put(sb, rnd() % 4u, 2);
        ptr_bits = env > 7u ? 4u : env > 3u ? 3u : env > 1u ? 2u : 1u;               /* ceil(log2(env + 1)) */
        put(sb, rnd() % (env + 1u), ptr_bits);
        for (i = 0; i < env; i++) put(sb, rnd() & 1u, 1);
    }
    return sauti_aac_sbr_grid_parse_max(sb->b, (sb->n + 7u) / 8u, &pos, 16u, SAUTI_AAC_SBR_MAX_ENV, g) && pos == sb->n;
}
static void put_sbr_env(Bw *sb, const SautiAacSbrGrid *g, const SautiAacSbrTables *t, unsigned amp_res, unsigned balanced, const uint8_t *df) {
    unsigned e, j;
    const unsigned eff = (g->frame_class == 0u && g->env_count == 1u) ? 0u : amp_res;
    const int env_t = (int)(eff ? SAUTI_SBR_HUFF_ENV_3_0DB_T : SAUTI_SBR_HUFF_ENV_1_5DB_T) + (balanced ? 2 : 0);
    uint32_t code; uint8_t bits;
    for (e = 0; e < g->env_count; e++) {
        const unsigned n = g->freq_res[e] ? t->n_high : t->n_low;
        if (df[e]) { for (j = 0; j < n; j++) { if (!sauti_aac_sbr_huffman_codeword((SautiAacSbrHuffmanBook)env_t, rr(-2, 2), &code, &bits)) exit(1); put(sb, code, bits); } continue; }
        put(sb, balanced ? (uint32_t)rr(4, 10) : (eff ? (uint32_t)rr(8, 14) : (uint32_t)rr(14, 30)), eff ? (balanced ? 5u : 6u) : (balanced ? 6u : 7u));
        for (j = 1; j < n; j++) { if (!sauti_aac_sbr_huffman_codeword((SautiAacSbrHuffmanBook)(env_t + 1), rr(-2, 2), &code, &bits)) exit(1); put(sb, code, bits); }
    }
}
static void put_sbr_noise(Bw *sb, const SautiAacSbrGrid *g, const SautiAacSbrTables *t, unsigned balanced, const uint8_t *df) {
    unsigned e, j;
    uint32_t code; uint8_t bits;
    for (e = 0; e < g->noise_count; e++) {
        if (df[e]) {
            const int book = balanced ? SAUTI_SBR_HUFF_NOISE_BAL_3_0DB_T : SAUTI_SBR_HUFF_NOISE_3_0DB_T;
            for (j = 0; j < t->n_noise; j++) { if (!sauti_aac_sbr_huffman_codeword((SautiAacSbrHuffmanBook)book, rr(-1, 1), &code, &bits)) exit(1); put(sb, code, bits); }
            continue;
        }
        put(sb, balanced ? (uint32_t)rr(2, 6) : (uint32_t)rr(8, 16), 5);
        { const int book = balanced ? SAUTI_SBR_HUFF_ENV_BAL_3_0DB_F : SAUTI_SBR_HUFF_ENV_3_0DB_F;
          for (j = 1; j < t->n_noise; j++) { if (!sauti_aac_sbr_huffman_codeword((SautiAacSbrHuffmanBook)book, rr(-1, 1), &code, &bits)) exit(1); put(sb, code, bits); } }
    }
}
static int sbr_candidate(Bw *sb, const Opt *o, unsigned channels, int indep, const SautiUsacSbrState *work) {
    unsigned info_present = 1, header_present = 1, cpl = 0, c, i;
    SautiAacSbrHeader h = work->header;
    const int repeat = (g_sbr_simple & 4096u) && work->have_header;              /* diagnostic: header and SbrInfo never change after the first frame */
    SautiAacSbrMaster master; SautiAacSbrTables tables; SautiAacSbrPatches patches;
    SautiAacSbrGrid grid[2];
    uint8_t df_e[2][SAUTI_AAC_SBR_MAX_ENV], df_n[2][2];
    memset(sb, 0, sizeof *sb); memset(df_e, 0, sizeof df_e); memset(df_n, 0, sizeof df_n);
    if (!indep) { info_present = (g_sbr_simple & 8u) ? 0u : rnd() % 6u == 0; header_present = info_present ? (rnd() & 1u) : 0u; put(sb, info_present, 1); if (info_present) put(sb, header_present, 1); }
    if (info_present && repeat) { put(sb, h.amp_res, 1); put(sb, h.xover_band, 4); put(sb, 0, 1); }
    else if (info_present) { h.amp_res = ((g_sbr_simple & 256u) || (g_sbr_simple & 32768u)) ? work->header.amp_res : (rnd() & 1u); h.xover_band = ((g_sbr_simple & 256u) || (g_sbr_simple & 65536u)) ? work->header.xover_band : (unsigned)rr(0, 2); put(sb, h.amp_res, 1); put(sb, h.xover_band, 4); put(sb, 0, 1); }
    if (header_present && repeat) {                                              /* the same header again (explicit, every field written) */
        put(sb, 0, 1); put(sb, h.start_freq, 4); put(sb, h.stop_freq, 4); put(sb, 1, 1); put(sb, 1, 1);
        put(sb, h.freq_scale, 2); put(sb, h.alter_scale, 1); put(sb, h.noise_bands, 2); put(sb, h.limiter_bands, 2); put(sb, h.limiter_gains, 2); put(sb, h.interpol_freq, 1); put(sb, h.smoothing_mode, 1);
    } else if (header_present && (getenv("SBR_HDR") || (work->have_header && getenv("SBR_HDR2")))) {   /* diagnostic: SBR_HDR2 (the same list) applies only after the first header, so it is a real change; SBR_HDR=start,stop,freq_scale,alter_scale,noise_bands,limiter_bands,limiter_gains,interpol,smoothing */
        unsigned v[9] = { 5, 9, 2, 1, 2, 2, 2, 1, 1 }; unsigned k = 0; const char *p = (work->have_header && getenv("SBR_HDR2")) ? getenv("SBR_HDR2") : getenv("SBR_HDR");
        while (*p && k < 9u) { v[k++] = (unsigned)strtoul(p, (char **)&p, 10); if (*p == ',') p++; }
        h.start_freq = v[0]; h.stop_freq = v[1]; h.freq_scale = v[2]; h.alter_scale = v[3]; h.noise_bands = v[4]; h.limiter_bands = v[5]; h.limiter_gains = v[6]; h.interpol_freq = v[7]; h.smoothing_mode = v[8];
        {   const unsigned x1 = h.freq_scale != 2u || h.alter_scale != 1u || h.noise_bands != 2u, x2 = h.limiter_bands != 2u || h.limiter_gains != 2u || h.interpol_freq != 1u || h.smoothing_mode != 1u;
            put(sb, 0, 1); put(sb, h.start_freq, 4); put(sb, h.stop_freq, 4); put(sb, x1, 1); put(sb, x2, 1);                 /* only the extras that differ from the defaults are sent */
            if (x1) { put(sb, h.freq_scale, 2); put(sb, h.alter_scale, 1); put(sb, h.noise_bands, 2); }
            if (x2) { put(sb, h.limiter_bands, 2); put(sb, h.limiter_gains, 2); put(sb, h.interpol_freq, 1); put(sb, h.smoothing_mode, 1); } }
    } else if (header_present) {
        const unsigned dflt = (g_sbr_simple & 128u) ? 1u : (g_sbr_simple & 16u) ? 0u : (rnd() & 1u);
        put(sb, dflt, 1);
        h.freq_scale = 2; h.alter_scale = 1; h.noise_bands = 2; h.limiter_bands = 2; h.limiter_gains = 2; h.interpol_freq = 1; h.smoothing_mode = 1;
        if (dflt) {
            h.start_freq = g_sbr_el.sbr_start_freq; h.stop_freq = g_sbr_el.sbr_stop_freq; h.freq_scale = g_sbr_el.sbr_freq_scale; h.alter_scale = g_sbr_el.sbr_alter_scale; h.noise_bands = g_sbr_el.sbr_noise_bands;
            h.limiter_bands = g_sbr_el.sbr_limiter_bands; h.limiter_gains = g_sbr_el.sbr_limiter_gains; h.interpol_freq = g_sbr_el.sbr_interpol_freq; h.smoothing_mode = g_sbr_el.sbr_smoothing_mode;
        }
        else {
            const unsigned x1 = ((g_sbr_simple & 64u) || (g_sbr_simple & 1024u)) ? 0u : (rnd() & 1u), x2 = ((g_sbr_simple & 64u) || (g_sbr_simple & 2048u)) ? 0u : (rnd() & 1u);
            h.start_freq = (g_sbr_simple & 32u) ? g_sbr_el.sbr_start_freq : (unsigned)rr(3, 8); h.stop_freq = (g_sbr_simple & 32u) ? g_sbr_el.sbr_stop_freq : (unsigned)rr((int)h.start_freq + 3 > 6 ? (int)h.start_freq + 3 : 6, 12);
            put(sb, h.start_freq, 4); put(sb, h.stop_freq, 4); put(sb, x1, 1); put(sb, x2, 1);
            if (x1) { h.freq_scale = (unsigned)rr(1, 3); h.alter_scale = rnd() & 1u; h.noise_bands = (unsigned)rr(0, 3); put(sb, h.freq_scale, 2); put(sb, h.alter_scale, 1); put(sb, h.noise_bands, 2); }
            if (x2) { h.limiter_bands = (unsigned)rr(0, 3); h.limiter_gains = (unsigned)rr(0, 3); h.interpol_freq = rnd() & 1u; h.smoothing_mode = rnd() & 1u; put(sb, h.limiter_bands, 2); put(sb, h.limiter_gains, 2); put(sb, h.interpol_freq, 1); put(sb, h.smoothing_mode, 1); }
        }
    }
    if (!sauti_aac_sbr_payload_build(&h, o->out_rate, &master, &tables, &patches)) return 0;
    if (channels == 2u) { cpl = g_sbr_trail[0] == g_sbr_trail[1] ? ((g_sbr_simple & 8192u) ? 1u : (g_sbr_simple & 16384u) ? 0u : (rnd() & 1u)) : 0u; put(sb, cpl, 1); }
    for (c = 0; c < channels; c++) { if (c == 1u && cpl) { grid[1] = grid[0]; break; } if (!put_sbr_grid(sb, g_sbr_trail[c], &grid[c]) || ((g_sbr_simple & 512u) && grid[c].env_count > 5u && grid[c].frame_class != 0u)) return 0; }
    for (c = 0; c < channels; c++) {                                               /* sbr_dtdf(): the first flag of an independent frame is not transmitted */
        for (i = 0; i < grid[c].env_count; i++) { df_e[c][i] = (indep && i == 0u) ? 0u : (uint8_t)((g_sbr_simple & 2u) ? 0u : (rnd() & 1u)); if (!(indep && i == 0u)) put(sb, df_e[c][i], 1); }
        for (i = 0; i < grid[c].noise_count; i++) { df_n[c][i] = (indep && i == 0u) ? 0u : (uint8_t)((g_sbr_simple & 2u) ? 0u : (rnd() & 1u)); if (!(indep && i == 0u)) put(sb, df_n[c][i], 1); }
    }
    for (c = 0; c < (cpl || channels == 1u ? 1u : 2u); c++) for (i = 0; i < tables.n_noise; i++) put(sb, rnd() & 3u, 2);     /* sbr_invf() */
    if (channels == 2u && !cpl) {
        put_sbr_env(sb, &grid[0], &tables, h.amp_res, 0, df_e[0]); put_sbr_env(sb, &grid[1], &tables, h.amp_res, 0, df_e[1]);
        put_sbr_noise(sb, &grid[0], &tables, 0, df_n[0]); put_sbr_noise(sb, &grid[1], &tables, 0, df_n[1]);
    } else for (c = 0; c < channels; c++) { put_sbr_env(sb, &grid[c], &tables, h.amp_res, cpl && c == 1u, df_e[c]); put_sbr_noise(sb, &grid[c], &tables, cpl && c == 1u, df_n[c]); }
    for (c = 0; c < channels; c++) {                                               /* bs_add_harmonic_flag */
        const unsigned on = (g_sbr_simple & 4u) ? 0u : rnd() % 3u == 0;
        put(sb, on, 1);
        if (on) for (i = 0; i < tables.n_high; i++) put(sb, rnd() & 1u, 1);
    }
    return 1;
}
static void put_sbr(Bw *w, const Opt *o, unsigned channels, int indep) {
    unsigned attempt, i;
    for (attempt = 0; attempt < 20000u; attempt++) {
        static Bw sb;
        SautiUsacSbrState trial = g_sbrst; SautiAacSbrPayloadFrame pl[2]; unsigned cpl = 0; SautiUsacBits b; int r;
        if (!sbr_candidate(&sb, o, channels, indep, &g_sbrst)) continue;
        b.data = sb.b; b.nbits = sb.n; b.pos = 0;
        r = sauti_usac_sbr_parse(&g_sbr_el, channels, &b, indep, o->out_rate, &trial, pl, &cpl);
        if (r != 1 || b.pos != sb.n) continue;
        {   /* the reference decoder (FDK) treats envelope values above 35 (3 dB steps) / 70 (1.5 dB steps) as a corrupt frame and conceals it: keep the stream inside that range */
            int ok = 1; unsigned cc, ee, bb;
            for (cc = 0; cc < channels && ok; cc++)
                for (ee = 0; ee < pl[cc].grid.env_count && ok; ee++) {
                    const unsigned eff = (pl[cc].grid.frame_class == 0u && pl[cc].grid.env_count == 1u) ? 0u : trial.header.amp_res;
                    const unsigned n = pl[cc].grid.freq_res[ee] ? pl[cc].tables.n_high : pl[cc].tables.n_low;
                    for (bb = 0; bb < n; bb++) if (pl[cc].quant.env[ee][bb] > (eff ? 35u : 70u)) ok = 0;
                }
            if (!ok) continue;
        }
        if (getenv("SAUTI_USAC_GEN_TRACE")) {
            unsigned e0; unsigned any_h = 0;
            for (e0 = 0; e0 < pl[0].tables.n_high; e0++) any_h |= pl[0].harmonic[e0];
            fprintf(stderr, "SBR ch %u cpl %u hdr start %u stop %u xover %u amp %u fs %u as %u nb %u lim %u/%u interp %u smooth %u | grid class %u env %u freqres %u,%u borders", channels, cpl, trial.header.start_freq, trial.header.stop_freq, trial.header.xover_band, trial.header.amp_res, trial.header.freq_scale, trial.header.alter_scale, trial.header.noise_bands, trial.header.limiter_bands, trial.header.limiter_gains, trial.header.interpol_freq, trial.header.smoothing_mode, pl[0].grid.frame_class, pl[0].grid.env_count, pl[0].grid.freq_res[0], pl[0].grid.freq_res[1]);
            for (e0 = 0; e0 <= pl[0].grid.env_count; e0++) fprintf(stderr, " %u", pl[0].grid.env_border[e0]);
            fprintf(stderr, " ptr %u df %u,%u harm %u\n", pl[0].grid.pointer, pl[0].control.df_env[0], pl[0].control.df_noise[0], any_h);
        }
        g_sbrst = trial;
        for (i = 0; i < channels; i++) g_sbr_trail[i] = pl[i].grid.env_border[pl[i].grid.env_count] - 16u;
        for (i = 0; i < sb.n; i++) put(w, (sb.b[i >> 3] >> (7u - (i & 7u))) & 1u, 1);
        return;
    }
    fprintf(stderr, "generator: no valid SBR frame found\n"); exit(1);
}

/* state of one element: the arithmetic coder contexts, the window sequence history and the complex prediction history of its channels */
typedef struct { SautiUsacAcState ac[2]; unsigned prev_seq[2]; int16_t prev_re[8][64], prev_im[8][64]; unsigned prev_ngroups, last_seq; } ElState;

int main(int argc, char **argv) {
    Opt o; const char *prefix; int a; unsigned f, i;
    Bw w, asc; FILE *fa, *fu; char path[1024];
    Chan ch[2]; SautiUsacAcState *ac; unsigned *prev_seq; unsigned layout[8] = { 0 }, nel = 1, el; static ElState elst[8];
    const unsigned rates[13] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };
    if (argc < 2) { fprintf(stderr, "usage: sauti_usac_gen PREFIX [options]\n"); return 2; }
    prefix = argv[1];
    memset(&o, 0, sizeof o); o.channels = 1; o.rate_idx = 3; o.len = 1024; o.frames = 20; o.gain = 90; o.amp = 12;
    for (a = 2; a < argc; a++) {
        if (!strcmp(argv[a], "--channels") && a + 1 < argc) o.channels = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--config") && a + 1 < argc) o.config = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--lfe-short")) g_lfe_short = 1;
        else if (!strcmp(argv[a], "--rate") && a + 1 < argc) o.rate_idx = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--len") && a + 1 < argc) o.len = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--frames") && a + 1 < argc) o.frames = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--seed") && a + 1 < argc) g_rng = (uint32_t)atoi(argv[++a]) * 2654435761u + 1u;
        else if (!strcmp(argv[a], "--gain") && a + 1 < argc) o.gain = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--amp") && a + 1 < argc) o.amp = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--noise-fill")) o.noise_fill = 1;
        else if (!strcmp(argv[a], "--ms")) o.ms = 1;
        else if (!strcmp(argv[a], "--cplx")) o.cplx = 1;
        else if (!strcmp(argv[a], "--sbr")) o.sbr = 1;
        else if (!strcmp(argv[a], "--sbr-simple") && a + 1 < argc) g_sbr_simple = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--sbr-start") && a + 1 < argc) o.sbr_start = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--sbr-stop") && a + 1 < argc) o.sbr_stop = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--common")) o.common = 1;
        else if (!strcmp(argv[a], "--tns")) o.tns = 1;
        else if (!strcmp(argv[a], "--on-lr") && a + 1 < argc) g_force_on_lr = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--tns-compress")) { o.tns = 1; g_tns_compress = 1; }
        else if (!strcmp(argv[a], "--kbd-first")) { o.kbd = 1; o.kbd_first = 1; }
        else if (!strcmp(argv[a], "--zero-bands")) o.zero_bands = 1;
        else if (!strcmp(argv[a], "--probe") && a + 2 < argc) { g_probe_ch = atoi(argv[++a]); g_probe_line = atoi(argv[++a]); }
        else if (!strcmp(argv[a], "--tns-neutral")) { o.tns = 1; g_tns_neutral = 1; }
        else if (!strcmp(argv[a], "--short")) o.shorts = 1;
        else if (!strcmp(argv[a], "--kbd")) o.kbd = 1;
        else if (!strcmp(argv[a], "--full-sfb")) o.full_sfb = 1;
        else if (!strcmp(argv[a], "--dense")) o.dense = 1;
        else { fprintf(stderr, "unknown option %s\n", argv[a]); return 2; }
    }
    if (o.config) {                                                         /* channel configurations 1..7: SCE / CPE / LFE elements in stream order (0 = SCE, 1 = CPE, 2 = LFE) */
        static const unsigned cfgs[8][6] = { { 0 }, { 0, 9 }, { 1, 9 }, { 0, 1, 9 }, { 0, 1, 0, 9 }, { 0, 1, 1, 9 }, { 0, 1, 1, 2, 9 }, { 0, 1, 1, 1, 2, 9 } };
        if (o.config > 7u || o.sbr) { fprintf(stderr, "bad options\n"); return 2; }
        for (nel = 0, o.channels = 0; cfgs[o.config][nel] != 9u; nel++) { layout[nel] = cfgs[o.config][nel]; o.channels += layout[nel] == 1u ? 2u : 1u; }
    } else layout[0] = o.channels == 2u ? 1u : 0u;
    if (o.channels < 1 || o.channels > 8 || (!o.config && o.channels > 2) || (o.len != 1024 && o.len != 768) || o.rate_idx > 12) { fprintf(stderr, "bad options\n"); return 2; }
    if (o.ms || o.cplx) o.common = 1;
    if (o.sbr && o.len != 1024u) { fprintf(stderr, "--sbr needs --len 1024 (2:1)\n"); return 2; }
    if (!o.sbr_start) o.sbr_start = 5;
    if (!o.sbr_stop) o.sbr_stop = 9;
    o.out_rate = (unsigned)sauti_usac_samplerate[o.rate_idx];
    { static const unsigned limit[11] = { 92017, 75132, 55426, 46009, 37566, 27713, 23004, 18783, 13856, 11502, 9391 };
      const unsigned core_rate = o.sbr ? o.out_rate / 2u : o.out_rate; unsigned k;
      for (k = 0; k < 11u && core_rate < limit[k]; k++) ;
      o.core_idx = k; }
    memset(&g_sbr_el, 0, sizeof g_sbr_el); g_sbr_el.sbr_start_freq = o.sbr_start; g_sbr_el.sbr_stop_freq = o.sbr_stop; g_sbr_el.sbr_freq_scale = 2; g_sbr_el.sbr_alter_scale = 1; g_sbr_el.sbr_noise_bands = 2;
    g_sbr_el.sbr_limiter_bands = 2; g_sbr_el.sbr_limiter_gains = 2; g_sbr_el.sbr_interpol_freq = 1; g_sbr_el.sbr_smoothing_mode = 1;
    if (getenv("SBR_CFG")) {                                                /* SBR_CFG=start,stop,freq_scale,alter_scale,noise_bands,limiter_bands,limiter_gains,interpol,smoothing: the default header of the configuration */
        unsigned v[9] = { o.sbr_start, o.sbr_stop, 2, 1, 2, 2, 2, 1, 1 }, k = 0; const char *p = getenv("SBR_CFG");
        while (*p && k < 9u) { v[k++] = (unsigned)strtoul(p, (char **)&p, 10); if (*p == ',') p++; }
        o.sbr_start = g_sbr_el.sbr_start_freq = v[0]; o.sbr_stop = g_sbr_el.sbr_stop_freq = v[1]; g_sbr_el.sbr_freq_scale = v[2]; g_sbr_el.sbr_alter_scale = v[3]; g_sbr_el.sbr_noise_bands = v[4];
        g_sbr_el.sbr_limiter_bands = v[5]; g_sbr_el.sbr_limiter_gains = v[6]; g_sbr_el.sbr_interpol_freq = v[7]; g_sbr_el.sbr_smoothing_mode = v[8];
    }
    /* AudioSpecificConfig: object type 42 (escape), sampling index, channel configuration, UsacConfig */
    memset(&asc, 0, sizeof asc);
    put(&asc, 31, 5); put(&asc, 42 - 32, 6); put(&asc, aac_index[o.rate_idx], 4); put(&asc, o.config ? o.config : o.channels, 4);
    put(&asc, o.rate_idx, 5); put(&asc, o.sbr ? 3u : (o.len == 768u ? 0u : 1u), 3); put(&asc, o.config ? o.config : o.channels, 5);
    put(&asc, nel - 1u, 4);                                                /* numElements - 1 (escapedValue(4, 8, 16)) */
    for (el = 0; el < nel; el++) {
        put(&asc, layout[el], 2);                                           /* SCE / CPE / LFE */
        if (layout[el] != 2u) { put(&asc, 0, 1); put(&asc, o.noise_fill, 1); }   /* tw_mdct = 0, noiseFilling (an LFE has neither) */
    }
    if (o.sbr) {                                                            /* SBR configuration: no harmonic transposer, inter-TES, PVC; default header start / stop, no extra fields */
        const unsigned x1 = g_sbr_el.sbr_freq_scale != 2u || g_sbr_el.sbr_alter_scale != 1u || g_sbr_el.sbr_noise_bands != 2u;
        const unsigned x2 = g_sbr_el.sbr_limiter_bands != 2u || g_sbr_el.sbr_limiter_gains != 2u || g_sbr_el.sbr_interpol_freq != 1u || g_sbr_el.sbr_smoothing_mode != 1u;
        put(&asc, 0, 3); put(&asc, o.sbr_start, 4); put(&asc, o.sbr_stop, 4); put(&asc, x1, 1); put(&asc, x2, 1);
        if (x1) { put(&asc, g_sbr_el.sbr_freq_scale, 2); put(&asc, g_sbr_el.sbr_alter_scale, 1); put(&asc, g_sbr_el.sbr_noise_bands, 2); }
        if (x2) { put(&asc, g_sbr_el.sbr_limiter_bands, 2); put(&asc, g_sbr_el.sbr_limiter_gains, 2); put(&asc, g_sbr_el.sbr_interpol_freq, 1); put(&asc, g_sbr_el.sbr_smoothing_mode, 1); }
        if (o.channels == 2u) put(&asc, 0, 2);                              /* stereoConfigIndex 0 */
    }
    put(&asc, 0, 1);                                                       /* usacConfigExtensionPresent */
    snprintf(path, sizeof path, "%s.asc", prefix); fa = fopen(path, "wb"); fwrite(asc.b, 1, (asc.n + 7u) / 8u, fa); fclose(fa);
    snprintf(path, sizeof path, "%s.ascbits", prefix); fa = fopen(path, "w"); fprintf(fa, "%zu\n", (size_t)asc.n); fclose(fa);      /* the exact length of the AudioSpecificConfig in bits (tools/mux_raw_aus_loas.py) */
    snprintf(path, sizeof path, "%s.aus", prefix); fu = fopen(path, "wb");
    if (getenv("SAUTI_USAC_GEN_ELEMS")) {                                   /* diagnostic: every element as a standalone mono (SCE, LFE) / stereo (CPE) stream PREFIX.eN.asc / .aus (the element bits behind the indep bit, an LFE behind core_mode 0 and tns_data_present 0): FDK cannot read channel configurations above 2 */
        for (el = 0; el < nel; el++) {
            Bw a2; const unsigned cc = layout[el] == 1u ? 2u : 1u; memset(&a2, 0, sizeof a2);
            put(&a2, 31, 5); put(&a2, 42 - 32, 6); put(&a2, aac_index[o.rate_idx], 4); put(&a2, cc, 4); put(&a2, o.rate_idx, 5); put(&a2, o.len == 768u ? 0u : 1u, 3); put(&a2, cc, 5); put(&a2, 0, 4);
            put(&a2, layout[el] == 1u ? 1u : 0u, 2); put(&a2, 0, 1); put(&a2, layout[el] == 2u ? 0u : o.noise_fill, 1); put(&a2, 0, 1);
            snprintf(path, sizeof path, "%s.e%u.asc", prefix, el); fa = fopen(path, "wb"); fwrite(a2.b, 1, (a2.n + 7u) / 8u, fa); fclose(fa);
            snprintf(path, sizeof path, "%s.e%u.aus", prefix, el); g_elem_fu[el] = fopen(path, "wb");
        }
    }
    memset(elst, 0, sizeof elst);
    for (f = 0; f < o.frames; f++) {
        const int indep = f == 0 || f % 8u == 0;
        memset(&w, 0, sizeof w);
        put(&w, (uint32_t)indep, 1);
        for (el = 0; el < nel; el++) {
        ElState *es = &elst[el]; const size_t eb0 = w.n; ac = es->ac; prev_seq = es->prev_seq;
        memcpy(g_prev_re, es->prev_re, sizeof g_prev_re); memcpy(g_prev_im, es->prev_im, sizeof g_prev_im); g_prev_ngroups = es->prev_ngroups; g_last_seq = es->last_seq;
        if (layout[el] == 2u) {                                             /* LFE: no core_mode, no TNS, no noise filling, long windows only */
            Opt ol = o; Chan *c = &ch[0]; ol.noise_fill = 0; ol.tns = 0; ol.shorts = (unsigned)(g_lfe_short != 0);
            memset(c, 0, sizeof *c); c->seq = (g_lfe_short && f >= 1u) ? 2u : 0u; c->shape = 0; c->grouping = c->seq == 2u ? (unsigned)rr(0, 127) : 0u; setup(&ol, c); fill_content(&ol, c, 0); c->tns_data = 0;
            put_fd(&w, &ol, c, &ac[0], 0, indep, 0);
        } else if (layout[el] == 0u) {
            Chan *c = &ch[0]; memset(c, 0, sizeof *c);
            c->seq = next_seq(prev_seq[0], &o); prev_seq[0] = c->seq; c->shape = (o.kbd && (f > 0 || o.kbd_first)) ? ((f == 0) ? 1u : (rnd() & 1u)) : 0u;
            c->grouping = c->seq == 2u ? (unsigned)rr(0, 127) : 0u; setup(&o, c); fill_content(&o, c, 0);
            put(&w, 0, 1);                                                  /* core_mode */
            put(&w, c->tns_data, 1);
            put_fd(&w, &o, c, &ac[0], 0, indep, 1);
            if (o.sbr) put_sbr(&w, &o, 1, indep);
        } else {
            const int common = o.common ? 1 : (int)(rnd() % 2u);
            unsigned tns_active = o.tns && (rnd() % 2u), ms_mode = o.cplx ? (rnd() % 5u == 0 ? (unsigned)rr(0, 2) : 3u) : (o.ms ? (unsigned)rr(1, 2) : 0u), common_tns = 0, tns_on_lr = 0, max_ste, g, b;
            uint8_t mask[8][64];
            for (i = 0; i < 2u; i++) memset(&ch[i], 0, sizeof ch[i]);
            if (common) {
                ch[0].seq = ch[1].seq = next_seq(prev_seq[0], &o); prev_seq[0] = prev_seq[1] = ch[0].seq;
                ch[0].shape = ch[1].shape = (o.kbd && (f > 0 || o.kbd_first)) ? ((f == 0) ? 1u : (rnd() & 1u)) : 0u;
                ch[0].grouping = ch[1].grouping = ch[0].seq == 2u ? (unsigned)rr(0, 127) : 0u;
                setup(&o, &ch[0]); setup(&o, &ch[1]);
                fill_content(&o, &ch[0], 0);
                ch[1].max_sfb = (rnd() % 3u == 0) ? (unsigned)rr((int)(ch[0].nswb / 2u), (int)ch[0].nswb) : ch[0].max_sfb;
                if (o.full_sfb) ch[1].max_sfb = ch[0].max_sfb;
                fill_content(&o, &ch[1], 1);
                apply_probe(&ch[0], 0); apply_probe(&ch[1], 1);
            } else {
                const unsigned seq = next_seq(prev_seq[0], &o);                 /* both channels follow the same window sequence (a transition the standard forbids would make the decoders disagree) */
                prev_seq[0] = prev_seq[1] = seq;
                for (i = 0; i < 2u; i++) {
                    ch[i].seq = seq; ch[i].shape = (o.kbd && (f > 0 || o.kbd_first)) ? ((f == 0) ? 1u : (rnd() & 1u)) : 0u;
                    ch[i].grouping = ch[i].seq == 2u ? (unsigned)rr(0, 127) : 0u; setup(&o, &ch[i]); fill_content(&o, &ch[i], 0);
                }
            }
            put(&w, 0, 1); put(&w, 0, 1);                                   /* core_mode x2 */
            put(&w, tns_active, 1); put(&w, (uint32_t)common, 1);
            max_ste = ch[0].max_sfb > ch[1].max_sfb ? ch[0].max_sfb : ch[1].max_sfb;
            if (common) {
                put_ics_info(&w, &ch[0]);
                if (ch[1].max_sfb == ch[0].max_sfb) put(&w, 1, 1);
                else { put(&w, 0, 1); put(&w, ch[1].max_sfb, ch[1].seq == 2u ? 4u : 6u); }
                put(&w, ms_mode, 2);
                if (ms_mode == 1) for (g = 0; g < ch[0].ngroups; g++) for (b = 0; b < max_ste; b++) { mask[g][b] = (uint8_t)(rnd() & 1u); put(&w, mask[g][b], 1); }
                else if (ms_mode == 3) put_cplx(&w, &ch[0], max_ste, indep);
            }
            if (tns_active) {
                if (common) { common_tns = rnd() & 1u; put(&w, common_tns, 1); }
                tns_on_lr = rnd() & 1u; if (g_force_on_lr >= 0) tns_on_lr = (unsigned)g_force_on_lr; put(&w, tns_on_lr, 1);
                if (common_tns) { ch[0].tns_data = 1; ch[1].tns_data = 0; put_tns(&w, &ch[0]); }
                else {
                    const unsigned both = (unsigned)(rnd() & 1u);
                    put(&w, both, 1);
                    if (both) ch[0].tns_data = ch[1].tns_data = 1;
                    else { const unsigned second = rnd() & 1u; put(&w, second, 1); ch[1].tns_data = second; ch[0].tns_data = !second; }
                }
            } else ch[0].tns_data = ch[1].tns_data = 0;
            if (getenv("SAUTI_USAC_GEN_TRACE")) fprintf(stderr, "FRAME %u common %d ms %u tns_active %u common_tns %u on_lr %u tns_data %u/%u seq %u\n", f, common, ms_mode, tns_active, common_tns, tns_on_lr, ch[0].tns_data, ch[1].tns_data, ch[0].seq);
            put_fd(&w, &o, &ch[0], &ac[0], common, indep, !common_tns);
            put_fd(&w, &o, &ch[1], &ac[1], common, indep, !common_tns);
            if (o.sbr) put_sbr(&w, &o, 2, indep);
            if (!(common && ms_mode == 3)) { memset(g_prev_re, 0, sizeof g_prev_re); memset(g_prev_im, 0, sizeof g_prev_im); }          /* the decoder's state after a frame without complex prediction */
            if (common) g_prev_ngroups = ch[0].ngroups;
            g_last_seq = ch[0].seq;
        }
        if (g_elem_fu[el]) {
            Bw wl; size_t k; uint32_t n; uint8_t be[4]; memset(&wl, 0, sizeof wl);
            put(&wl, (uint32_t)indep, 1); if (layout[el] == 2u) { put(&wl, 0, 1); put(&wl, 0, 1); }
            for (k = eb0; k < w.n; k++) put(&wl, (uint32_t)((w.b[k >> 3] >> (7u - (k & 7u))) & 1u), 1);
            n = (uint32_t)((wl.n + 7u) / 8u); be[0] = (uint8_t)(n >> 24); be[1] = (uint8_t)(n >> 16); be[2] = (uint8_t)(n >> 8); be[3] = (uint8_t)n; fwrite(be, 1, 4, g_elem_fu[el]); fwrite(wl.b, 1, n, g_elem_fu[el]);
        }
        memcpy(es->prev_re, g_prev_re, sizeof g_prev_re); memcpy(es->prev_im, g_prev_im, sizeof g_prev_im); es->prev_ngroups = g_prev_ngroups; es->last_seq = g_last_seq;
        }
        { uint32_t n = (uint32_t)((w.n + 7u) / 8u); uint8_t be[4] = { (uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n }; fwrite(be, 1, 4, fu); fwrite(w.b, 1, n, fu); }
    }
    fclose(fu); for (el = 0; el < nel; el++) if (g_elem_fu[el]) fclose(g_elem_fu[el]);
    (void)rates;
    fprintf(stderr, "wrote %u frames: %u channel(s), %u lines, rate index %u\n", o.frames, o.channels, o.len, o.rate_idx);
    return 0;
}
