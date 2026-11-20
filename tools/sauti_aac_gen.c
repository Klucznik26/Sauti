/* Test stream generator (tool, not a product): writes syntactically valid AAC access units from RANDOM QUANTISED SPECTRA, so profiles and frame lengths that no available encoder emits can be
 * decoded by the reference decoders (FFmpeg, FDK) and by Sauti and compared.  It is an encoder's bit writer without the psychoacoustics: it picks section partitions, codebooks,
 * scalefactors and (optionally) TNS filters and Mid/Side flags at random, entropy-codes the spectrum with the standard codebooks (the same tables the decoder uses, symbol -> codeword),
 * and checks its own work by decoding every frame with the Sauti parser and comparing the quantised values.
 *
 * usage: sauti_aac_gen OUT_PREFIX [options]   ->  OUT_PREFIX.asc (AudioSpecificConfig) and OUT_PREFIX.aus (per access unit: 4-byte big-endian length + bytes; pass to tools/mux_raw_aus_mp4.py)
 *   --frame N        1024 (default), 960   (AAC-LC long windows only)
 *   --channels N     1 or 2 (default 2)
 *   --rate I         sampling_frequency_index (default 3 = 48 kHz)
 *   --frames N       access units (default 200)
 *   --seed S         random seed (default 1)
 *   --ms             Mid/Side stereo on random bands (CPE, common_window)
 *   --tns            random TNS filters on long windows
 *   --short          random window sequences: EIGHT_SHORT with random grouping, LONG_START / LONG_STOP transitions
 *   --kbd            random window shape (sine / Kaiser-Bessel derived) per frame
 *   --quiet Q        lowers global_gain by Q steps (4 steps = 6 dB): the default levels are far above full scale (peak ~100), which clips in fixed-point decoders such as FDK; 36 keeps the peak below 1
 *   --level G        global_gain and every scalefactor kept within G..G+12 (3 octaves) instead of a random walk over ~10 octaves: a controlled level; 136 gives a peak of about 0.3 with the default amplitude
 *   --pns            one band in six becomes a perceptual-noise-substitution band (codebook 13); --pns-den N: one in N (1 = every band)
 *   --ms-all         M/S stereo with ms_mask_present = 2 (every band), implies --ms
 *   --amp A          maximum quantised magnitude (default 12; > 15 uses escape codes)
 * build: make build/sauti_aac_gen */
#include "sauti_aac_bands.h"
#include "sauti_aac_layout.h"
#include "sauti_aac_spectral.h"
#include "sauti_aac_tables.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- bit writer ------------------------------------------------------------------------------------------------------------------------------------------------ */
typedef struct { uint8_t b[16384]; size_t n; } Bw;
static void put(Bw *w, uint32_t v, unsigned bits) { while (bits--) { if ((w->n & 7u) == 0) w->b[w->n >> 3] = 0; if ((v >> bits) & 1u) w->b[w->n >> 3] |= (uint8_t)(0x80u >> (w->n & 7u)); w->n++; } }

static uint32_t g_rng = 1;
static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static int rr(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }

/* ---- tuple -> codebook index ------------------------------------------------------------------------------------------------------------------------------------ */
static int tuple_index(unsigned book, const int *v, unsigned *index) {
    static int16_t table[12][289][4]; static int built[12]; static unsigned count[12] = { 0, 81, 81, 81, 81, 81, 81, 64, 64, 169, 169, 289 };
    unsigned i, k, dim = book <= 4 ? 4u : 2u;
    if (!built[book]) { for (i = 0; i < count[book]; i++) if (!sauti_aac_spectral_index_asm(book, i, table[book][i])) return 0; built[book] = 1; }
    for (i = 0; i < count[book]; i++) { int same = 1; for (k = 0; k < dim; k++) if (table[book][i][k] != v[k]) same = 0; if (same) { *index = i; return 1; } }
    return 0;
}
static int is_unsigned_book(unsigned b) { return b == 3 || b == 4 || b >= 7; }
static unsigned book_for_max(unsigned mx) { if (mx == 0) return 0; if (mx <= 1) return 1 + rnd() % 2; if (mx <= 2) return 3 + rnd() % 2; if (mx <= 4) return 5 + rnd() % 2; if (mx <= 7) return 7 + rnd() % 2; if (mx <= 12) return 9 + rnd() % 2; return 11; }

static void put_codeword(Bw *w, unsigned book, unsigned symbol) { uint32_t code; uint8_t bits; if (!sauti_aac_standard_codeword(book, symbol, &code, &bits)) { fprintf(stderr, "no codeword book %u symbol %u\n", book, symbol); exit(1); } put(w, code, bits); }

static void put_escape(Bw *w, unsigned magnitude) {                                      /* magnitude >= 16 */
    unsigned n = 4; while ((magnitude >> (n + 1)) != 0) n++;
    { unsigned k; for (k = 4; k < n; k++) put(w, 1, 1); put(w, 0, 1); put(w, magnitude - (1u << n), n); }
}
static void put_spectrum(Bw *w, unsigned book, const int *q, unsigned n) {            /* n lines of one section */
    unsigned i, dim = book <= 4 ? 4u : 2u;
    for (i = 0; i < n; i += dim) {
        int v[4], c[4]; unsigned index, k;
        for (k = 0; k < dim; k++) v[k] = q[i + k];
        for (k = 0; k < dim; k++) c[k] = is_unsigned_book(book) ? (book == 11 && abs(v[k]) > 16 ? 16 : abs(v[k])) : v[k];       /* unsigned books: magnitudes in the codeword, signs follow */
        if (!tuple_index(book, c, &index)) { fprintf(stderr, "tuple not codable in book %u\n", book); exit(1); }
        put_codeword(w, book, index);
        if (is_unsigned_book(book)) for (k = 0; k < dim; k++) if (v[k]) put(w, v[k] < 0 ? 1u : 0u, 1);
        if (book == 11) for (k = 0; k < dim; k++) if (abs(v[k]) >= 16) put_escape(w, (unsigned)abs(v[k]));
    }
}

/* ---- channel stream ------------------------------------------------------------------------------------------------------------------------------------------------ */
typedef struct {
    int q[1024];                                   /* natural layout: [window][line] (one window for long sequences) */
    unsigned seq, shape, max_sfb, groups, group_len[8];
    unsigned book[8][64]; int sf[8][64]; unsigned gain; int tns; unsigned tns_len, tns_order, tns_dir; int tns_coef[20];
} Chan;
typedef struct { unsigned frame, rate, channels, ms, msall, tns, pns, amp, shorts, kbd, quiet, level; const uint16_t *off, *soff; unsigned bands, sbands; } Cfg;

static unsigned g_prev_seq;                                                         /* window sequence of the previous frame (shared by both channels: common_window) */
static void pick_sequence(const Cfg *c, unsigned *seq) {
    unsigned s;
    if (!c->shorts) { *seq = 0; return; }
    switch (g_prev_seq) {
    case 0: case 3: s = (rnd() % 4u == 0) ? 1u : 0u; break;                          /* LONG -> LONG or LONG_START; STOP -> LONG or LONG_START */
    case 1: s = 2u; break;                                                           /* LONG_START -> EIGHT_SHORT */
    default: s = (rnd() % 3u == 0) ? 3u : 2u; break;                                 /* EIGHT_SHORT -> EIGHT_SHORT or LONG_STOP */
    }
    *seq = s; g_prev_seq = s;
}
static void make_channel(const Cfg *c, Chan *ch, unsigned seq, unsigned shape, unsigned max_sfb, const unsigned *group_len, unsigned groups) {
    unsigned b, i, g, w, first_window; int last_sf, last_nv;
    const int is_short = seq == 2u;
    const unsigned ns = c->frame / 8u;
    memset(ch, 0, sizeof *ch);
    ch->seq = seq; ch->shape = shape; ch->max_sfb = max_sfb; ch->groups = groups;
    for (g = 0; g < groups; g++) ch->group_len[g] = group_len[g];
    ch->gain = c->level ? (unsigned)rr((int)c->level, (int)c->level + 12) : (unsigned)rr(110, 150) - c->quiet; last_sf = (int)ch->gain; last_nv = (int)ch->gain - 90; first_window = 0;
    for (g = 0; g < groups; g++) {
        for (b = 0; b < max_sfb; b++) {
            const uint16_t *off = is_short ? c->soff : c->off; const unsigned lo = off[b], hi = off[b + 1]; unsigned mx = 0; const int klass = rr(0, 9);
            const unsigned lim = klass < 3 ? 0u : klass < 6 ? 2u : klass < 8 ? 7u : c->amp;
            for (w = first_window; w < first_window + group_len[g]; w++)
                for (i = lo; i < hi; i++) {
                    int v = 0; if (lim && (rnd() % 3) != 0) { v = rr(1, (int)lim); if (rnd() & 1) v = -v; }
                    ch->q[(is_short ? w * ns : 0) + i] = v; if ((unsigned)abs(v) > mx) mx = (unsigned)abs(v);
                }
            ch->book[g][b] = book_for_max(mx);
            if (c->pns && rr(0, (int)c->pns - 1) == 0) {                                              /* perceptual noise substitution: no spectral data, the band is filled with noise of the coded energy */
                int nv = (int)ch->gain - 100 + rr(-8, 8); if (nv < last_nv - 60) nv = last_nv - 60; if (nv > last_nv + 60) nv = last_nv + 60;
                for (w = first_window; w < first_window + group_len[g]; w++) for (i = lo; i < hi; i++) ch->q[(is_short ? w * ns : 0) + i] = 0;
                ch->book[g][b] = 13u; ch->sf[g][b] = nv; last_nv = nv;
            } else
            if (ch->book[g][b]) { int sf = last_sf + rr(-6, 6); if (sf < 60) sf = 60; if (sf > 200) sf = 200; if (c->level) { if (sf < (int)c->level) sf = (int)c->level; if (sf > (int)c->level + 12) sf = (int)c->level + 12; } ch->sf[g][b] = sf; last_sf = sf; }
        }
        first_window += group_len[g];
    }
    if (c->tns) {
        ch->tns = rr(0, 2) != 0; ch->tns_dir = rnd() & 1u;
        if (!is_short) { ch->tns_len = (unsigned)rr(8, (int)(c->bands < 30 ? c->bands : 30)); ch->tns_order = (unsigned)rr(1, 8); }
        else { ch->tns_len = (unsigned)rr(3, (int)(c->sbands < 15 ? c->sbands : 15)); ch->tns_order = (unsigned)rr(1, 7); }    /* short windows: the same filter in every window */
        for (i = 0; i < ch->tns_order; i++) ch->tns_coef[i] = rr(-3, 3);
    }
}
static void put_ics_info(Bw *w, const Chan *ch) {
    put(w, 0, 1); put(w, ch->seq, 2); put(w, ch->shape, 1);
    if (ch->seq == 2u) {
        unsigned g, k;
        put(w, ch->max_sfb, 4);
        { unsigned win = 0; for (g = 0; g < ch->groups; g++) for (k = 0; k < ch->group_len[g]; k++, win++) if (win > 0) put(w, k > 0 ? 1u : 0u, 1); }    /* window i+1 joins the group of window i */
    } else { put(w, ch->max_sfb, 6); put(w, 0, 1); }                                    /* max_sfb, no predictor */
}
static void put_sections(Bw *w, const Chan *ch) {
    const unsigned bits = ch->seq == 2u ? 3u : 5u, esc = (1u << bits) - 1u; unsigned g, b;
    for (g = 0; g < ch->groups; g++) {
        b = 0;
        while (b < ch->max_sfb) {
            unsigned e = b + 1, len; while (e < ch->max_sfb && ch->book[g][e] == ch->book[g][b]) e++;
            put(w, ch->book[g][b], 4); len = e - b; while (len >= esc) { put(w, esc, bits); len -= esc; } put(w, len, bits);
            b = e;
        }
    }
}
static void put_scalefactors(Bw *w, const Chan *ch) {
    int prev = (int)ch->gain, prev_nv = (int)ch->gain - 90, first_noise = 1; unsigned b, g;
    for (g = 0; g < ch->groups; g++) for (b = 0; b < ch->max_sfb; b++) {
        if (ch->book[g][b] == 13u) {                                                    /* noise energy: the first one as a 9-bit offset, the rest as scalefactor deltas */
            if (first_noise) { put(w, (uint32_t)(ch->sf[g][b] - prev_nv + 256), 9); first_noise = 0; }
            else put_codeword(w, 0, (unsigned)(ch->sf[g][b] - prev_nv + 60));
            prev_nv = ch->sf[g][b];
        } else if (ch->book[g][b]) { put_codeword(w, 0, (unsigned)(ch->sf[g][b] - prev + 60)); prev = ch->sf[g][b]; }
    }
}
static void put_tns(Bw *w, const Chan *ch) {                                              /* long window: n_filt 2 bits, coef_res 1, length 6, order 5, direction 1, compress 1, coefficients; short: per window n_filt 1, coef_res 1, length 4, order 3 */
    unsigned i, win;
    if (ch->seq != 2u) {
        put(w, 1, 2); put(w, 1, 1); put(w, ch->tns_len, 6); put(w, ch->tns_order, 5); put(w, ch->tns_dir, 1); put(w, 0, 1);
        for (i = 0; i < ch->tns_order; i++) put(w, (uint32_t)(ch->tns_coef[i] & 15), 4);
        return;
    }
    for (win = 0; win < 8; win++) {
        put(w, 1, 1); put(w, 1, 1); put(w, ch->tns_len, 4); put(w, ch->tns_order, 3); put(w, ch->tns_dir, 1); put(w, 0, 1);
        for (i = 0; i < ch->tns_order; i++) put(w, (uint32_t)(ch->tns_coef[i] & 15), 4);
    }
}
static void put_channel(Bw *w, const Cfg *c, const Chan *ch, int with_ics_info) {
    unsigned b, g, first_window = 0; const unsigned ns = c->frame / 8u; const int is_short = ch->seq == 2u;
    put(w, ch->gain, 8);
    if (with_ics_info) put_ics_info(w, ch);
    put_sections(w, ch); put_scalefactors(w, ch);
    put(w, 0, 1);                                                                       /* pulse_data_present */
    put(w, ch->tns ? 1u : 0u, 1);
    if (ch->tns) put_tns(w, ch);
    put(w, 0, 1);                                                                       /* gain_control_data_present */
    for (g = 0; g < ch->groups; g++) {                                                  /* spectral data: group by group, section by section; inside a band the windows of the group follow each other */
        b = 0;
        while (b < ch->max_sfb) {
            unsigned e = b + 1, band, win; while (e < ch->max_sfb && ch->book[g][e] == ch->book[g][b]) e++;
            if (ch->book[g][b] && ch->book[g][b] != 13u) {
                for (band = b; band < e; band++) {
                    const uint16_t *off = is_short ? c->soff : c->off;
                    for (win = first_window; win < first_window + ch->group_len[g]; win++)
                        put_spectrum(w, ch->book[g][b], ch->q + (is_short ? win * ns : 0) + off[band], off[band + 1] - off[band]);
                }
            }
            b = e;
        }
        first_window += ch->group_len[g];
    }
}

/* ---- main ------------------------------------------------------------------------------------------------------------------------------------------------------------ */
int main(int argc, char **argv) {
    Cfg c; unsigned frames = 200, seed = 1, f; char path[512]; FILE *fa, *fb; int a; Bw asc;
    memset(&c, 0, sizeof c); c.frame = 1024; c.rate = 3; c.channels = 2; c.amp = 12;
    if (argc < 2) return 2;
    for (a = 2; a < argc; a++) {
        if (!strcmp(argv[a], "--frame") && a + 1 < argc) c.frame = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--channels") && a + 1 < argc) c.channels = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--rate") && a + 1 < argc) c.rate = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--frames") && a + 1 < argc) frames = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--seed") && a + 1 < argc) seed = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--amp") && a + 1 < argc) c.amp = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--quiet") && a + 1 < argc) c.quiet = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--level") && a + 1 < argc) c.level = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--pns")) c.pns = 6;
        else if (!strcmp(argv[a], "--pns-den") && a + 1 < argc) c.pns = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--ms-all")) { c.ms = 1; c.msall = 1; }
        else if (!strcmp(argv[a], "--ms")) c.ms = 1;
        else if (!strcmp(argv[a], "--tns")) c.tns = 1;
        else if (!strcmp(argv[a], "--short")) c.shorts = 1;
        else if (!strcmp(argv[a], "--kbd")) c.kbd = 1;
        else { fprintf(stderr, "unknown option %s\n", argv[a]); return 2; }
    }
    if ((c.frame != 1024 && c.frame != 960) || c.channels < 1 || c.channels > 2 || c.rate > 12) return 2;
    g_rng = seed * 2654435761u + 12345u; if (!g_rng) g_rng = 1;
    { const unsigned lay = c.frame == 960 ? SAUTI_AAC_LAYOUT_960 : SAUTI_AAC_LAYOUT_1024;
      if (!sauti_aac_band_offsets(SAUTI_AAC_IDX(c.rate, lay), 0, &c.off, &c.bands) || !sauti_aac_band_offsets(SAUTI_AAC_IDX(c.rate, lay), 1, &c.soff, &c.sbands)) { fprintf(stderr, "no band table for this frame length / rate\n"); return 2; } }
    /* AudioSpecificConfig: AOT 2, rate, channel config, frameLengthFlag, dependsOnCoreCoder 0, extensionFlag 0 */
    memset(&asc, 0, sizeof asc); put(&asc, 2, 5); put(&asc, c.rate, 4); put(&asc, c.channels, 4); put(&asc, c.frame == 960 ? 1u : 0u, 1); put(&asc, 0, 1); put(&asc, 0, 1);
    snprintf(path, sizeof path, "%s.asc", argv[1]); fa = fopen(path, "wb"); if (!fa) return 2; fwrite(asc.b, 1, (asc.n + 7) / 8, fa); fclose(fa);
    snprintf(path, sizeof path, "%s.aus", argv[1]); fb = fopen(path, "wb"); if (!fb) return 2;
    for (f = 0; f < frames; f++) {
        static Bw w; Chan ch[2]; size_t len; unsigned seq, shape, max_sfb, groups, group_len[8], k;
        memset(&w, 0, sizeof w);
        pick_sequence(&c, &seq); shape = c.kbd ? (rnd() & 1u) : 0u;
        if (seq == 2u) {
            unsigned win = 0; groups = 0;
            for (k = 0; k < 8; k++) { if (k == 0 || (rnd() % 3u) == 0) group_len[groups++] = 1; else group_len[groups - 1]++; win++; }
            max_sfb = (c.sbands > 4 && (rnd() & 3u)) ? (unsigned)rr((int)c.sbands / 2, (int)c.sbands) : c.sbands; if (max_sfb > 15u) max_sfb = 15u; (void)win;
        } else { groups = 1; group_len[0] = 1; max_sfb = (c.bands > 8 && (rnd() & 3u)) ? (unsigned)rr((int)c.bands / 2, (int)c.bands) : c.bands; }
        if (c.channels == 1) { make_channel(&c, &ch[0], seq, shape, max_sfb, group_len, groups); put(&w, 0, 3); put(&w, 0, 4); put_channel(&w, &c, &ch[0], 1); }
        else {
            unsigned ms_mask = 0, b, g; uint8_t used[8][64] = { { 0 } };
            make_channel(&c, &ch[0], seq, shape, max_sfb, group_len, groups); make_channel(&c, &ch[1], seq, shape, max_sfb, group_len, groups);
            if (c.ms) { ms_mask = c.msall ? 2u : (unsigned)rr(0, 2); for (g = 0; g < groups; g++) for (b = 0; b < max_sfb; b++) used[g][b] = (uint8_t)(ms_mask == 2 ? 1 : (ms_mask == 1 ? (rnd() & 1u) : 0u)); }
            put(&w, 1, 3); put(&w, 0, 4); put(&w, 1, 1);                                  /* ID_CPE, tag, common_window */
            put_ics_info(&w, &ch[0]); put(&w, ms_mask, 2);
            if (ms_mask == 1) for (g = 0; g < groups; g++) for (b = 0; b < max_sfb; b++) put(&w, used[g][b], 1);
            put_channel(&w, &c, &ch[0], 0); put_channel(&w, &c, &ch[1], 0);
        }
        put(&w, 7, 3);                                                                   /* ID_END */
        len = (w.n + 7) / 8;
        { uint8_t hdr[4] = { (uint8_t)(len >> 24), (uint8_t)(len >> 16), (uint8_t)(len >> 8), (uint8_t)len }; fwrite(hdr, 1, 4, fb); fwrite(w.b, 1, len, fb); }
    }
    fclose(fb);
    fprintf(stderr, "wrote %u frames: frame %u, %u channel(s), rate index %u, %u scalefactor bands\n", frames, c.frame, c.channels, c.rate, c.bands);
    return 0;
}
