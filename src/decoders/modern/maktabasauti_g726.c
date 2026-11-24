/* MaktabaSauti, modul maktabasauti_g726: STRUMIENIOWY dekoder ADPCM ITU-T G.726 (16 / 24 / 32 / 40 kbit/s przy 8 kHz, czyli 2 / 3 / 4 / 5 bitow na probke; G.721 = 32 kbit/s,
 * G.723 ADPCM = 24 i 40 kbit/s), napisany w czystym C.
 *
 * Algorytm: Rec. ITU-T G.726 (adaptacyjny kwantyzator z szybka i wolna skala, predyktor 2 bieguny + 6 zer, detekcja tonu i przejscia, kontrola szybkosci adaptacji), w ukladzie
 * implementacji referencyjnej Sun (g72x, domena publiczna: te same stale i tablice co w normie).  Wyjscie: 14-bitowy sygnal odtworzony razy 4 = 16 bitow ze znakiem; f32 = wartosc / 32768.
 * Wzorzec do porownania: dekoder g726 / g726le z FFmpeg (bit w bit, tests/test_maktabasauti_g726.c + scripts/test_g726_vs_ffmpeg.sh).
 *
 * Kontenery (kolejnosc bitow wg FFmpeg, ktory jest wzorcem):
 *   - WAV: znacznik formatu 0x45 / 0x64 / 0x40, wBitsPerSample = 2 / 3 / 4 / 5; kody pakowane od NAJSTARSZEGO bitu ("left-justified", big-endian), mono;
 *   - AU / SND: kodowanie 23 (G.721, 4 bity), 25 (G.723 24 kbit/s, 3 bity), 26 (G.723 40 kbit/s, 5 bitow); kody od NAJMLODSZEGO bitu ("right-justified", little-endian), mono;
 *   - surowy strumien (.g726): bez naglowka; opcje raw.rate (8000), raw.channels (1; wiecej = nieobslugiwane), raw.bits (2 .. 5, domyslnie 4), raw.order (be | le, domyslnie be);
 *     rozpoznanie tylko po rozszerzeniu `.g726` z name_hint (pewnosc 10); inaczej trzeba wymusic kodek `g726-raw`.
 * G.726 ma stan: przewijanie = od poczatku danych z odrzuceniem ramek (O(n)); stan i akumulator bitow sa zerowane przy seek / reset.
 * Dane nie sa wczytywane do pamieci (bufor 4 KiB); liczba probek = bajty * 8 / bity (reszta bitow odrzucana). */
#include "maktabasauti/maktabasauti_codec.h"
#include <stdlib.h>
#include <string.h>

#define STAGE 4096u

/* ---------------------------------------------------------------- rdzen ADPCM ---------------------------------------------------------------- */
typedef struct { int yl, yu, dms, dml, ap, a[2], b[6], pk[2], dq[6], sr[2], td; } g726_t;

static const int power2[15] = { 1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000 };

static const int dqln2[4] = { 116, 365, 365, 116 };
static const int wi2[4] = { -704, 14048, 14048, -704 };
static const int fi2[4] = { 0, 0xE00, 0xE00, 0 };
static const int dqln3[8] = { -2048, 135, 273, 373, 373, 273, 135, -2048 };
static const int wi3[8] = { -128, 960, 4384, 18624, 18624, 4384, 960, -128 };
static const int fi3[8] = { 0, 0x200, 0x400, 0xE00, 0xE00, 0x400, 0x200, 0 };
static const int dqln4[16] = { -2048, 4, 135, 213, 273, 323, 373, 425, 425, 373, 323, 273, 213, 135, 4, -2048 };
static const int wi4[16] = { -12, 18, 41, 64, 112, 198, 355, 1122, 1122, 355, 198, 112, 64, 41, 18, -12 };
static const int fi4[16] = { 0, 0, 0, 0x200, 0x200, 0x200, 0x600, 0xE00, 0xE00, 0x600, 0x200, 0x200, 0x200, 0, 0, 0 };
static const int dqln5[32] = { -2048, -66, 28, 104, 169, 224, 274, 318, 358, 395, 429, 459, 488, 514, 539, 566, 566, 539, 514, 488, 459, 429, 395, 358, 318, 274, 224, 169, 104, 28, -66, -2048 };
static const int wi5[32] = { 448, 448, 768, 1248, 1280, 1312, 1856, 3200, 4512, 5728, 7008, 8960, 11456, 14080, 16928, 22272, 22272, 16928, 14080, 11456, 8960, 7008, 5728, 4512, 3200, 1856, 1312, 1280, 1248, 768, 448, 448 };
static const int fi5[32] = { 0, 0, 0, 0, 0, 0x200, 0x200, 0x200, 0x200, 0x200, 0x400, 0x600, 0x800, 0xA00, 0xC00, 0xC00, 0xC00, 0xC00, 0xA00, 0x800, 0x600, 0x400, 0x200, 0x200, 0x200, 0x200, 0x200, 0, 0, 0, 0, 0 };

static void g726_init(g726_t *s) {
    int i;
    memset(s, 0, sizeof *s);
    s->yl = 34816; s->yu = 544;
    for (i = 0; i < 6; i++) s->dq[i] = 32;
    s->sr[0] = s->sr[1] = 32;
}
static int quan(int val, const int *table, int size) { int i; for (i = 0; i < size; i++) if (val < table[i]) break; return i; }
static int fmult(int an, int srn) {
    int anmag, anexp, anmant, wanexp, wanmant, retval;
    anmag = (an > 0) ? an : ((-an) & 0x1FFF);
    anexp = quan(anmag, power2, 15) - 6;
    anmant = (anmag == 0) ? 32 : (anexp >= 0) ? anmag >> anexp : anmag << -anexp;
    wanexp = anexp + ((srn >> 6) & 0xF) - 13;
    wanmant = (anmant * (srn & 077) + 0x30) >> 4;
    retval = (wanexp >= 0) ? ((wanmant << wanexp) & 0x7FFF) : (wanmant >> -wanexp);
    return ((an ^ srn) < 0) ? -retval : retval;
}
static int predictor_zero(const g726_t *s) {
    int i, sezi = fmult(s->b[0] >> 2, s->dq[0]);
    for (i = 1; i < 6; i++) sezi += fmult(s->b[i] >> 2, s->dq[i]);
    return sezi;
}
static int predictor_pole(const g726_t *s) { return fmult(s->a[1] >> 2, s->sr[1]) + fmult(s->a[0] >> 2, s->sr[0]); }
static int step_size(const g726_t *s) {
    int y, dif, al;
    if (s->ap >= 256) return s->yu;
    y = s->yl >> 6; dif = s->yu - y; al = s->ap >> 2;
    if (dif > 0) y += (dif * al) >> 6; else if (dif < 0) y += (dif * al + 0x3F) >> 6;
    return y;
}
static int reconstruct(int sign, int dqln, int y) {
    int dql = dqln + (y >> 2), dex, dqt, dq;
    if (dql < 0) return sign ? -0x8000 : 0;
    dex = (dql >> 7) & 15; dqt = 128 + (dql & 127); dq = (dqt << 7) >> (14 - dex);
    return sign ? dq - 0x8000 : dq;
}
static void update(int code_size, int y, int wi, int fi, int dq, int sr, int dqsez, g726_t *s) {
    int cnt, mag, exp, a2p = 0, a1ul, pks1, fa1, tr, ylint, ylfrac, thr1, thr2, dqthr, pk0 = dqsez < 0 ? 1 : 0;
    mag = dq & 0x7FFF;
    ylint = s->yl >> 15; ylfrac = (s->yl >> 10) & 0x1F; thr1 = (32 + ylfrac) << ylint;
    thr2 = (ylint > 9) ? 31 << 10 : thr1;
    dqthr = (thr2 + (thr2 >> 1)) >> 1;
    tr = (s->td == 0 || mag <= dqthr) ? 0 : 1;
    s->yu = y + ((wi - y) >> 5);
    if (s->yu < 544) s->yu = 544; else if (s->yu > 5120) s->yu = 5120;
    s->yl += s->yu + ((-s->yl) >> 6);
    if (tr == 1) {
        s->a[0] = s->a[1] = 0;
        for (cnt = 0; cnt < 6; cnt++) s->b[cnt] = 0;
    } else {
        pks1 = pk0 ^ s->pk[0];
        a2p = s->a[1] - (s->a[1] >> 7);
        if (dqsez != 0) {
            fa1 = pks1 ? s->a[0] : -s->a[0];
            if (fa1 < -8191) a2p -= 0x100; else if (fa1 > 8191) a2p += 0xFF; else a2p += fa1 >> 5;
            if (pk0 ^ s->pk[1]) { if (a2p <= -12160) a2p = -12288; else if (a2p >= 12416) a2p = 12288; else a2p -= 0x80; }
            else { if (a2p <= -12416) a2p = -12288; else if (a2p >= 12160) a2p = 12288; else a2p += 0x80; }
        }
        s->a[1] = a2p;
        s->a[0] -= s->a[0] >> 8;
        if (dqsez != 0) { if (pks1 == 0) s->a[0] += 192; else s->a[0] -= 192; }
        a1ul = 15360 - a2p;
        if (s->a[0] < -a1ul) s->a[0] = -a1ul; else if (s->a[0] > a1ul) s->a[0] = a1ul;
        for (cnt = 0; cnt < 6; cnt++) {
            if (code_size == 5) s->b[cnt] -= s->b[cnt] >> 9; else s->b[cnt] -= s->b[cnt] >> 8;
            if (dq & 0x7FFF) { if ((dq ^ s->dq[cnt]) >= 0) s->b[cnt] += 128; else s->b[cnt] -= 128; }
        }
    }
    for (cnt = 5; cnt > 0; cnt--) s->dq[cnt] = s->dq[cnt - 1];
    if (mag == 0) s->dq[0] = (dq >= 0) ? 0x20 : -992;                  /* 0xFC20 jako 16 bit ze znakiem: znak ma byc zachowany w XOR z dq */
    else { exp = quan(mag, power2, 15); s->dq[0] = (dq >= 0) ? (exp << 6) + ((mag << 6) >> exp) : (exp << 6) + ((mag << 6) >> exp) - 0x400; }
    s->sr[1] = s->sr[0];
    if (sr == 0) s->sr[0] = 0x20;
    else if (sr > 0) { exp = quan(sr, power2, 15); s->sr[0] = (exp << 6) + ((sr << 6) >> exp); }
    else if (sr > -32768) { mag = -sr; exp = quan(mag, power2, 15); s->sr[0] = (exp << 6) + ((mag << 6) >> exp) - 0x400; }
    else s->sr[0] = -992;
    s->pk[1] = s->pk[0]; s->pk[0] = pk0;
    if (tr == 1) s->td = 0; else if (a2p < -11776) s->td = 1; else s->td = 0;
    s->dms += (fi - s->dms) >> 5;
    s->dml += ((fi << 2) - s->dml) >> 7;
    if (tr == 1) s->ap = 256;
    else if (y < 1536) s->ap += (0x200 - s->ap) >> 4;
    else if (s->td == 1) s->ap += (0x200 - s->ap) >> 4;
    else if (abs((s->dms << 2) - s->dml) >= (s->dml >> 3)) s->ap += (0x200 - s->ap) >> 4;
    else s->ap += (-s->ap) >> 4;
}
/* jedna probka: `code` o `bits` bitach -> 16 bit ze znakiem */
static int g726_sample(g726_t *s, unsigned code, unsigned bits) {
    const int *dqln, *wi, *fi; int i, sezi, sez, sei, se, y, dq, sr, dqsez, v;
    switch (bits) {
    case 2: dqln = dqln2; wi = wi2; fi = fi2; break;
    case 3: dqln = dqln3; wi = wi3; fi = fi3; break;
    case 5: dqln = dqln5; wi = wi5; fi = fi5; break;
    default: dqln = dqln4; wi = wi4; fi = fi4; break;
    }
    i = (int)(code & ((1u << bits) - 1u));
    sezi = predictor_zero(s); sez = sezi >> 1; sei = sezi + predictor_pole(s); se = sei >> 1;
    y = step_size(s);
    dq = reconstruct(i & (1 << (bits - 1u)), dqln[i], y);
    sr = (dq < 0) ? se - (dq & 0x3FFF) : se + dq;
    dqsez = sr - se + sez;
    update((int)bits, y, bits == 4u ? wi[i] * 32 : wi[i], fi[i], dq, sr, dqsez, s);
    v = sr * 4;
    return ((v + 32768) & 0xFFFF) - 32768;                             /* 16 bit ze znakiem z zawinieciem, jak implementacja Sun i spandsp (strumienie poza zakresem 14 bitow) */
}

/* ---------------------------------------------------------------- strumien ---------------------------------------------------------------- */
typedef struct {
    const maktabasauti_host_t *host; maktabasauti_reader_t *rd;
    uint8_t buf[STAGE]; size_t at, have;
    uint32_t bits; int msb_first;                                      /* msb_first: kody od najstarszego bitu (WAV, raw be), inaczej od najmlodszego (AU, raw le) */
    uint32_t acc, accbits;
    g726_t st;
    uint64_t data_off, total, pos; int total_known, seekable;
    uint64_t bytes_left, data_bytes; int bytes_known;                               /* ile bajtow danych jeszcze wolno wczytac */
    maktabasauti_result_t pend;
} g726s_t;

static maktabasauti_result_t refill(g726s_t *p) {
    size_t want = STAGE - p->have;
    int64_t m;
    if (p->at) { memmove(p->buf, p->buf + p->at, p->have - p->at); p->have -= p->at; p->at = 0; want = STAGE - p->have; }
    if (p->bytes_known && (uint64_t)want > p->bytes_left) want = (size_t)p->bytes_left;
    if (!want) return MAKTABASAUTI_EOF;
    m = p->host->read(p->host->user, p->rd, p->buf + p->have, want);
    if (m < 0) return (maktabasauti_result_t)m;
    if (m == 0) return MAKTABASAUTI_EOF;
    p->have += (size_t)m;
    if (p->bytes_known) p->bytes_left -= (uint64_t)m;
    return MAKTABASAUTI_OK;
}
/* nastepny kod; EOF gdy zabraklo bitow na pelny kod */
static maktabasauti_result_t next_code(g726s_t *p, unsigned *code) {
    while (p->accbits < p->bits) {
        if (p->at >= p->have) {
            maktabasauti_result_t r = refill(p);
            if (r != MAKTABASAUTI_OK) return r;
        }
        if (p->msb_first) p->acc = (p->acc << 8) | p->buf[p->at++];
        else p->acc |= (uint32_t)p->buf[p->at++] << p->accbits;
        p->accbits += 8u;
    }
    if (p->msb_first) { *code = (p->acc >> (p->accbits - p->bits)) & ((1u << p->bits) - 1u); p->accbits -= p->bits; p->acc &= (1u << p->accbits) - 1u; }
    else { *code = p->acc & ((1u << p->bits) - 1u); p->acc >>= p->bits; p->accbits -= p->bits; }
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL g726_read(void *state, void *dst, size_t n, size_t *frames) {
    g726s_t *p = (g726s_t *)state; float *o = (float *)dst; size_t done = 0; maktabasauti_result_t r = MAKTABASAUTI_OK;
    *frames = 0;
    if (p->pend != MAKTABASAUTI_OK) return p->pend;
    while (done < n) {
        unsigned code;
        if (p->total_known && p->pos >= p->total) { r = MAKTABASAUTI_EOF; break; }
        r = next_code(p, &code);
        if (r == MAKTABASAUTI_EOF) { r = p->total_known ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_EOF; break; }
        if (r != MAKTABASAUTI_OK) break;
        o[done++] = (float)g726_sample(&p->st, code, p->bits) / 32768.0f; p->pos++; r = MAKTABASAUTI_OK;
    }
    *frames = done;
    if (done) { if (r == MAKTABASAUTI_E_TRUNCATED) p->pend = r; return MAKTABASAUTI_OK; }
    if (r == MAKTABASAUTI_E_TRUNCATED) p->pend = r;
    return r;
}
static maktabasauti_result_t g726_rewind(g726s_t *p) {
    if (p->host->seek(p->host->user, p->rd, (int64_t)p->data_off, 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;
    p->at = p->have = 0; p->acc = p->accbits = 0; p->pos = 0; p->pend = MAKTABASAUTI_OK; g726_init(&p->st);
    if (p->bytes_known) p->bytes_left = p->data_bytes;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL g726_seek(void *state, uint64_t frame, uint32_t flags, uint64_t *actual) {
    g726s_t *p = (g726s_t *)state; maktabasauti_result_t r; (void)flags;
    if (!p->seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;
    if (p->total_known && frame > p->total) return MAKTABASAUTI_E_OUT_OF_RANGE;
    r = g726_rewind(p);
    while (r == MAKTABASAUTI_OK && p->pos < frame) { unsigned code; r = next_code(p, &code); if (r == MAKTABASAUTI_OK) { (void)g726_sample(&p->st, code, p->bits); p->pos++; } }
    if (r == MAKTABASAUTI_EOF) r = MAKTABASAUTI_E_OUT_OF_RANGE;
    if (r == MAKTABASAUTI_OK) *actual = frame;
    return r;
}
static maktabasauti_result_t MAKTABASAUTI_CALL g726_reset(void *state) { g726s_t *p = (g726s_t *)state; return p->seekable ? g726_rewind(p) : MAKTABASAUTI_E_NOT_SEEKABLE; }
static maktabasauti_result_t MAKTABASAUTI_CALL g726_flush(void *state) { ((g726s_t *)state)->pend = MAKTABASAUTI_OK; return MAKTABASAUTI_OK; }
static const char *MAKTABASAUTI_CALL g726_last_error(const void *state) { (void)state; return NULL; }
static void MAKTABASAUTI_CALL g726_close(void *state) { g726s_t *p = (g726s_t *)state; if (p) p->host->free(p->host->user, p); }

/* ---------------------------------------------------------------- otwarcie ---------------------------------------------------------------- */
static uint16_t le16(const uint8_t *p) { return (uint16_t)((unsigned)p[0] | ((unsigned)p[1] << 8)); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static maktabasauti_result_t read_exact(const maktabasauti_host_t *h, maktabasauti_reader_t *rd, void *dst, size_t len) {
    uint8_t *o = (uint8_t *)dst;
    while (len) {
        const int64_t m = h->read(h->user, rd, o, len);
        if (m < 0) return (maktabasauti_result_t)m;
        if (m == 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;             /* naglowek urwany */
        o += m; len -= (size_t)m;
    }
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t skip_bytes(const maktabasauti_host_t *h, maktabasauti_reader_t *rd, uint64_t n) {
    while (n) {
        uint8_t tmp[256]; const size_t want = n < sizeof tmp ? (size_t)n : sizeof tmp; const int64_t m = h->read(h->user, rd, tmp, want);
        if (m < 0) return (maktabasauti_result_t)m;
        if (m == 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        n -= (uint64_t)m;
    }
    return MAKTABASAUTI_OK;
}
static void g726_fill_info(maktabasauti_stream_info_t *info, const g726s_t *p, uint32_t rate, const char *container) {
    memset(info, 0, sizeof *info);
    info->struct_size = sizeof *info;
    info->sample_rate = rate; info->channels = 1u; info->channel_mask = MAKTABASAUTI_LAYOUT_MONO;
    info->native_format = MAKTABASAUTI_FORMAT_F32;
    info->total_frames = p->total_known ? p->total : MAKTABASAUTI_UNKNOWN_LENGTH;
    info->flags = p->seekable ? MAKTABASAUTI_STREAM_SEEKABLE : 0u;
    strncpy(info->codec_name, "g726", sizeof info->codec_name - 1u);
    strncpy(info->container_name, container, sizeof info->container_name - 1u);
}
/* `consumed` = bajty wejscia zuzyte przez naglowek; data_size < 0 = nieznany */
static maktabasauti_result_t g726_finish(const maktabasauti_host_t *h, maktabasauti_reader_t *rd, uint32_t bits, int msb_first, uint32_t rate, uint64_t consumed, int64_t data_size,
                                         const char *container, maktabasauti_stream_info_t *info, void **state) {
    g726s_t *p = (g726s_t *)h->alloc(h->user, sizeof *p);
    const uint32_t fl = h->input_flags ? h->input_flags(h->user, rd) : 0u;
    if (!p) return MAKTABASAUTI_E_NO_MEMORY;
    memset(p, 0, sizeof *p);
    p->host = h; p->rd = rd; p->bits = bits; p->msb_first = msb_first; p->data_off = consumed; p->seekable = (fl & MAKTABASAUTI_INPUT_SEEKABLE) != 0;
    g726_init(&p->st);
    if (data_size >= 0) { p->bytes_known = 1; p->bytes_left = p->data_bytes = (uint64_t)data_size; p->total = (uint64_t)data_size * 8u / bits; p->total_known = 1; }
    g726_fill_info(info, p, rate, container);
    *state = p;
    return MAKTABASAUTI_OK;
}

/* ---- WAV ---- */
static int wav_tag_ok(unsigned t) { return t == 0x45u || t == 0x64u || t == 0x40u; }
static int32_t MAKTABASAUTI_CALL g726_wav_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    size_t off = 12;
    (void)self; (void)hint;
    if (!p || n < 12u || memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4)) return 0;
    while (off + 8u <= n) {                                                  /* znacznik formatu z prefiksu: pewnosc wyzsza niz wav-pcm (50) */
        const uint32_t sz = le32(p + off + 4);
        if (!memcmp(p + off, "fmt ", 4)) return (off + 10u <= n && wav_tag_ok(le16(p + off + 8))) ? 60 : 0;
        if (off + 8u + (uint64_t)sz + (sz & 1u) > n) break;
        off += 8u + sz + (sz & 1u);
    }
    return 40;                                                               /* fmt dalej niz prefiks: wav-pcm odmowi, potem my */
}
static maktabasauti_result_t MAKTABASAUTI_CALL g726_wav_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *h, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    uint8_t hd[12], ck[8], fmt[16]; uint64_t consumed = 12; uint32_t rate = 0, bits = 0; int have_fmt = 0, n_chunks = 0; maktabasauti_result_t r;
    (void)self; *state = NULL;
    r = read_exact(h, rd, hd, 12); if (r != MAKTABASAUTI_OK) return r;
    if (memcmp(hd, "RIFF", 4) || memcmp(hd + 8, "WAVE", 4)) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    for (;;) {
        uint32_t sz; uint64_t skip;
        if (++n_chunks > 4096) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        r = read_exact(h, rd, ck, 8); if (r != MAKTABASAUTI_OK) return r;
        consumed += 8u; sz = le32(ck + 4);
        if (!memcmp(ck, "fmt ", 4)) {
            if (have_fmt || sz < 16u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            r = read_exact(h, rd, fmt, 16); if (r != MAKTABASAUTI_OK) return r;
            consumed += 16u; have_fmt = 1;
            if (!wav_tag_ok(le16(fmt))) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            if (le16(fmt + 2) != 1u) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;     /* G.726 wielokanalowe: bez wzorca */
            rate = le32(fmt + 4); bits = le16(fmt + 14);
            if (!rate || rate > 768000u || bits < 2u || bits > 5u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            skip = (uint64_t)sz - 16u + (sz & 1u);
            r = skip_bytes(h, rd, skip); if (r != MAKTABASAUTI_OK) return r;
            consumed += skip;
        } else if (!memcmp(ck, "data", 4)) {
            int64_t size;
            if (!have_fmt) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            if (sz == 0u || sz == 0xffffffffu) { const int64_t in = h->size ? h->size(h->user, rd) : -1; size = in >= (int64_t)consumed ? in - (int64_t)consumed : -1; }   /* zapis strumieniowy: reszta wejscia */
            else size = (int64_t)sz;
            return g726_finish(h, rd, bits, 1, rate, consumed, size, "wav", info, state);
        } else {
            skip = (uint64_t)sz + (sz & 1u);
            r = skip_bytes(h, rd, skip); if (r != MAKTABASAUTI_OK) return r;
            consumed += skip;
        }
    }
}

/* ---- AU ---- */
static int32_t MAKTABASAUTI_CALL g726_au_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    (void)self; (void)hint;
    if (!p || n < 24u || memcmp(p, ".snd", 4)) return 0;
    { const uint32_t enc = be32(p + 12); return (enc == 23u || enc == 25u || enc == 26u) ? 60 : 0; }
}
static maktabasauti_result_t MAKTABASAUTI_CALL g726_au_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *h, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    uint8_t hd[24]; uint32_t off, size, enc, rate, channels, bits; int64_t data_size; maktabasauti_result_t r;
    (void)self; *state = NULL;
    r = read_exact(h, rd, hd, 24); if (r != MAKTABASAUTI_OK) return r;
    if (memcmp(hd, ".snd", 4)) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    off = be32(hd + 4); size = be32(hd + 8); enc = be32(hd + 12); rate = be32(hd + 16); channels = be32(hd + 20);
    if (off < 24u || off > (16u << 20) || rate < 1u || rate > 768000u || channels < 1u || channels > 32u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if (enc != 23u && enc != 25u && enc != 26u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if (channels != 1u) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    bits = enc == 23u ? 4u : enc == 25u ? 3u : 5u;
    r = skip_bytes(h, rd, off - 24u); if (r != MAKTABASAUTI_OK) return r;
    if (size == 0xffffffffu) { const int64_t in = h->size ? h->size(h->user, rd) : -1; data_size = in >= (int64_t)off ? in - (int64_t)off : -1; }
    else data_size = (int64_t)size;
    return g726_finish(h, rd, bits, 0, rate, off, data_size, "au", info, state);
}

/* ---- surowy ---- */
static int32_t MAKTABASAUTI_CALL g726_raw_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    const char *dot;
    (void)self; (void)p; (void)n;
    if (!hint) return 0;
    dot = strrchr(hint, '.'); dot = dot ? dot + 1 : hint;
    return (!strcmp(dot, "g726") || !strcmp(dot, "G726")) ? 10 : 0;
}
static maktabasauti_result_t raw_u32(const maktabasauti_host_t *h, const char *key, uint32_t def, uint32_t lo, uint32_t hi, uint32_t *out) {
    const char *v = h->option ? h->option(h->user, key) : NULL; char *end; unsigned long x;
    *out = def;
    if (!v || !v[0]) return MAKTABASAUTI_OK;
    x = strtoul(v, &end, 10);
    if (*end || x < lo || x > hi) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    *out = (uint32_t)x;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL g726_raw_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *h, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    uint32_t rate, channels, bits; maktabasauti_result_t r; int msb = 1; int64_t size;
    const char *order = h->option ? h->option(h->user, "raw.order") : NULL;
    (void)self; *state = NULL;
    r = raw_u32(h, "raw.rate", 8000u, 1u, 768000u, &rate); if (r != MAKTABASAUTI_OK) return r;
    r = raw_u32(h, "raw.channels", 1u, 1u, 8u, &channels); if (r != MAKTABASAUTI_OK) return r;
    r = raw_u32(h, "raw.bits", 4u, 2u, 5u, &bits); if (r != MAKTABASAUTI_OK) return r;
    if (order && order[0]) { if (!strcmp(order, "le")) msb = 0; else if (strcmp(order, "be")) return MAKTABASAUTI_E_INVALID_ARGUMENT; }
    if (channels != 1u) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    size = h->size ? h->size(h->user, rd) : -1;
    return g726_finish(h, rd, bits, msb, rate, 0, size, "raw", info, state);
}

#define G726_FLAGS (MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK)
const maktabasauti_codec_t maktabasauti_g726_wav_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "g726-wav", "ITU-T G.726 ADPCM in WAV (streaming)", "maktabasauti_g726", G726_FLAGS, 0u, NULL,
    g726_wav_probe, g726_wav_open, g726_read, g726_seek, g726_reset, g726_flush, NULL, g726_last_error, NULL, g726_close, { NULL }, { NULL }
};
const maktabasauti_codec_t maktabasauti_g726_au_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "g726-au", "ITU-T G.726 / G.721 / G.723 ADPCM in Sun AU (streaming)", "maktabasauti_g726", G726_FLAGS, 0u, NULL,
    g726_au_probe, g726_au_open, g726_read, g726_seek, g726_reset, g726_flush, NULL, g726_last_error, NULL, g726_close, { NULL }, { NULL }
};
const maktabasauti_codec_t maktabasauti_g726_raw_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "g726-raw", "ITU-T G.726 ADPCM without a header (options raw.rate, raw.bits, raw.order)", "maktabasauti_g726", G726_FLAGS, 0u, NULL,
    g726_raw_probe, g726_raw_open, g726_read, g726_seek, g726_reset, g726_flush, NULL, g726_last_error, NULL, g726_close, { NULL }, { NULL }
};
