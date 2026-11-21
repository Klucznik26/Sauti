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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t b[1 << 18]; size_t n; } Bw;
static void put(Bw *w, uint32_t v, unsigned bits) { while (bits--) { if ((w->n & 7u) == 0) w->b[w->n >> 3] = 0; if ((v >> bits) & 1u) w->b[w->n >> 3] |= (uint8_t)(0x80u >> (w->n & 7u)); w->n++; } }
static int g_probe_ch = -1, g_probe_line = 100;
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
typedef struct { unsigned channels, rate_idx, len, frames, noise_fill, ms, common, tns, shorts, kbd, kbd_first, full_sfb, dense, zero_bands; int gain, amp; } Opt;
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
        if (o->len == 768u) { c->swb = sauti_usac_swb96[o->rate_idx]; count = sauti_usac_num_swb96[o->rate_idx]; }
        else sauti_aac_band_offsets(o->rate_idx, 1, &c->swb, &count);
        c->tns_max = sauti_usac_tns_max_bands_128[o->rate_idx]; c->wlen = o->len / 8u; c->stride = 128u; c->nwin = 8u;
        c->ngroups = 0; for (j = 0; j < 8u; j++) c->glen[j] = 0; c->glen[0] = 1;
        for (j = 0; j < 7u; j++) { if (c->grouping & (1u << (6u - j))) c->glen[c->ngroups]++; else { c->ngroups++; c->glen[c->ngroups] = 1; } }
        c->ngroups++;
    } else {
        if (o->len == 768u) { c->swb = sauti_usac_swb768[o->rate_idx]; count = sauti_usac_num_swb768[o->rate_idx]; }
        else sauti_aac_band_offsets(o->rate_idx, 0, &c->swb, &count);
        c->tns_max = sauti_usac_tns_max_bands_1024[o->rate_idx]; c->wlen = o->len; c->stride = o->len; c->nwin = 1u; c->ngroups = 1u; c->glen[0] = 1u;
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

int main(int argc, char **argv) {
    Opt o; const char *prefix; int a; unsigned f, i;
    Bw w, asc; FILE *fa, *fu; char path[1024];
    Chan ch[2]; SautiUsacAcState ac[2]; unsigned prev_seq[2] = { 0, 0 };
    const unsigned rates[13] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };
    if (argc < 2) { fprintf(stderr, "usage: sauti_usac_gen PREFIX [options]\n"); return 2; }
    prefix = argv[1];
    memset(&o, 0, sizeof o); o.channels = 1; o.rate_idx = 3; o.len = 1024; o.frames = 20; o.gain = 90; o.amp = 12;
    for (a = 2; a < argc; a++) {
        if (!strcmp(argv[a], "--channels") && a + 1 < argc) o.channels = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--rate") && a + 1 < argc) o.rate_idx = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--len") && a + 1 < argc) o.len = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--frames") && a + 1 < argc) o.frames = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--seed") && a + 1 < argc) g_rng = (uint32_t)atoi(argv[++a]) * 2654435761u + 1u;
        else if (!strcmp(argv[a], "--gain") && a + 1 < argc) o.gain = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--amp") && a + 1 < argc) o.amp = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--noise-fill")) o.noise_fill = 1;
        else if (!strcmp(argv[a], "--ms")) o.ms = 1;
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
    if (o.channels < 1 || o.channels > 2 || (o.len != 1024 && o.len != 768) || o.rate_idx > 12) { fprintf(stderr, "bad options\n"); return 2; }
    if (o.ms) o.common = 1;
    /* AudioSpecificConfig: object type 42 (escape), sampling index, channel configuration, UsacConfig */
    memset(&asc, 0, sizeof asc);
    put(&asc, 31, 5); put(&asc, 42 - 32, 6); put(&asc, aac_index[o.rate_idx], 4); put(&asc, o.channels, 4);
    put(&asc, o.rate_idx, 5); put(&asc, o.len == 768u ? 0u : 1u, 3); put(&asc, o.channels, 5);
    put(&asc, 0, 4);                                                       /* numElements - 1 = 0 (escapedValue(4, 8, 16)) */
    put(&asc, o.channels == 1u ? 0u : 1u, 2); put(&asc, 0, 1); put(&asc, o.noise_fill, 1);   /* SCE / CPE, tw_mdct = 0, noiseFilling */
    put(&asc, 0, 1);                                                       /* usacConfigExtensionPresent */
    snprintf(path, sizeof path, "%s.asc", prefix); fa = fopen(path, "wb"); fwrite(asc.b, 1, (asc.n + 7u) / 8u, fa); fclose(fa);
    snprintf(path, sizeof path, "%s.aus", prefix); fu = fopen(path, "wb");
    memset(ac, 0, sizeof ac);
    for (f = 0; f < o.frames; f++) {
        const int indep = f == 0 || f % 8u == 0;
        memset(&w, 0, sizeof w);
        put(&w, (uint32_t)indep, 1);
        if (o.channels == 1u) {
            Chan *c = &ch[0]; memset(c, 0, sizeof *c);
            c->seq = next_seq(prev_seq[0], &o); prev_seq[0] = c->seq; c->shape = (o.kbd && (f > 0 || o.kbd_first)) ? ((f == 0) ? 1u : (rnd() & 1u)) : 0u;
            c->grouping = c->seq == 2u ? (unsigned)rr(0, 127) : 0u; setup(&o, c); fill_content(&o, c, 0);
            put(&w, 0, 1);                                                  /* core_mode */
            put(&w, c->tns_data, 1);
            put_fd(&w, &o, c, &ac[0], 0, indep, 1);
        } else {
            const int common = o.common ? 1 : (int)(rnd() % 2u);
            unsigned tns_active = o.tns && (rnd() % 2u), ms_mode = o.ms ? (unsigned)rr(1, 2) : 0u, common_tns = 0, tns_on_lr = 0, max_ste, g, b;
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
        }
        { uint32_t n = (uint32_t)((w.n + 7u) / 8u); uint8_t be[4] = { (uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n }; fwrite(be, 1, 4, fu); fwrite(w.b, 1, n, fu); }
    }
    fclose(fu);
    (void)rates;
    fprintf(stderr, "wrote %u frames: %u channel(s), %u lines, rate index %u\n", o.frames, o.channels, o.len, o.rate_idx);
    return 0;
}
