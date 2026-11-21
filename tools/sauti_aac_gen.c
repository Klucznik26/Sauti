/* Test stream generator (tool, not a product): writes syntactically valid AAC access units from RANDOM QUANTISED SPECTRA, so profiles and frame lengths that no available encoder emits can be
 * decoded by the reference decoders (FFmpeg, FDK) and by Sauti and compared.  It is an encoder's bit writer without the psychoacoustics: it picks section partitions, codebooks,
 * scalefactors and (optionally) TNS filters and Mid/Side flags at random, entropy-codes the spectrum with the standard codebooks (the same tables the decoder uses, symbol -> codeword),
 * and checks its own work by decoding every frame with the Sauti parser and comparing the quantised values.
 *
 * usage: sauti_aac_gen OUT_PREFIX [options]   ->  OUT_PREFIX.crcinfo (per access unit: the bit offset of the second channel of a CPE, 0xffffffff for an SCE: the ADTS error check needs it) and OUT_PREFIX.asc (AudioSpecificConfig) and OUT_PREFIX.aus (per access unit: 4-byte big-endian length + bytes; pass to tools/mux_raw_aus_mp4.py)
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
 *   --full-sfb       max_sfb always the full number of bands (constant across frames)
 *   --pns-quiet      noise bands 20 octaves below the signal (the noise itself is negligible in the PCM; for tests of what PNS does to other tools)
 *   --ltp            AAC LTP (object type 4): random ltp_data (lag 0..2047, gain index, used flags) in the long windows; --ltp-max-coef I limits the gain index (0..7)
 *   --peak P         AAC LTP: closed loop, the generator decodes its own output and switches the long-term prediction off for two frames whenever a frame peaks above P (default 0.6), a guard
 *                    against run-away feedback through the long-term gain (not observed with the full gain range over 600 frames at --level 130)
 *   --er             error-resilient syntax without resilience tools: object type 17 (ER AAC LC), 19 with --ltp (ER AAC LTP); no element ids, no ID_END, TNS data after the gain-control flag
 *   --main           AAC Main (object type 1): random predictor_data (prediction_used flags, resets) in the long windows
 *   --level G        global_gain and every scalefactor kept within G..G+12 (3 octaves) instead of a random walk over ~10 octaves: a controlled level; 136 gives a peak of about 0.3 with the default amplitude
 *   --pns            one band in six becomes a perceptual-noise-substitution band (codebook 13); --pns-den N: one in N (1 = every band)
 *   --ms-all         M/S stereo with ms_mask_present = 2 (every band), implies --ms
 *   --amp A          maximum quantised magnitude (default 12; > 15 uses escape codes)
 * build: make build/sauti_aac_gen */
#include "sauti_aac_bands.h"
#include "sauti_aac_layout.h"
#include "sauti_aac_spectral.h"
#include "sauti_aac_syntax.h"
#include "sauti_aac_lcn.h"
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
    unsigned pred, pred_n, pred_reset, pred_group; uint8_t pred_used[41];            /* AAC Main predictor_data of the (shared) ics_info */
    unsigned ltp_mode, ltp_present, ltp_lag, ltp_coef, ltp_n; uint8_t ltp_used[40];            /* AAC LTP ltp_data (own to each channel) */
    unsigned seq, shape, max_sfb, groups, group_len[8];
    unsigned book[8][64]; int sf[8][64]; unsigned gain; int tns; unsigned tns_len, tns_order, tns_dir; int tns_coef[20];
} Chan;
typedef struct { unsigned frame, rate, channels, config, er, main, ltp, ltpmax, peakmilli, predmode, fullsfb, pnsquiet, ms, msall, tns, pns, amp, shorts, kbd, quiet, level; const uint16_t *off, *soff; unsigned bands, sbands; } Cfg;

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
                int nv = (int)ch->gain - 100 + rr(-8, 8) - (c->pnsquiet ? 80 : 0); if (nv < last_nv - 60) nv = last_nv - 60; if (nv > last_nv + 60) nv = last_nv + 60;
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
static void fill_pred(const Cfg *c, Chan *ch) {                                         /* random predictor_data for a long window of an AAC Main stream */
    const unsigned limit = sauti_aac_pred_sfb_max(c->rate); unsigned i;
    ch->pred_n = ch->max_sfb < limit ? ch->max_sfb : limit;
    ch->pred = rr(0, 3) != 0; ch->pred_reset = ch->pred && rr(0, 3) == 0; ch->pred_group = (unsigned)rr(1, 30);
    for (i = 0; i < ch->pred_n; i++) ch->pred_used[i] = rr(0, 3) != 0;
    if (c->predmode == 1) ch->pred = 0;                                                  /* Main syntax, predictor_data_present = 0 */
    if (c->predmode == 2) { ch->pred = 1; ch->pred_reset = 0; for (i = 0; i < ch->pred_n; i++) ch->pred_used[i] = 0; }     /* present, nothing used, no reset */
    if (c->predmode == 3) { ch->pred = 1; ch->pred_reset = 0; for (i = 0; i < ch->pred_n; i++) ch->pred_used[i] = 1; }     /* everything used, no reset */
    if (c->predmode == 4) { ch->pred = 1; ch->pred_reset = 1; for (i = 0; i < ch->pred_n; i++) ch->pred_used[i] = 1; }     /* everything used, with reset groups */
}
static unsigned g_ltp_off;                                                          /* frames left with the long-term prediction switched off (closed-loop limiter) */
static void fill_ltp(const Cfg *c, Chan *ch) {                                          /* random ltp_data for a long window of an AAC LTP stream */
    unsigned i;
    ch->ltp_mode = 1; ch->ltp_n = ch->max_sfb < 40u ? ch->max_sfb : 40u;
    ch->pred = rr(0, 3) != 0; ch->ltp_present = rr(0, 3) != 0;
    if (g_ltp_off) { ch->pred = 0; ch->ltp_present = 0; } ch->ltp_lag = (unsigned)rr(0, 2047); ch->ltp_coef = (unsigned)rr(0, (int)c->ltpmax);
    for (i = 0; i < ch->ltp_n; i++) ch->ltp_used[i] = rr(0, 2) != 0;
}
static void put_ltp(Bw *w, const Chan *ch) {                                            /* ltp_data_present, then ltp_data */
    unsigned i;
    put(w, ch->ltp_present, 1);
    if (ch->ltp_present) { put(w, ch->ltp_lag, 11); put(w, ch->ltp_coef, 3); for (i = 0; i < ch->ltp_n; i++) put(w, ch->ltp_used[i], 1); }
}
static void put_ics_info(Bw *w, const Chan *ch) {
    put(w, 0, 1); put(w, ch->seq, 2); put(w, ch->shape, 1);
    if (ch->seq == 2u) {
        unsigned g, k;
        put(w, ch->max_sfb, 4);
        { unsigned win = 0; for (g = 0; g < ch->groups; g++) for (k = 0; k < ch->group_len[g]; k++, win++) if (win > 0) put(w, k > 0 ? 1u : 0u, 1); }    /* window i+1 joins the group of window i */
    } else {
        unsigned i;
        put(w, ch->max_sfb, 6); put(w, ch->pred, 1);                                    /* max_sfb, predictor_data_present */
        if (ch->pred && ch->ltp_mode) put_ltp(w, ch);
        else if (ch->pred) { put(w, ch->pred_reset, 1); if (ch->pred_reset) put(w, ch->pred_group, 5); for (i = 0; i < ch->pred_n; i++) put(w, ch->pred_used[i], 1); }
    }
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
    if (ch->tns && !c->er) put_tns(w, ch);
    put(w, 0, 1);                                                                       /* gain_control_data_present */
    if (ch->tns && c->er) put_tns(w, ch);                                              /* ER syntax: tns_data after the gain-control flag */
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
/* one single_channel_element (id 0) or lfe_element (id 3) of a frame */
static void emit_sce(Bw *w, const Cfg *c, unsigned id, unsigned tag, unsigned seq, unsigned shape, unsigned max_sfb, const unsigned *group_len, unsigned groups) {
    Chan ch;
    make_channel(c, &ch, seq, shape, max_sfb, group_len, groups);
    if (c->main && seq != 2u) fill_pred(c, &ch);
    if (c->ltp && seq != 2u) fill_ltp(c, &ch);
    if (!c->er) put(w, id, 3);
    put(w, tag, 4); put_channel(w, c, &ch, 1);
}
/* one channel_pair_element with a common window; *second_bit = the bit offset of the second channel (for the ADTS error check) when not NULL */
static void emit_cpe(Bw *w, const Cfg *c, unsigned tag, unsigned seq, unsigned shape, unsigned max_sfb, const unsigned *group_len, unsigned groups, uint32_t *second_bit) {
    Chan ch[2]; unsigned ms_mask = 0, b, g; uint8_t used[8][64] = { { 0 } };
    make_channel(c, &ch[0], seq, shape, max_sfb, group_len, groups); make_channel(c, &ch[1], seq, shape, max_sfb, group_len, groups);
    if (c->main && seq != 2u) fill_pred(c, &ch[0]);
    if (c->ltp && seq != 2u) { fill_ltp(c, &ch[0]); fill_ltp(c, &ch[1]); }
    if (c->ms) { ms_mask = c->msall ? 2u : (unsigned)rr(0, 2); for (g = 0; g < groups; g++) for (b = 0; b < max_sfb; b++) used[g][b] = (uint8_t)(ms_mask == 2 ? 1 : (ms_mask == 1 ? (rnd() & 1u) : 0u)); }
    if (!c->er) put(w, 1, 3);
    put(w, tag, 4); put(w, 1, 1);                                                        /* ID_CPE, tag, common_window */
    put_ics_info(w, &ch[0]); if (c->ltp && seq != 2u && ch[0].pred) put_ltp(w, &ch[1]); put(w, ms_mask, 2);
    if (ms_mask == 1) for (g = 0; g < groups; g++) for (b = 0; b < max_sfb; b++) put(w, used[g][b], 1);
    put_channel(w, c, &ch[0], 0); if (second_bit) *second_bit = (uint32_t)w->n; put_channel(w, c, &ch[1], 0);
}
int main(int argc, char **argv) {
    Cfg c; unsigned frames = 200, seed = 1, f; char path[512]; FILE *fa, *fb, *fc; int a; Bw asc;
    memset(&c, 0, sizeof c); c.frame = 1024; c.rate = 3; c.channels = 2; c.amp = 12; c.peakmilli = 600;
    if (argc < 2) return 2;
    for (a = 2; a < argc; a++) {
        if (!strcmp(argv[a], "--frame") && a + 1 < argc) c.frame = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--channels") && a + 1 < argc) { c.channels = (unsigned)atoi(argv[++a]); c.config = c.channels; }
        else if (!strcmp(argv[a], "--config") && a + 1 < argc) { c.config = (unsigned)atoi(argv[++a]); }
        else if (!strcmp(argv[a], "--rate") && a + 1 < argc) c.rate = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--frames") && a + 1 < argc) frames = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--seed") && a + 1 < argc) seed = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--amp") && a + 1 < argc) c.amp = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--quiet") && a + 1 < argc) c.quiet = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--level") && a + 1 < argc) c.level = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--pns")) c.pns = 6;
        else if (!strcmp(argv[a], "--pns-den") && a + 1 < argc) c.pns = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--main")) c.main = 1;
        else if (!strcmp(argv[a], "--er")) c.er = 1;
        else if (!strcmp(argv[a], "--ltp")) { c.ltp = 1; c.ltpmax = 7; }
        else if (!strcmp(argv[a], "--peak") && a + 1 < argc) c.peakmilli = (unsigned)(atof(argv[++a]) * 1000.0);
        else if (!strcmp(argv[a], "--ltp-max-coef") && a + 1 < argc) { c.ltp = 1; c.ltpmax = (unsigned)atoi(argv[++a]); }
        else if (!strcmp(argv[a], "--pns-quiet")) c.pnsquiet = 1;
        else if (!strcmp(argv[a], "--full-sfb")) c.fullsfb = 1;
        else if (!strcmp(argv[a], "--pred-mode") && a + 1 < argc) c.predmode = (unsigned)atoi(argv[++a]);
        else if (!strcmp(argv[a], "--ms-all")) { c.ms = 1; c.msall = 1; }
        else if (!strcmp(argv[a], "--ms")) c.ms = 1;
        else if (!strcmp(argv[a], "--tns")) c.tns = 1;
        else if (!strcmp(argv[a], "--short")) c.shorts = 1;
        else if (!strcmp(argv[a], "--kbd")) c.kbd = 1;
        else { fprintf(stderr, "unknown option %s\n", argv[a]); return 2; }
    }
    if (!c.config) c.config = c.channels;                                                  /* --channels 1 / 2 or --config 1..7 (channel_configuration: 1 C, 2 L R, 3 C L R, 4 + S, 5 + Ls Rs, 6 + LFE, 7 = 7.1) */
    { static const unsigned count[8] = { 0, 1, 2, 3, 4, 5, 6, 8 }; if (c.config < 1 || c.config > 7) return 2; c.channels = count[c.config]; }
    if ((c.frame != 1024 && c.frame != 960) || c.rate > 12) return 2;
    if (c.frame == 960 && (c.config > 2 || c.ltp)) return 2;        /* 960 lines: mono / stereo; LTP has no 960 variant */
    g_rng = seed * 2654435761u + 12345u; if (!g_rng) g_rng = 1;
    { const unsigned lay = c.frame == 960 ? SAUTI_AAC_LAYOUT_960 : SAUTI_AAC_LAYOUT_1024;
      if (!sauti_aac_band_offsets(SAUTI_AAC_IDX(c.rate, lay), 0, &c.off, &c.bands) || !sauti_aac_band_offsets(SAUTI_AAC_IDX(c.rate, lay), 1, &c.soff, &c.sbands)) { fprintf(stderr, "no band table for this frame length / rate\n"); return 2; } }
    /* AudioSpecificConfig: AOT 2, rate, channel config, frameLengthFlag, dependsOnCoreCoder 0, extensionFlag 0 */
    memset(&asc, 0, sizeof asc); put(&asc, c.er ? (c.ltp ? 19u : 17u) : c.main ? 1u : c.ltp ? 4u : 2u, 5); put(&asc, c.rate, 4); put(&asc, c.config, 4); put(&asc, c.frame == 960 ? 1u : 0u, 1); put(&asc, 0, 1);
    if (c.er) { put(&asc, 1, 1); put(&asc, 0, 3); put(&asc, 0, 1); put(&asc, 0, 2); }       /* extensionFlag, the three resilience flags, extensionFlag3, epConfig 0 */
    else put(&asc, 0, 1);
    snprintf(path, sizeof path, "%s.asc", argv[1]); fa = fopen(path, "wb"); if (!fa) return 2; fwrite(asc.b, 1, (asc.n + 7) / 8, fa); fclose(fa);
    snprintf(path, sizeof path, "%s.aus", argv[1]); fb = fopen(path, "wb"); if (!fb) return 2;
    snprintf(path, sizeof path, "%s.crcinfo", argv[1]); fc = fopen(path, "wb"); if (!fc) return 2;
    for (f = 0; f < frames; f++) {
        static Bw w; uint32_t second_bit = 0xffffffffu; size_t len; unsigned seq, shape, max_sfb, groups, group_len[8], k;
        memset(&w, 0, sizeof w);
        pick_sequence(&c, &seq); if (getenv("SAUTI_GEN_DEBUG")) fprintf(stderr, "frame %u seq %u\n", f, seq); shape = c.kbd ? (rnd() & 1u) : 0u;
        if (seq == 2u) {
            unsigned win = 0; groups = 0;
            for (k = 0; k < 8; k++) { if (k == 0 || (rnd() % 3u) == 0) group_len[groups++] = 1; else group_len[groups - 1]++; win++; }
            max_sfb = (c.sbands > 4 && !c.fullsfb && (rnd() & 3u)) ? (unsigned)rr((int)c.sbands / 2, (int)c.sbands) : c.sbands; if (max_sfb > 15u) max_sfb = 15u; (void)win;
        } else { groups = 1; group_len[0] = 1; max_sfb = (c.bands > 8 && !c.fullsfb && (rnd() & 3u)) ? (unsigned)rr((int)c.bands / 2, (int)c.bands) : c.bands; }
        {
            static const char *const layout[8] = { "", "S", "C", "SC", "SCS", "SCC", "SCCL", "SCCCL" };      /* elements of channel_configuration 1..7: S = SCE, C = CPE, L = LFE */
            const char *el = layout[c.config]; unsigned sce_tag = 0, cpe_tag = 0, lfe_tag = 0, only_cpe_or_sce = c.config <= 2u;
            for (; *el; el++) {
                if (*el == 'S') emit_sce(&w, &c, 0, sce_tag++, seq, shape, max_sfb, group_len, groups);
                else if (*el == 'L') { unsigned lg[8] = { 1 }; emit_sce(&w, &c, 3, lfe_tag++, 0, 0, c.bands < 8u ? c.bands : 8u, lg, 1); }      /* LFE: ONLY_LONG, a few bands */
                else emit_cpe(&w, &c, cpe_tag++, seq, shape, max_sfb, group_len, groups, only_cpe_or_sce ? &second_bit : NULL);
            }
        }
        if (!c.er) put(&w, 7, 3);                                                        /* ID_END */
        len = (w.n + 7) / 8;
        if (c.ltp && c.channels <= 2) {                                                  /* closed loop: decode what was just written and watch the peak */
            static SautiAacLcnState st; static float pcm[2 * 1024]; static int started; SautiAacLcnConfig lc; SautiAacReason why; unsigned i;
            memset(&lc, 0, sizeof lc); lc.rate_index = c.rate; lc.channel_config = c.channels; lc.ltp = 1; lc.er = c.er;
            if (!started) { sauti_aac_lcn_state_init(&st); started = 1; }
            if (sauti_aac_lcn_decode(&lc, &st, w.b, len, pcm, &why) == 1) {
                float peak = 0; for (i = 0; i < 1024u * c.channels; i++) if (fabsf(pcm[i]) > peak) peak = fabsf(pcm[i]);
                if (g_ltp_off) g_ltp_off--;
                if ((unsigned)(peak * 1000.0f) > c.peakmilli) g_ltp_off = 2;
            }
        }
        { uint8_t hdr[4] = { (uint8_t)(len >> 24), (uint8_t)(len >> 16), (uint8_t)(len >> 8), (uint8_t)len }; fwrite(hdr, 1, 4, fb); fwrite(w.b, 1, len, fb); }
        { uint8_t ci[4] = { (uint8_t)(second_bit >> 24), (uint8_t)(second_bit >> 16), (uint8_t)(second_bit >> 8), (uint8_t)second_bit }; fwrite(ci, 1, 4, fc); }
    }
    fclose(fb); fclose(fc);
    fprintf(stderr, "wrote %u frames: frame %u, %u channel(s), rate index %u, %u scalefactor bands\n", frames, c.frame, c.channels, c.rate, c.bands);
    return 0;
}
