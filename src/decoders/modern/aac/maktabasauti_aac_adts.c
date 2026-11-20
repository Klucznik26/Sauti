/* MaktabaSauti, modul maktabasauti_aac: AAC w ADTS jako PRAWDZIWIE STRUMIENIOWY kodek (etap 2, 2026-11-20).
 *
 * Zastepuje w rejestrze domyslnym piec opakowan starych dekoderow "cala pamiec" (LC stereo/mono/wielokanalowy, HE-AAC mono/CPE).  Algorytm dekodowania jest TEN SAM:
 * wolane sa te same funkcje poziomu ramki (sauti_aac_lc_adts_*_decode_ex, sauti_aac_he_adts_*_decode_ex, mostek SBR/PS); zmienil sie tylko sposob dostarczania danych:
 *
 *   - wejscie czytane przez usluge hosta (peek/read/seek), po jednej ramce ADTS; NIGDY cale w pamieci.  Pamiec: stan dekodera + ramka (<= 8191 B) + PCM jednej ramki +
 *     ograniczona pula punktow kontrolnych (<= 8 MiB) -- niezalezna od dlugosci nagrania;
 *   - ramka jest zuzywana dopiero, gdy jest KOMPLETNA (peek, potem read): E_WOULD_BLOCK w dowolnym miejscu zostawia stan nietkniety, a wywolanie mozna powtorzyc;
 *   - wejscie nieprzewijalne: otwarcie, odczyt, flush; dlugosc nieznana (UNKNOWN_LENGTH), seek/reset = E_NOT_SEEKABLE;
 *   - wejscie przewijalne: przy otwarciu jedno sekwencyjne przejscie po NAGLOWKACH liczy ramki (dokladna dlugosc, jak w starym dekoderze), potem seek DOKLADNY: stan dekodera
 *     (historia IMDCT, PNS, QMF/SBR/PS) jest odtwarzany z najblizszego punktu kontrolnego i doliczany do celu; punkty kontrolne zapisuje odczyt, ich liczba jest ograniczona
 *     (stride rosnie dwukrotnie, gdy pula sie zapelni), wiec koszt seek rosnie z dlugoscia, pamiec nie;
 *   - uszkodzony strumien: zla ramka -> E_CORRUPT_DATA po wydaniu poprawnych ramek (lepki do flush/seek/reset; flush gubi historie dekodera i resynchronizuje na nastepnej
 *     poprawnej ramce), urwana ramka na koncu -> E_TRUNCATED, zmiana parametrow w srodku -> E_UNSUPPORTED_FEATURE;
 *   - brak stanu globalnego, cala pamiec przez host->alloc/free: wiele instancji w wielu watkach.
 *
 * Wybor wariantu jak w starym rejestrze (kolejnosc: LC stereo, LC mono, LC wielokanalowy, HE mono, HE CPE) wg PIERWSZEJ ramki: ramka musi sie w pelni zdekodowac danym
 * wariantem; strumien HE (SBR w FIL) odrzuca rdzen LC i trafia do HE.  PCM jest bit w bit rowny staremu API (tests/test_maktabasauti_regression.c).
 *
 * Opcje: "adts.scan" = "off" wylacza przejscie po naglowkach przy otwarciu (dlugosc nieznana, seek bez sprawdzania zakresu). */
#include "maktabasauti/maktabasauti_codec.h"
#include "sauti_aac_cpe.h"
#include "sauti_aac_headers.h"
#include "sauti_aac_he_adts_cpe.h"
#include "sauti_aac_he_adts_mono.h"
#include "sauti_aac_he_ps_bridge.h"
#include "sauti_aac_lc_multi.h"
#include "sauti_aac_sbr_channel.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

#define ADTS_MAX_FRAME 8191u
#define ADTS_HEADER_PEEK 9u
#define HE_FRAME_PCM 2048u
#define LC_FRAME_PCM 1024u
#define CACHE_FLOATS (8u * LC_FRAME_PCM)                 /* LC 8 kanalow x 1024 = 8192; HE stereo 2 x 2048 = 4096 tez sie miesci */
#define CHECKPOINT_BUDGET ((size_t)8 << 20)              /* gorna granica pamieci punktow kontrolnych */
#define CHECKPOINT_MAX 256u
#define RESYNC_LIMIT ((size_t)1 << 20)                   /* ile bajtow szukamy synchronizacji po uszkodzeniu */

typedef enum { V_LC_STEREO = 0, V_LC_MONO, V_LC_MULTI, V_HE_MONO, V_HE_CPE } variant_t;

typedef struct {
    SautiAacHeAdtsMonoState mono;
    SautiAacHeAdtsCpeState cpe;
    SautiAacSbrChannel channel[2];
    SautiAacHePsBridgeState ps;
    SautiAacSbrComplex pending[SAUTI_AAC_SBR_CHANNEL_GRID];           /* tryb PS: siatka ramki czekajacej na wyprzedzenie */
    SautiAacSbrPayloadFrame pending_payload;
    uint32_t pending_valid;
    uint32_t flushed;
} he_state_t;

/* caly stan dekodowania: kopia = punkt kontrolny */
typedef struct {
    union {
        SautiAacLcSynthesis lc[2];
        SautiAacLcMulti multi;
        he_state_t he;
    } u;
    uint64_t frames_in;                                  /* ramki ADTS zdekodowane */
    uint64_t pcm_out;                                    /* bloki PCM wyprodukowane (LC: = frames_in; HE-PS: o jeden mniej, patrz he_produce) */
    int64_t in_offset;                                   /* pozycja wejscia nastepnej, jeszcze nie pobranej ramki */
} core_t;

typedef struct { core_t core; } checkpoint_t;

typedef struct {
    const maktabasauti_codec_t *codec;
    const maktabasauti_host_t *host;
    maktabasauti_reader_t *rd;
    variant_t variant;
    unsigned config;                                     /* channel_configuration z ADTS */
    unsigned rate_index, core_rate, core_channels, crc_present;
    unsigned out_channels, out_rate, frame_pcm, ps_mode;
    SautiAacLcLayout layout;                             /* tylko config 0 (PCE) */
    int seekable, total_known;
    uint64_t known_frames;                               /* liczba ramek ADTS z przejscia po naglowkach (gdy total_known) */
    core_t core;
    core_t cp0;                                          /* stan poczatkowy (blok 0) */
    checkpoint_t **cps; size_t cp_count, cp_cap; uint64_t cp_stride;
    size_t cp_max;
    float cache[CACHE_FLOATS];
    size_t cache_pos, cache_len;                         /* ramki PCM w cache */
    uint64_t position;                                   /* ramki PCM wydane */
    int have_frame; size_t frame_len;
    int src_eof;
    unsigned resyncing;                                  /* po flush: ramki, ktorych nie da sie zdekodowac (np. HE bez naglowka SBR po utracie stanu), sa pomijane do pierwszej udanej */
    maktabasauti_result_t pend;                          /* odlozony blad (po wydaniu ramek z cache) */
    char err[96];
    /* bufory robocze HE (nie sa czescia punktu kontrolnego) */
    SautiAacSbrComplex scratch_qmf38[38u * 64u];
    SautiAacSbrComplex scratch_grid[SAUTI_AAC_SBR_CHANNEL_GRID];
    SautiAacSbrComplex scratch_g2[2][SAUTI_AAC_SBR_CHANNEL_GRID];
    uint8_t frame[ADTS_MAX_FRAME + 16u];
} dec_t;

/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
/* naglowek ADTS (C): te same warunki co sauti_aac_adts_parse_asm, ale na samych 7-9 bajtach, bez wymagania calej ramki (do sondy po prefiksie i do wyznaczania dlugosci) */
typedef struct { unsigned rate_index, rate, config, frame_bytes, header_bytes, raw_blocks, crc_present; } adts_hdr_t;

static const unsigned adts_rates[13] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };

/* 1 = naglowek poprawny (profil LC, indeks czestotliwosci 0..12, konfiguracja kanalow 0..7, dlugosc >= naglowek), 0 = nie */
static int adts_parse_header(const uint8_t *p, size_t n, adts_hdr_t *h) {
    unsigned prot_absent, sfi, cfg, fl;
    if (n < 7u || p[0] != 0xFFu || (p[1] & 0xF6u) != 0xF0u) return 0;
    prot_absent = p[1] & 1u;
    if (!prot_absent && n < 9u) return 0;
    if ((p[2] >> 6) != 1u) return 0;                                    /* profil 1 = AAC-LC */
    sfi = (unsigned)(p[2] >> 2) & 15u;
    if (sfi > 12u) return 0;
    cfg = (unsigned)(((p[2] & 1u) << 2) | (p[3] >> 6));
    fl = (unsigned)(((p[3] & 3u) << 11) | (p[4] << 3) | (p[5] >> 5));
    if (fl < (prot_absent ? 7u : 9u)) return 0;
    h->rate_index = sfi; h->rate = adts_rates[sfi]; h->config = cfg; h->frame_bytes = fl;
    h->header_bytes = prot_absent ? 7u : 9u; h->raw_blocks = (unsigned)(p[6] & 3u) + 1u; h->crc_present = prot_absent ? 0u : 1u;
    return 1;
}

static int same_stream(const dec_t *d, const adts_hdr_t *h) {
    return h->rate_index == d->rate_index && h->config == d->config && h->raw_blocks == 1u && h->crc_present == d->crc_present;
}

/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
/* pobieranie ramki: peek (kompletna ramka albo nic), potem read.  Zwraca OK (d->frame, d->frame_len), EOF (czysty koniec na granicy ramki), E_TRUNCATED (urwana ramka),
 * E_CORRUPT_DATA (nie ma poprawnego naglowka w tym miejscu), albo blad wejscia (E_WOULD_BLOCK, E_IO...) -- w tych przypadkach nic nie zostalo zuzyte. */
static maktabasauti_result_t fetch_frame(dec_t *d) {
    const maktabasauti_host_t *h = d->host;
    int64_t n, m; adts_hdr_t hd; SautiAacAdts full;
    if (d->have_frame) return MAKTABASAUTI_OK;
    n = h->peek(h->user, d->rd, d->frame, ADTS_HEADER_PEEK);
    if (n < 0) return (maktabasauti_result_t)n;
    if (n == 0) return MAKTABASAUTI_EOF;
    if (!adts_parse_header(d->frame, (size_t)n, &hd)) {
        /* urwany naglowek na koncu wejscia (bajty sa prefiksem poprawnej synchronizacji) albo smieci */
        const int prefix = d->frame[0] == 0xFFu && (n < 2 || (d->frame[1] & 0xF6u) == 0xF0u);
        return (prefix && n < (int64_t)ADTS_HEADER_PEEK) ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_E_CORRUPT_DATA;
    }
    m = h->peek(h->user, d->rd, d->frame, hd.frame_bytes);
    if (m < 0) return (maktabasauti_result_t)m;
    if ((uint64_t)m < hd.frame_bytes) return MAKTABASAUTI_E_TRUNCATED;
    if (!sauti_aac_adts_parse_asm(d->frame, hd.frame_bytes, &full) || full.frame_bytes != hd.frame_bytes) return MAKTABASAUTI_E_CORRUPT_DATA;
    m = h->read(h->user, d->rd, d->frame, hd.frame_bytes);
    if (m < 0) return (maktabasauti_result_t)m;
    if ((uint64_t)m != hd.frame_bytes) return MAKTABASAUTI_E_IO;        /* peek obiecal te bajty */
    d->frame_len = hd.frame_bytes;
    d->have_frame = 1;
    return MAKTABASAUTI_OK;
}

/* po uszkodzeniu: pomija bajty do najblizszego miejsca wygladajacego na poprawna ramke (naglowek + synchronizacja nastepnej ramki, gdy widac ja w oknie), zaczynajac od bajtu PO
 * obecnej pozycji.  OK = wejscie stoi na kandydacie; EOF = nic nie znaleziono do konca wejscia albo limitu; blad wejscia (np. WOULD_BLOCK) = bez zmiany tego, co juz pominieto. */
static maktabasauti_result_t resync(dec_t *d) {
    const maktabasauti_host_t *h = d->host;
    size_t skipped = 0; int first = 1;
    while (skipped < RESYNC_LIMIT) {
        uint8_t win[4096]; int64_t n; size_t i, adv; adts_hdr_t hd;
        n = h->peek(h->user, d->rd, win, sizeof win);
        if (n < 0) return (maktabasauti_result_t)n;
        if (n < 7) { if (n > 0) (void)h->read(h->user, d->rd, win, (size_t)n); return MAKTABASAUTI_EOF; }
        for (i = first ? 1u : 0u; i + 7u <= (size_t)n; i++) {
            adts_hdr_t nx;
            if (win[i] != 0xFFu || (win[i + 1] & 0xF6u) != 0xF0u || !adts_parse_header(win + i, (size_t)n - i, &hd)) continue;
            if (i + (size_t)hd.frame_bytes + 7u <= (size_t)n && !adts_parse_header(win + i + hd.frame_bytes, (size_t)n - i - hd.frame_bytes, &nx)) continue;   /* widac nastepna ramke: musi byc poprawna */
            if (i) { int64_t rd = h->read(h->user, d->rd, win, i); if (rd < 0) return (maktabasauti_result_t)rd; }
            return MAKTABASAUTI_OK;
        }
        adv = (size_t)n > 6u ? (size_t)n - 6u : (size_t)n;                 /* zostawiamy 6 bajtow: kandydat moze zaczynac sie tuz przed koncem okna */
        { int64_t rd = h->read(h->user, d->rd, win, adv); if (rd < 0) return (maktabasauti_result_t)rd; skipped += (size_t)rd; }
        first = 0;
    }
    return MAKTABASAUTI_EOF;
}

/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
static void core_reset(dec_t *d) {
    memset(&d->core, 0, sizeof d->core);
    switch (d->variant) {
    case V_LC_STEREO: (void)sauti_aac_lc_synthesis_init(&d->core.u.lc[0], 0); (void)sauti_aac_lc_synthesis_init(&d->core.u.lc[1], 0); break;
    case V_LC_MONO: (void)sauti_aac_lc_synthesis_init(&d->core.u.lc[0], 0); break;
    case V_LC_MULTI:
        if (d->config == 0u) (void)sauti_aac_lc_multi_init_layout(&d->core.u.multi, &d->layout, 0);
        else (void)sauti_aac_lc_multi_init(&d->core.u.multi, d->config, 0);
        break;
    case V_HE_MONO: case V_HE_CPE: {
        he_state_t *s = &d->core.u.he;
        memset(s, 0, sizeof *s);
        (void)sauti_aac_he_adts_mono_init(&s->mono); (void)sauti_aac_he_adts_cpe_init(&s->cpe);
        (void)sauti_aac_sbr_channel_init(&s->channel[0]); (void)sauti_aac_sbr_channel_init(&s->channel[1]);
        break; }
    }
}

static int all_finite(const float *p, size_t n) { size_t i; for (i = 0; i < n; i++) if (!isfinite(p[i])) return 0; return 1; }

/* ---- produkcja jednego bloku PCM do d->cache (z ramki d->frame) -------------------------------------------------------------------------------------------------- */
typedef enum { P_OK = 0, P_NEED_MORE, P_FAIL, P_UNSUPPORTED } prod_t;

static prod_t decode_lc(dec_t *d) {
    SautiAacReason reason = SAUTI_AAC_REASON_OK; int r;
    switch (d->variant) {
    case V_LC_STEREO: r = sauti_aac_lc_adts_stereo_decode_ex(d->frame, d->frame_len, d->core.u.lc, d->cache, &reason); if (r == 1 && !all_finite(d->cache, 2048u)) r = 0; break;
    case V_LC_MONO: r = sauti_aac_lc_adts_mono_decode_ex(d->frame, d->frame_len, &d->core.u.lc[0], d->cache, &reason); if (r == 1 && !all_finite(d->cache, 1024u)) r = 0; break;
    default:
        r = sauti_aac_lc_adts_multi_decode_ex(d->frame, d->frame_len, &d->core.u.multi, d->cache, &reason);
        if (r == 1 && !all_finite(d->cache, (size_t)1024u * d->out_channels)) r = 0;
        break;
    }
    return r == 1 ? P_OK : (r < 0 ? P_UNSUPPORTED : P_FAIL);
}

/* HE: render PS dla czekajacej ramki; `lookahead` = siatka nastepnej ramki albo NULL na koncu strumienia (cisza) */
static int he_render_ps(dec_t *d, const SautiAacSbrComplex *lookahead, float *pcm_stereo) {
    he_state_t *s = &d->core.u.he;
    SautiAacSbrComplex *qmf38 = d->scratch_qmf38;
    memcpy(qmf38, s->pending, sizeof(s->pending));
    if (lookahead) memcpy(qmf38 + 32u * 64u, lookahead, 6u * 64u * sizeof(qmf38[0]));
    else memset(qmf38 + 32u * 64u, 0, 6u * 64u * sizeof(qmf38[0]));
    return sauti_aac_he_ps_bridge_render(&s->pending_payload, qmf38, 38u * 64u, &s->ps, pcm_stereo, 4096u);
}

/* ramka bez danych PS: te same parametry co poprzednia ramka, plasko na cala ramke (jak w starym adapterze) */
static void he_ps_hold(const SautiAacPsFrame *prev, SautiAacPsFrame *out) {
    const unsigned last = prev->resolved_num_env ? prev->resolved_num_env - 1u : 0u;
    memset(out, 0, sizeof(*out));
    out->header = prev->header;
    out->resolved_num_env = 1u; out->border_position[0] = 31u;
    out->iid_count = prev->iid_count; out->icc_count = prev->icc_count;
    memcpy(out->iid[0], prev->iid[last], sizeof(out->iid[0]));
    memcpy(out->icc[0], prev->icc[last], sizeof(out->icc[0]));
}

/* dekoduje d->frame stanem HE; P_OK = blok w cache, P_NEED_MORE = ramka zuzyta, blok bedzie po nastepnej (PS), P_FAIL / P_UNSUPPORTED */
static prod_t decode_he(dec_t *d) {
    he_state_t *s = &d->core.u.he;
    SautiAacReason reason = SAUTI_AAC_REASON_OK;
    if (d->variant == V_HE_MONO) {
        float core[1024]; SautiAacSbrPayloadFrame payload; int r;
        SautiAacSbrComplex *grid = d->scratch_grid;
        r = sauti_aac_he_adts_mono_temporal_decode_ex(d->frame, d->frame_len, &s->mono, core, &payload, &reason);
        if (r != 1) return r < 0 ? P_UNSUPPORTED : P_FAIL;
        if (d->ps_mode) {
            if (!payload.has_ps) {
                if (!s->pending_payload.has_ps) return P_FAIL;           /* nie ma czego powtorzyc */
                he_ps_hold(&s->pending_payload.ps, &payload.ps); payload.has_ps = 1u;
            }
        }
        if (!sauti_aac_sbr_channel_frame(&s->channel[0], core, &payload, grid)) return P_FAIL;
        if (!d->ps_mode) {
            if (!sauti_aac_sbr_channel_synthesize(&s->channel[0], grid, d->cache)) return P_FAIL;
            return all_finite(d->cache, HE_FRAME_PCM) ? P_OK : P_FAIL;
        }
        if (s->pending_valid) {
            float st[4096];
            if (!he_render_ps(d, grid, st)) return P_FAIL;
            memcpy(d->cache, st, sizeof(st));
            memcpy(s->pending, grid, sizeof(s->pending));
            s->pending_payload = payload;
            return all_finite(d->cache, (size_t)HE_FRAME_PCM * 2u) ? P_OK : P_FAIL;
        }
        memcpy(s->pending, grid, sizeof(s->pending));
        s->pending_payload = payload; s->pending_valid = 1u;
        return P_NEED_MORE;
    } else {
        float core2[2u * 1024u], c[2][1024], pcm2[2][HE_FRAME_PCM]; unsigned i, k; int r;
        SautiAacSbrPayloadCpeFrame pl;
        SautiAacSbrComplex (*g2)[SAUTI_AAC_SBR_CHANNEL_GRID] = d->scratch_g2;
        r = sauti_aac_he_adts_cpe_decode_ex(d->frame, d->frame_len, &s->cpe, core2, &pl, &reason);
        if (r != 1) return r < 0 ? P_UNSUPPORTED : P_FAIL;
        for (i = 0; i < 1024u; i++) { c[0][i] = core2[2u * i]; c[1][i] = core2[2u * i + 1u]; }
        for (k = 0; k < 2u; k++)
            if (!sauti_aac_sbr_channel_frame(&s->channel[k], c[k], &pl.channel[k], g2[k]) || !sauti_aac_sbr_channel_synthesize(&s->channel[k], g2[k], pcm2[k])) return P_FAIL;
        for (i = 0; i < HE_FRAME_PCM; i++) { d->cache[2u * i] = pcm2[0][i]; d->cache[2u * i + 1u] = pcm2[1][i]; }
        return all_finite(d->cache, (size_t)HE_FRAME_PCM * 2u) ? P_OK : P_FAIL;
    }
}

/* koniec znanej liczby ramek: nie czytamy dalej (bajty za ostatnia poprawna ramka, np. znacznik, sa pomijane) */
static int at_known_end(const dec_t *d) { return d->total_known && d->core.frames_in >= d->known_frames; }

static void checkpoint_maybe(dec_t *d) {
    checkpoint_t *cp; size_t i;
    if (!d->seekable || d->core.pcm_out == 0 || d->core.pcm_out % d->cp_stride != 0) return;
    if (d->cp_count && d->cps[d->cp_count - 1]->core.pcm_out >= d->core.pcm_out) return;     /* ten blok juz zapisany (powtorzony odczyt po seek) */
    if (d->cp_count == d->cp_cap) {                                       /* pula pelna: zostaw co drugi punkt, stride x2 */
        size_t keep = 0;
        for (i = 0; i < d->cp_count; i++) {
            if (d->cps[i]->core.pcm_out % (d->cp_stride * 2u) == 0) d->cps[keep++] = d->cps[i];
            else d->host->free(d->host->user, d->cps[i]);
        }
        d->cp_count = keep; d->cp_stride *= 2u;
        if (d->core.pcm_out % d->cp_stride != 0) return;
    }
    cp = (checkpoint_t *)d->host->alloc(d->host->user, sizeof *cp);
    if (!cp) return;                                                      /* brak pamieci: bez punktu kontrolnego, odczyt dziala dalej */
    cp->core = d->core;
    d->cps[d->cp_count++] = cp;
}

/* produkuje nastepny blok PCM do cache.  OK (cache_len>0), EOF, albo blad (WOULD_BLOCK: stan nietkniety) */
static maktabasauti_result_t produce(dec_t *d) {
    d->cache_pos = d->cache_len = 0;
    for (;;) {
        maktabasauti_result_t fr; prod_t p;
        if ((d->variant == V_HE_MONO) && d->ps_mode && d->core.u.he.pending_valid && (d->src_eof || at_known_end(d))) {
            he_state_t *s = &d->core.u.he;
            if (s->flushed) return MAKTABASAUTI_EOF;
            if (!he_render_ps(d, NULL, d->cache)) return MAKTABASAUTI_E_CORRUPT_DATA;
            s->pending_valid = 0; s->flushed = 1;
            if (!all_finite(d->cache, (size_t)HE_FRAME_PCM * 2u)) return MAKTABASAUTI_E_CORRUPT_DATA;
            goto produced;
        }
        if (d->src_eof || at_known_end(d)) return MAKTABASAUTI_EOF;
        fr = fetch_frame(d);
        if (fr == MAKTABASAUTI_EOF) { d->src_eof = 1; continue; }
        if (fr == MAKTABASAUTI_E_CORRUPT_DATA) {                           /* zla ramka: ustaw wejscie na nastepnej poprawnej, zglos blad (historia dekodera jest juz nieciagla: flush) */
            maktabasauti_result_t rr = resync(d);
            if (rr == MAKTABASAUTI_E_WOULD_BLOCK || rr == MAKTABASAUTI_E_IO) return rr;
            if (rr == MAKTABASAUTI_EOF) d->src_eof = 1;
            return MAKTABASAUTI_E_CORRUPT_DATA;
        }
        if (fr == MAKTABASAUTI_E_TRUNCATED) { d->src_eof = 1; return fr; }   /* urwana ramka na koncu: nic wiecej nie przyjdzie */
        if (fr != MAKTABASAUTI_OK) return fr;
        {   adts_hdr_t hd;
            if (adts_parse_header(d->frame, d->frame_len, &hd) && !same_stream(d, &hd)) { d->have_frame = 0; return MAKTABASAUTI_E_UNSUPPORTED_FEATURE; } }   /* zmiana parametrow w srodku */
        p = (d->variant <= V_LC_MULTI) ? decode_lc(d) : decode_he(d);
        d->have_frame = 0;                                                 /* ramka zuzyta (takze, gdy dekodowanie sie nie powiodlo: pomijamy ja) */
        d->core.in_offset = d->host->tell(d->host->user, d->rd);
        if ((p == P_UNSUPPORTED || p == P_FAIL) && d->resyncing && d->resyncing < 2000u) { d->resyncing++; continue; }   /* po flush: czekamy na ramke, od ktorej dekodowanie sie wznawia */
        if (p == P_UNSUPPORTED) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
        if (p == P_FAIL) return MAKTABASAUTI_E_CORRUPT_DATA;
        d->resyncing = 0;
        d->core.frames_in++;
        if (p == P_NEED_MORE) continue;
        goto produced;
    }
produced:
    d->core.pcm_out++;
    d->cache_len = d->frame_pcm;
    checkpoint_maybe(d);
    return MAKTABASAUTI_OK;
}

/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
static void drop_checkpoints(dec_t *d) {
    size_t i;
    for (i = 0; i < d->cp_count; i++) d->host->free(d->host->user, d->cps[i]);
    d->cp_count = 0; d->cp_stride = (d->variant >= V_HE_MONO) ? 256u : 64u;
}

static maktabasauti_result_t MAKTABASAUTI_CALL adts_read(void *state, void *dst, size_t n, size_t *frames) {
    dec_t *d = (dec_t *)state; float *out = (float *)dst; size_t done = 0; const size_t ch = d->out_channels;
    *frames = 0;
    if (d->pend != MAKTABASAUTI_OK) { maktabasauti_result_t r = d->pend; return r; }     /* odlozony blad: lepki do flush/seek/reset (rdzen tez go pamieta) */
    while (done < n) {
        size_t take;
        if (d->cache_pos == d->cache_len) {
            maktabasauti_result_t r = produce(d);
            if (r != MAKTABASAUTI_OK) {
                if (r == MAKTABASAUTI_EOF && at_known_end(d) && d->core.pcm_out == 0) { /* pusty strumien: nic do wydania */ }
                *frames = done;
                if (r == MAKTABASAUTI_EOF) return done ? MAKTABASAUTI_OK : MAKTABASAUTI_EOF;
                if (r == MAKTABASAUTI_E_WOULD_BLOCK) return done ? MAKTABASAUTI_OK : r;                  /* nielepki: stan nietkniety, mozna powtorzyc */
                if (done) { d->pend = r; return MAKTABASAUTI_OK; }                                    /* najpierw oddajemy to, co sie udalo */
                d->pend = r;
                return r;
            }
        }
        take = n - done; if (take > d->cache_len - d->cache_pos) take = d->cache_len - d->cache_pos;
        memcpy(out + done * ch, d->cache + d->cache_pos * ch, take * ch * sizeof(float));
        d->cache_pos += take; done += take; d->position += take;
    }
    *frames = done;
    return MAKTABASAUTI_OK;
}

static void restore_core(dec_t *d, const core_t *c) {
    d->core = *c;
    d->cache_pos = d->cache_len = 0; d->have_frame = 0; d->src_eof = 0; d->pend = MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL adts_seek(void *state, uint64_t frame, uint32_t flags, uint64_t *actual) {
    dec_t *d = (dec_t *)state; uint64_t tb, within; const core_t *best = &d->cp0; size_t i; int continue_current; (void)flags;
    if (!d->seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;
    if (d->total_known && frame > d->known_frames * d->frame_pcm) return MAKTABASAUTI_E_OUT_OF_RANGE;
    tb = frame / d->frame_pcm; within = frame % d->frame_pcm;
    if (d->cache_len && d->core.pcm_out && tb == d->core.pcm_out - 1u && d->pend == MAKTABASAUTI_OK) {      /* blok, ktory juz jest w cache */
        d->cache_pos = (size_t)within; d->position = frame; *actual = frame; return MAKTABASAUTI_OK;
    }
    for (i = 0; i < d->cp_count; i++) if (d->cps[i]->core.pcm_out <= tb && d->cps[i]->core.pcm_out >= best->pcm_out) best = &d->cps[i]->core;
    /* kontynuacja od biezacego stanu, gdy cel jest za nim i zaden punkt kontrolny nie jest blizej */
    continue_current = d->pend == MAKTABASAUTI_OK && tb >= d->core.pcm_out && best->pcm_out <= d->core.pcm_out;
    if (!continue_current) {
        int64_t got = d->host->seek(d->host->user, d->rd, best->in_offset, 0);
        if (got < 0) return (maktabasauti_result_t)got;                                                   /* stan bez zmian */
        restore_core(d, best);
    }
    d->cache_pos = d->cache_len = 0;
    while (d->core.pcm_out < tb) {                                                                       /* bloki do (bez) celu: tylko dekodowanie, PCM jest wyrzucany */
        maktabasauti_result_t r = produce(d);
        if (r == MAKTABASAUTI_EOF) { d->cache_pos = d->cache_len = 0; goto landed_at_end; }
        if (r != MAKTABASAUTI_OK) return r;
    }
    d->cache_pos = d->cache_len = 0;
    if (!(d->total_known && frame == d->known_frames * d->frame_pcm)) {                                  /* blok zawierajacy cel */
        maktabasauti_result_t r = produce(d);
        if (r == MAKTABASAUTI_OK) d->cache_pos = (size_t)within;
        else if (r == MAKTABASAUTI_EOF) goto landed_at_end;
        else return r;
    } else { d->cache_len = d->cache_pos = 0; }
    d->position = frame; *actual = frame;
    return MAKTABASAUTI_OK;
landed_at_end:                                                                                           /* za koncem strumienia (dlugosc nieznana): ladujemy na koncu */
    d->cache_pos = d->cache_len = 0;
    d->position = d->core.pcm_out * d->frame_pcm; *actual = d->position;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL adts_reset(void *state) {
    dec_t *d = (dec_t *)state;
    restore_core(d, &d->cp0);
    d->position = 0;                                                      /* rdzen przewinal wejscie na poczatek przed wywolaniem */
    return MAKTABASAUTI_OK;
}

/* flush: porzuca historie dekodera i kontynuuje od biezacej pozycji wejscia (po uszkodzeniu produce() juz ustawilo je na nastepnej poprawnej ramce).  Po flush licznik ramek i punkty
 * kontrolne nie obowiazuja, wiec przewijanie jest wylaczone (tryb strumienia na zywo). */
static maktabasauti_result_t MAKTABASAUTI_CALL adts_flush(void *state) {
    dec_t *d = (dec_t *)state;
    core_reset(d);
    d->core.in_offset = d->host->tell(d->host->user, d->rd);
    d->cache_pos = d->cache_len = 0; d->have_frame = 0; d->pend = MAKTABASAUTI_OK;
    drop_checkpoints(d); d->total_known = 0; d->seekable = 0; d->resyncing = 1;
    return MAKTABASAUTI_OK;
}

static const char *MAKTABASAUTI_CALL adts_last_error(const void *state) { const dec_t *d = (const dec_t *)state; return d->err[0] ? d->err : NULL; }

static void MAKTABASAUTI_CALL adts_close(void *state) {
    dec_t *d = (dec_t *)state;
    const maktabasauti_host_t *h;
    if (!d) return;
    h = d->host;
    drop_checkpoints(d);
    if (d->cps) h->free(h->user, d->cps);
    h->free(h->user, d);
}

/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
static int32_t MAKTABASAUTI_CALL adts_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    adts_hdr_t h, nx; (void)self; (void)hint;
    if (!p || !adts_parse_header(p, n, &h) || h.raw_blocks != 1u) return 0;
    if (n >= (size_t)h.frame_bytes + 7u) {                                /* druga ramka widoczna w prefiksie: lancuch musi sie zgadzac (strumien samosynchronizujacy) */
        if (!adts_parse_header(p + h.frame_bytes, n - h.frame_bytes, &nx) || nx.rate_index != h.rate_index || nx.config != h.config) return 0;
        return 90;
    }
    if (n == h.frame_bytes) return 85;                                    /* plik z jedna ramka */
    if (n < h.frame_bytes) return 70;                                     /* ramka dluzsza niz prefiks (nie da sie sprawdzic lancucha) */
    /* kilka bajtow za ramka (< 7): musza byc poczatkiem synchronizacji nastepnej ramki (strumien urwany w naglowku), inaczej smieci */
    return (p[h.frame_bytes] == 0xFFu && (n - h.frame_bytes < 2u || (p[h.frame_bytes + 1u] & 0xF6u) == 0xF0u)) ? 75 : 0;
}

static uint64_t default_mask(unsigned channels) {
    switch (channels) { case 1: return MAKTABASAUTI_LAYOUT_MONO; case 2: return MAKTABASAUTI_LAYOUT_STEREO; case 6: return MAKTABASAUTI_LAYOUT_5_1; case 8: return MAKTABASAUTI_LAYOUT_7_1; default: return 0; }
}

/* sekwencyjne przejscie po naglowkach: liczy ramki ADTS od biezacej pozycji (po pierwszej ramce) do konca albo pierwszej niepoprawnej; wraca na `resume` */
static maktabasauti_result_t scan_frames(dec_t *d, int64_t resume, uint64_t *count, int *consistent) {
    const maktabasauti_host_t *h = d->host; uint64_t c = 1;               /* pierwsza ramka juz policzona */
    *consistent = 1;
    for (;;) {
        uint8_t hdr[ADTS_HEADER_PEEK]; int64_t n, m; adts_hdr_t hd;
        n = h->peek(h->user, d->rd, hdr, ADTS_HEADER_PEEK);
        if (n < 0) return (maktabasauti_result_t)n;
        if (n == 0) break;
        if (!adts_parse_header(hdr, (size_t)n, &hd)) break;               /* smieci albo urwany naglowek na koncu: koniec zliczania */
        if (!same_stream(d, &hd)) { *consistent = 0; break; }
        {   uint8_t skip[1024]; size_t left = hd.frame_bytes;           /* przesuwamy sie po ramce kawalkami (NIE do d->frame: tam czeka pierwsza ramka) */
            while (left) {
                size_t want = left < sizeof skip ? left : sizeof skip;
                m = h->read(h->user, d->rd, skip, want);
                if (m < 0) return (maktabasauti_result_t)m;
                if (m == 0) break;
                left -= (size_t)m;
            }
            if (left) break;                                              /* ramka urwana: nie liczymy */
        }
        c++;
    }
    if (h->seek(h->user, d->rd, resume, 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;
    *count = c;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL adts_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *host, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    dec_t *d = NULL; maktabasauti_result_t r; adts_hdr_t hd; uint32_t flags; const char *scan_opt; int64_t after_first;
    prod_t pr = P_FAIL; variant_t chosen = V_LC_STEREO;
    *state = NULL;
    d = (dec_t *)host->alloc(host->user, sizeof *d);
    if (!d) return MAKTABASAUTI_E_NO_MEMORY;
    memset(d, 0, sizeof *d);
    d->codec = self; d->host = host; d->rd = rd;
    r = fetch_frame(d);
    if (r == MAKTABASAUTI_E_CORRUPT_DATA || r == MAKTABASAUTI_E_TRUNCATED || r == MAKTABASAUTI_EOF) r = MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if (r != MAKTABASAUTI_OK) goto fail;
    (void)adts_parse_header(d->frame, d->frame_len, &hd);
    if (hd.raw_blocks != 1u) { r = MAKTABASAUTI_E_UNSUPPORTED_FEATURE; goto fail; }   /* wiele raw_data_block w ramce: niewspierane (jak wczesniej) */
    d->rate_index = hd.rate_index; d->core_rate = hd.rate; d->config = hd.config; d->crc_present = hd.crc_present;
    after_first = host->tell(host->user, rd);
    flags = host->input_flags ? host->input_flags(host->user, rd) : 0u;
    d->seekable = (flags & MAKTABASAUTI_INPUT_SEEKABLE) != 0;

    /* wariant wg PIERWSZEJ ramki, w kolejnosci starego rejestru; probne dekodowanie na swiezym stanie, potem stan wraca do poczatkowego */
    {   static const variant_t order_stereo[] = { V_LC_STEREO, V_HE_CPE }, order_mono[] = { V_LC_MONO, V_HE_MONO }, order_multi[] = { V_LC_MULTI };
        const variant_t *order = hd.config == 2u ? order_stereo : hd.config == 1u ? order_mono : order_multi;
        size_t norder = hd.config == 2u ? 2u : hd.config == 1u ? 2u : 1u, k;
        if (hd.config == 0u || hd.config >= 3u) {
            if (hd.crc_present) { r = MAKTABASAUTI_E_UNSUPPORTED_FEATURE; goto fail; }             /* wielokanalowy LC bez CRC (jak wczesniej) */
            if (hd.config == 0u) {
                int sc = sauti_aac_lc_multi_scan_pce(d->frame, d->frame_len, &d->layout);
                if (sc != 1) { r = MAKTABASAUTI_E_UNSUPPORTED_FEATURE; goto fail; }
            } else if (!sauti_aac_lc_multi_config_channels(hd.config)) { r = MAKTABASAUTI_E_UNSUPPORTED_FEATURE; goto fail; }
        }
        for (k = 0; k < norder; k++) {
            d->variant = order[k];
            d->core_channels = hd.config == 0u ? d->layout.channels : (order[k] == V_LC_MULTI ? sauti_aac_lc_multi_config_channels(hd.config) : (hd.config == 1u ? 1u : 2u));
            d->ps_mode = 0;
            core_reset(d);
            d->out_channels = d->core_channels;
            d->frame_pcm = order[k] >= V_HE_MONO ? HE_FRAME_PCM : LC_FRAME_PCM;
            d->out_rate = order[k] >= V_HE_MONO ? d->core_rate * 2u : d->core_rate;
            if (order[k] == V_HE_MONO) {                                   /* PS wykrywane z pierwszej ramki (jak w starym adapterze) */
                float core1[1024]; SautiAacSbrPayloadFrame payload1; SautiAacReason why = SAUTI_AAC_REASON_OK;
                if (sauti_aac_he_adts_mono_temporal_decode_ex(d->frame, d->frame_len, &d->core.u.he.mono, core1, &payload1, &why) != 1) { pr = P_FAIL; continue; }
                if (payload1.has_ps) { d->ps_mode = 1; d->out_channels = 2u; }
                core_reset(d);
            }
            pr = order[k] <= V_LC_MULTI ? decode_lc(d) : decode_he(d);
            if (pr == P_OK || pr == P_NEED_MORE) { chosen = order[k]; break; }
        }
        if (!(pr == P_OK || pr == P_NEED_MORE)) { r = MAKTABASAUTI_E_UNSUPPORTED_FEATURE; goto fail; }   /* pierwsza ramka nie dekoduje sie zadnym wariantem */
    }
    d->variant = chosen;
    core_reset(d);
    d->core.in_offset = 0; d->cp0 = d->core;                                                          /* blok 0: ramka 0 jest jeszcze w d->frame (have_frame) */
    d->cp_stride = d->variant >= V_HE_MONO ? 256u : 64u;
    d->cp_max = CHECKPOINT_BUDGET / sizeof(checkpoint_t); if (d->cp_max > CHECKPOINT_MAX) d->cp_max = CHECKPOINT_MAX; if (d->cp_max < 4u) d->cp_max = 4u;
    d->cp_cap = d->cp_max;
    d->cps = (checkpoint_t **)host->alloc(host->user, d->cp_cap * sizeof *d->cps);
    if (!d->cps) { r = MAKTABASAUTI_E_NO_MEMORY; goto fail; }

    /* dlugosc: przejscie po naglowkach (tylko gdy wejscie sie przewija) */
    scan_opt = host->option ? host->option(host->user, "adts.scan") : NULL;
    d->total_known = 0;
    if (d->seekable && !(scan_opt && (!strcmp(scan_opt, "off") || !strcmp(scan_opt, "0")))) {
        uint64_t count = 0; int consistent = 0;
        r = scan_frames(d, after_first, &count, &consistent);
        if (r != MAKTABASAUTI_OK) goto fail;
        if (consistent) { d->total_known = 1; d->known_frames = count; }
    }
    d->have_frame = 1;                                                                                 /* pierwsza ramka czeka w buforze: pierwszy produce() ja zdekoduje */
    d->core.in_offset = after_first;
    /* ramka 0 jest w buforze, a punkt kontrolny 0 ma wejscie na jej poczatku (0): przywrocenie punktu 0 pobierze ja ponownie */

    memset(info, 0, sizeof *info);
    info->struct_size = sizeof *info;
    info->sample_rate = d->out_rate; info->channels = d->out_channels; info->channel_mask = default_mask(d->out_channels);
    info->native_format = MAKTABASAUTI_FORMAT_F32;
    info->total_frames = d->total_known ? d->known_frames * d->frame_pcm : MAKTABASAUTI_UNKNOWN_LENGTH;
    info->flags = d->seekable ? MAKTABASAUTI_STREAM_SEEKABLE : 0u;
    strcpy(info->codec_name, "aac"); strcpy(info->container_name, "adts");
    strcpy(info->codec_profile, d->variant >= V_HE_MONO ? "HE-AAC" : "LC");
    *state = d;
    return MAKTABASAUTI_OK;
fail:
    if (d) { if (d->cps) host->free(host->user, d->cps); host->free(host->user, d); }
    return r;
}

const maktabasauti_codec_t maktabasauti_aac_adts_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "aac-adts", "AAC in ADTS (LC mono/stereo/multichannel, HE-AAC v1/v2), streaming", "maktabasauti_aac",
    MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0u, NULL,
    adts_probe, adts_open, adts_read, adts_seek, adts_reset, adts_flush, NULL, adts_last_error, NULL, adts_close, { NULL }, { NULL }
};

const maktabasauti_codec_t *maktabasauti_aac_adts_codec(void) { return &maktabasauti_aac_adts_desc; }
