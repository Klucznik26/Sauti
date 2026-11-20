/* MaktabaSauti, modul maktabasauti_dvs: STRUMIENIOWE dekodery Atari Falcon DVSM (PCM, Pack-2 delta, Pack-4 Voice) i AVR (PCM), napisane w czystym C.
 *
 * Plik NIE jest wczytywany do pamieci: dane ida przez maly bufor posredni (4 KiB) i sa dekodowane ramka po ramce, wiec wejscie moze byc nieprzewijalne, bez znanego rozmiaru i
 * dowolnie dlugie (pamiec nie zalezy od dlugosci).  Wyniki dla poprawnych plikow sa IDENTYCZNE bit w bit z dawnymi adapterami (sauti_decoder_dvsm.c / sauti_decoder_avr.c): te same wyrazenia
 * zmiennoprzecinkowe, ta sama tablica krokow, to samo nasycanie.  Zachowanie przy niepelnych danych jest strumieniowe: ramki sprzed ucietego miejsca sa wydawane, potem E_TRUNCATED (stary
 * adapter odrzucal wtedy caly plik).  Naglowek jest walidowany jak dawniej.
 *
 * Przewijanie (wejscie przewijalne ze znanym rozmiarem): PCM i AVR dokladnie z arytmetyki pozycji; delta (Pack-2/4) przez punkty kontrolne akumulatorow (ograniczona pula, krok
 * podwajany), potem odtworzenie od najblizszego punktu.  Bledy: lepki E_TRUNCATED (urwana ramka / blok), czyszczony przez seek/reset/flush.
 *
 * Ograniczenia opisane w docs/MAKTABASAUTI_API.md: DTM (Digital Tracker) jest formatem modulowym wymagajacym calego pliku (probki + wzorce): zostaje adapterem z calym buforem. */
#include "maktabasauti/maktabasauti_codec.h"
#include <math.h>
#include <string.h>

#define STAGE_BYTES 4096u
#define CP_MAX 1024u
#define S16 3.0517578125e-05f                             /* 2^-15, dokladnie jak w jadrach asemblerowych (0x38000000) */
#define S8 0.0078125f                                     /* 2^-7 (0x3c000000) */

/* ---- bufor posredni: nic nie jest tracone po E_WOULD_BLOCK (bajty zostaja w buforze) ---- */
typedef struct { uint8_t buf[STAGE_BYTES]; size_t at, have; } stage_t;
static maktabasauti_result_t st_need(const maktabasauti_host_t *h, maktabasauti_reader_t *rd, stage_t *s, size_t n) {
    if (s->have - s->at >= n) return MAKTABASAUTI_OK;
    if (s->at) { memmove(s->buf, s->buf + s->at, s->have - s->at); s->have -= s->at; s->at = 0; }
    while (s->have < n) {
        const int64_t m = h->read(h->user, rd, s->buf + s->have, STAGE_BYTES - s->have);
        if (m < 0) return (maktabasauti_result_t)m;
        if (m == 0) return MAKTABASAUTI_EOF;                                              /* mniej niz n bajtow do konca wejscia */
        s->have += (size_t)m;
    }
    return MAKTABASAUTI_OK;
}
static size_t st_avail(const stage_t *s) { return s->have - s->at; }
static void st_clear(stage_t *s) { s->at = s->have = 0; }

static uint16_t be16(const uint8_t *p) { return (uint16_t)(((unsigned)p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static int32_t sat16(int32_t v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); }
static int32_t seed16(const uint8_t *p) { return (int32_t)(int16_t)be16(p); }
static uint64_t default_mask(uint32_t ch) { return ch == 1u ? MAKTABASAUTI_LAYOUT_MONO : (ch == 2u ? MAKTABASAUTI_LAYOUT_STEREO : 0u); }

static void fill_common(maktabasauti_stream_info_t *info, uint32_t hz, uint32_t ch, uint64_t total, int seekable, int lossless, const char *codec, const char *container) {
    memset(info, 0, sizeof *info);
    info->struct_size = sizeof *info;
    info->sample_rate = hz; info->channels = ch; info->channel_mask = default_mask(ch);
    info->native_format = MAKTABASAUTI_FORMAT_F32;
    info->bits_per_sample = 0;
    info->total_frames = total ? total : MAKTABASAUTI_UNKNOWN_LENGTH;
    info->flags = (seekable ? MAKTABASAUTI_STREAM_SEEKABLE : 0u) | (lossless ? MAKTABASAUTI_STREAM_LOSSLESS : 0u);
    strncpy(info->codec_name, codec, sizeof info->codec_name - 1u);
    strncpy(info->container_name, container, sizeof info->container_name - 1u);
}

/* przesuniecie wejscia (przy otwarciu): bajty za naglowkiem; wejscie nieprzewijalne: odczyt i odrzucenie */
static maktabasauti_result_t skip_input(const maktabasauti_host_t *h, maktabasauti_reader_t *rd, uint64_t n) {
    uint8_t tmp[512];
    while (n) {
        const size_t want = n < sizeof tmp ? (size_t)n : sizeof tmp;
        const int64_t m = h->read(h->user, rd, tmp, want);
        if (m < 0) return (maktabasauti_result_t)m;
        if (m == 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        n -= (uint64_t)m;
    }
    return MAKTABASAUTI_OK;
}

/* =============================================================== AVR =============================================================== */
typedef struct {
    const maktabasauti_host_t *host; maktabasauti_reader_t *rd; stage_t st;
    uint32_t channels, bits, is_signed; uint64_t total, pos; int64_t data_off; int seekable;
    maktabasauti_result_t pend;
} avr_t;
enum { AVR_HEADER = 128 };

static int32_t MAKTABASAUTI_CALL avr_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    (void)self; (void)hint;
    return (p && n >= 4u && memcmp(p, "2BIT", 4) == 0) ? 50 : 0;
}
static maktabasauti_result_t MAKTABASAUTI_CALL avr_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *h, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    uint8_t hd[AVR_HEADER]; int64_t n; uint32_t channels, bits, is_signed, rate, units, flags; unsigned c, sg; avr_t *a; maktabasauti_result_t r;
    (void)self; *state = NULL;
    n = h->peek(h->user, rd, hd, sizeof hd);
    if (n < 0) return (maktabasauti_result_t)n;
    if (n < (int64_t)sizeof hd || memcmp(hd, "2BIT", 4) != 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    c = be16(hd + 12);
    if (c == 0) channels = 1; else if (c == 0xffffu) channels = 2; else return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    bits = be16(hd + 14);
    if (bits != 8u && bits != 16u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    sg = be16(hd + 16);
    if (sg == 0) is_signed = 0; else if (sg == 0xffffu) is_signed = 1; else return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if (hd[40] != 0 || hd[41] != 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;                 /* zarezerwowana kompresja musi byc 0 */
    rate = be32(hd + 22) & 0x00ffffffu;
    units = be32(hd + 26);
    if (!rate || !units || units % channels) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;       /* pelne ramki */
    r = skip_input(h, rd, AVR_HEADER);
    if (r != MAKTABASAUTI_OK) return r;
    a = (avr_t *)h->alloc(h->user, sizeof *a);
    if (!a) return MAKTABASAUTI_E_NO_MEMORY;
    memset(a, 0, sizeof *a);
    a->host = h; a->rd = rd; a->channels = channels; a->bits = bits; a->is_signed = is_signed; a->total = units / channels; a->data_off = AVR_HEADER;
    flags = h->input_flags ? h->input_flags(h->user, rd) : 0u;
    a->seekable = (flags & MAKTABASAUTI_INPUT_SEEKABLE) != 0;
    fill_common(info, rate, channels, a->total, a->seekable, 1, "pcm", "avr");
    *state = a;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL avr_read(void *state, void *dst, size_t n, size_t *frames) {
    avr_t *a = (avr_t *)state; float *o = (float *)dst; const size_t fb = (size_t)a->channels * (a->bits / 8u); size_t done = 0; maktabasauti_result_t r = MAKTABASAUTI_OK;
    *frames = 0;
    if (a->pend != MAKTABASAUTI_OK) return a->pend;
    while (done < n) {
        size_t k, i, avail_f, want = n - done;
        if (a->pos >= a->total) { r = MAKTABASAUTI_EOF; break; }
        if ((uint64_t)want > a->total - a->pos) want = (size_t)(a->total - a->pos);
        if (want > STAGE_BYTES / fb) want = STAGE_BYTES / fb;
        r = st_need(a->host, a->rd, &a->st, fb);
        if (r == MAKTABASAUTI_EOF) { r = MAKTABASAUTI_E_TRUNCATED; break; }                      /* dane urwane przed zadeklarowana dlugoscia */
        if (r != MAKTABASAUTI_OK) break;
        avail_f = st_avail(&a->st) / fb;
        if (want > avail_f) want = avail_f;
        for (k = 0; k < want; k++) {
            const uint8_t *p = a->st.buf + a->st.at + k * fb;
            for (i = 0; i < a->channels; i++) {
                if (a->bits == 8u) { const int32_t v = a->is_signed ? (int32_t)(int8_t)p[i] : (int32_t)p[i] - 128; o[(done + k) * a->channels + i] = (float)v * S8; }
                else { const uint16_t w = be16(p + 2u * i); const int32_t v = a->is_signed ? (int32_t)(int16_t)w : (int32_t)w - 32768; o[(done + k) * a->channels + i] = (float)v * S16; }
            }
        }
        a->st.at += want * fb; a->pos += want; done += want; r = MAKTABASAUTI_OK;
    }
    *frames = done;
    if (done) { if (r == MAKTABASAUTI_E_TRUNCATED) a->pend = r; return MAKTABASAUTI_OK; }       /* blad po wydanych ramkach: najpierw ramki, blad przy nastepnym wywolaniu */
    if (r == MAKTABASAUTI_E_TRUNCATED) a->pend = r;
    return r;
}
static maktabasauti_result_t avr_goto(avr_t *a, uint64_t frame) {
    const uint64_t off = (uint64_t)a->data_off + frame * a->channels * (a->bits / 8u);
    if (a->host->seek(a->host->user, a->rd, (int64_t)off, 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;
    st_clear(&a->st); a->pos = frame; a->pend = MAKTABASAUTI_OK;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL avr_seek(void *state, uint64_t frame, uint32_t flags, uint64_t *actual) {
    avr_t *a = (avr_t *)state; maktabasauti_result_t r; (void)flags;
    if (!a->seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;
    if (frame > a->total) return MAKTABASAUTI_E_OUT_OF_RANGE;
    r = avr_goto(a, frame);
    if (r == MAKTABASAUTI_OK) *actual = frame;
    return r;
}
static maktabasauti_result_t MAKTABASAUTI_CALL avr_reset(void *state) { avr_t *a = (avr_t *)state; return a->seekable ? avr_goto(a, 0) : MAKTABASAUTI_E_NOT_SEEKABLE; }
static maktabasauti_result_t MAKTABASAUTI_CALL avr_flush(void *state) { ((avr_t *)state)->pend = MAKTABASAUTI_OK; return MAKTABASAUTI_OK; }
static void MAKTABASAUTI_CALL avr_close(void *state) { avr_t *a = (avr_t *)state; if (a) a->host->free(a->host->user, a); }

/* =============================================================== DVSM ============================================================== */
typedef struct { uint64_t frame; int32_t acc[2]; } cp_t;
typedef struct {
    const maktabasauti_host_t *host; maktabasauti_reader_t *rd; stage_t st;
    uint32_t channels, bits, pack, bpf;                   /* bpf: bajty danych delty na ramke stereo (2 = Pack-2, 1 = Pack-4) */
    uint64_t block, fpb;                                  /* rozmiar bloku stereo w bajtach i liczba ramek pelnego bloku */
    int64_t data_off; int seekable, total_known, tail; uint64_t total;
    uint64_t pos; int32_t acc[2];                         /* nastepna ramka do wydania i akumulatory PO ramce pos-1 */
    int16_t steps[256];
    cp_t *cps; size_t ncp; uint64_t stride;
    maktabasauti_result_t pend;
} dvsm_t;

static int16_t step_value(unsigned i) {                   /* tablica Pack-2: identyczna z dawnym adapterem (floor(pow(1.084618362, i) + 0.5), nasycona) */
    double v;
    if (i == 0) return 0;
    if (i == 128) return -32768;
    if (i < 128) { v = floor(pow(1.084618362, (double)i) + 0.5); if (v > 32767.0) v = 32767.0; return (int16_t)v; }
    v = floor(pow(1.084618362, (double)(256 - i)) + 0.5); if (v > 32767.0) v = 32767.0;
    return (int16_t)-v;
}

static int32_t MAKTABASAUTI_CALL dvsm_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    (void)self; (void)hint;
    return (p && n >= 6u && memcmp(p, "DVSM\0\0", 6) == 0) ? 50 : 0;
}

/* bajt, od ktorego trzeba czytac, zeby zdekodowac ramke `f` (stan akumulatorow sprzed niej jest osobno) */
static uint64_t dvsm_locate(const dvsm_t *d, uint64_t f) {
    if (d->pack == 0u) return (uint64_t)d->data_off + f * d->channels * (d->bits / 8u);
    if (d->channels == 1u) return f == 0 ? (uint64_t)d->data_off : (uint64_t)d->data_off + 2u + (d->pack == 2u ? f - 1u : (f - 1u) / 2u);
    { const uint64_t blk = f / d->fpb, j = f % d->fpb; return (uint64_t)d->data_off + blk * d->block + (j == 0 ? 0u : 4u + (j - 1u) * d->bpf); }
}

static void cp_note(dvsm_t *d) {                          /* punkt kontrolny co `stride` ramek; przy pelnej puli co drugi wypada, a krok sie podwaja */
    size_t i;
    if (!d->cps || d->pos == 0 || d->pos % d->stride != 0) return;
    if (d->ncp && d->cps[d->ncp - 1u].frame >= d->pos) return;                                // juz zapisany (po przewinieciu)
    if (d->ncp == CP_MAX) {
        for (i = 0; i < CP_MAX / 2u; i++) d->cps[i] = d->cps[2u * i];
        d->ncp = CP_MAX / 2u; d->stride *= 2u;
        if (d->pos % d->stride != 0) return;
    }
    d->cps[d->ncp].frame = d->pos; d->cps[d->ncp].acc[0] = d->acc[0]; d->cps[d->ncp].acc[1] = d->acc[1]; d->ncp++;
}

/* jedna ramka; OK / EOF (czyste, na granicy) / E_TRUNCATED (urwana ramka) / blad wejscia (E_WOULD_BLOCK: nic nie zuzyte, ponowienie) */
static maktabasauti_result_t dvsm_frame(dvsm_t *d, float *out) {
    stage_t *s = &d->st; maktabasauti_result_t r; const uint8_t *p;
    cp_note(d);
    if (d->pack == 0u) {
        const size_t fb = (size_t)d->channels * (d->bits / 8u); uint32_t i;
        r = st_need(d->host, d->rd, s, fb);
        if (r == MAKTABASAUTI_EOF) return st_avail(s) ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_EOF;
        if (r != MAKTABASAUTI_OK) return r;
        p = s->buf + s->at;
        for (i = 0; i < d->channels; i++) out[i] = d->bits == 16u ? (float)seed16(p + 2u * i) * S16 : (float)(int32_t)(int8_t)p[i] * S8;
        s->at += fb;
    } else if (d->channels == 1u) {
        if (d->pos == 0) {
            r = st_need(d->host, d->rd, s, 2u);
            if (r == MAKTABASAUTI_EOF) return st_avail(s) ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_EOF;
            if (r != MAKTABASAUTI_OK) return r;
            d->acc[0] = seed16(s->buf + s->at); s->at += 2u;
        } else {
            r = st_need(d->host, d->rd, s, 1u);
            if (r == MAKTABASAUTI_EOF) return MAKTABASAUTI_EOF;
            if (r != MAKTABASAUTI_OK) return r;
            if (d->pack == 2u) { d->acc[0] = sat16(d->acc[0] + d->steps[s->buf[s->at]]); s->at++; }
            else {                                                                           /* Pack-4: dwa kody 4-bitowe w bajcie, starszy pierwszy */
                const uint8_t b = s->buf[s->at]; const unsigned odd = (unsigned)((d->pos - 1u) & 1u);
                d->acc[0] = sat16(d->acc[0] + d->steps[odd ? (b & 15u) : (unsigned)(b >> 4)]);
                if (odd) s->at++;
            }
        }
        out[0] = (float)d->acc[0] * S16;
    } else {
        const uint64_t j = d->pos % d->fpb;
        if (j == 0) {
            r = st_need(d->host, d->rd, s, 4u);
            if (r == MAKTABASAUTI_EOF) return st_avail(s) ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_EOF;
            if (r != MAKTABASAUTI_OK) return r;
            d->acc[0] = seed16(s->buf + s->at); d->acc[1] = seed16(s->buf + s->at + 2u); s->at += 4u;
        } else {
            r = st_need(d->host, d->rd, s, d->bpf);
            if (r == MAKTABASAUTI_EOF) return st_avail(s) ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_EOF;
            if (r != MAKTABASAUTI_OK) return r;
            p = s->buf + s->at;
            if (d->pack == 2u) { d->acc[0] = sat16(d->acc[0] + d->steps[p[0]]); d->acc[1] = sat16(d->acc[1] + d->steps[p[1]]); }
            else { d->acc[0] = sat16(d->acc[0] + d->steps[p[0] >> 4]); d->acc[1] = sat16(d->acc[1] + d->steps[p[0] & 15u]); }
            s->at += d->bpf;
        }
        out[0] = (float)d->acc[0] * S16; out[1] = (float)d->acc[1] * S16;
    }
    d->pos++;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL dvsm_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *h, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    static const uint32_t rates[] = { 8195, 9834, 12293, 16390, 19668, 24585, 32780, 49170 };
    uint8_t hd[16]; int64_t n, size; uint32_t hz, pack, mode, channels, bits, flags; uint64_t offset, block, bytes = 0, total = 0, tail = 0; dvsm_t *d; maktabasauti_result_t r; unsigned i;
    (void)self; *state = NULL;
    n = h->peek(h->user, rd, hd, sizeof hd);
    if (n < 0) return (maktabasauti_result_t)n;
    if (n < (int64_t)sizeof hd || memcmp(hd, "DVSM\0\0", 6) != 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    offset = be16(hd + 6); hz = be16(hd + 8); pack = hd[10]; mode = hd[11]; block = be32(hd + 12);
    channels = (mode & 2u) ? 1u : 2u; bits = (mode & 1u) ? 16u : 8u;
    if (mode > 3u || offset < 16u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if (pack != 0 && pack != 2u && pack != 4u) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;       /* ADPCM i PACK override: nieobslugiwane (jak dawniej) */
    if ((pack == 2u || pack == 4u) && bits != 16u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if (hz < 8u) hz = rates[hz];
    if (hz < 256u || hz > 384000u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if ((pack == 2u || pack == 4u) && channels == 2u && (block < 4u || (pack == 2u && (block & 1u)))) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    size = h->size(h->user, rd);
    if (size >= 0) {                                                                          /* znany rozmiar: dlugosc z arytmetyki, ta sama walidacja co w starym adapterze */
        const uint64_t fb = (uint64_t)channels * (bits / 8u);
        if ((uint64_t)size < offset) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        bytes = (uint64_t)size - offset;
        if (bytes == 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        if (pack == 0) { total = bytes / fb; tail = bytes % fb; }
        else if (bytes < 2u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        else if (channels == 1u) total = pack == 2u ? bytes - 1u : 1u + 2u * (bytes - 2u);
        else {
            const uint64_t bpf = pack == 2u ? 2u : 1u, f_full = 1u + (block - 4u) / bpf, full = bytes / block, rem = bytes % block;
            total = full * f_full;
            if (rem >= 4u) { total += 1u + (rem - 4u) / bpf; tail = (rem - 4u) % bpf; } else tail = rem;
        }
        if (total == 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    }
    flags = (size >= 0 && h->input_flags && (h->input_flags(h->user, rd) & MAKTABASAUTI_INPUT_SEEKABLE)) ? 1u : 0u;      /* przewijanie wymaga znanego rozmiaru (zakres, dlugosc) */
    d = (dvsm_t *)h->alloc(h->user, sizeof *d);
    if (!d) return MAKTABASAUTI_E_NO_MEMORY;
    memset(d, 0, sizeof *d);
    d->host = h; d->rd = rd; d->channels = channels; d->bits = bits; d->pack = pack; d->block = block; d->data_off = (int64_t)offset; d->bpf = pack == 2u ? 2u : 1u;
    d->fpb = (pack != 0 && channels == 2u) ? 1u + (block - 4u) / d->bpf : 0u;
    d->total_known = size >= 0; d->total = total; d->tail = tail != 0; d->seekable = (int)flags; d->stride = 4096u;
    if (pack == 4u) { static const int16_t voice[16] = { -8192, -4096, -2048, -1024, -512, -256, -64, 0, 64, 256, 512, 1024, 2048, 4096, 8192, 16384 }; memcpy(d->steps, voice, sizeof voice); }
    if (pack == 2u) for (i = 0; i < 256u; i++) d->steps[i] = step_value(i);
    if (pack != 0 && d->seekable) {
        d->cps = (cp_t *)h->alloc(h->user, CP_MAX * sizeof *d->cps);
        if (!d->cps) { h->free(h->user, d); return MAKTABASAUTI_E_NO_MEMORY; }
    }
    r = skip_input(h, rd, offset);                                                            /* przejscie za naglowek (nieprzewijalne: odczyt; przewijalne: tez, bufor czytnika) */
    if (r != MAKTABASAUTI_OK) { if (d->cps) h->free(h->user, d->cps); h->free(h->user, d); return r; }
    fill_common(info, hz, channels, d->total_known ? total : 0u, d->seekable, 0, "dvs", "dvs");
    *state = d;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL dvsm_read(void *state, void *dst, size_t n, size_t *frames) {
    dvsm_t *d = (dvsm_t *)state; float *o = (float *)dst; size_t done = 0; maktabasauti_result_t r = MAKTABASAUTI_OK;
    *frames = 0;
    if (d->pend != MAKTABASAUTI_OK) return d->pend;
    while (done < n) {
        if (d->total_known && d->pos >= d->total) { r = d->tail ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_EOF; break; }
        r = dvsm_frame(d, o + done * d->channels);
        if (r != MAKTABASAUTI_OK) break;
        done++;
    }
    *frames = done;
    if (r == MAKTABASAUTI_E_TRUNCATED) d->pend = r;
    if (done) return MAKTABASAUTI_OK;
    return r == MAKTABASAUTI_OK ? MAKTABASAUTI_EOF : r;
}

static maktabasauti_result_t dvsm_goto(dvsm_t *d, uint64_t frame) {
    uint64_t from = 0; int32_t acc0 = 0, acc1 = 0; size_t i; float tmp[2];
    if (d->pack != 0 && frame != 0) {
        for (i = d->ncp; i > 0; i--) if (d->cps[i - 1u].frame <= frame) { from = d->cps[i - 1u].frame; acc0 = d->cps[i - 1u].acc[0]; acc1 = d->cps[i - 1u].acc[1]; break; }
    } else from = frame;
    if (d->host->seek(d->host->user, d->rd, (int64_t)dvsm_locate(d, from), 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;
    st_clear(&d->st); d->pos = from; d->acc[0] = acc0; d->acc[1] = acc1; d->pend = MAKTABASAUTI_OK;
    while (d->pos < frame) {                                                                  /* odtworzenie od punktu kontrolnego (ramki odrzucane); ponawianie przy E_WOULD_BLOCK nie dotyczy wejsc przewijalnych */
        const maktabasauti_result_t r = dvsm_frame(d, tmp);
        if (r != MAKTABASAUTI_OK) return r == MAKTABASAUTI_EOF ? MAKTABASAUTI_E_TRUNCATED : r;
    }
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL dvsm_seek(void *state, uint64_t frame, uint32_t flags, uint64_t *actual) {
    dvsm_t *d = (dvsm_t *)state; maktabasauti_result_t r; (void)flags;
    if (!d->seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;
    if (frame > d->total) return MAKTABASAUTI_E_OUT_OF_RANGE;
    r = dvsm_goto(d, frame);
    if (r == MAKTABASAUTI_OK) *actual = frame;
    return r;
}
static maktabasauti_result_t MAKTABASAUTI_CALL dvsm_reset(void *state) { dvsm_t *d = (dvsm_t *)state; return d->seekable ? dvsm_goto(d, 0) : MAKTABASAUTI_E_NOT_SEEKABLE; }
static maktabasauti_result_t MAKTABASAUTI_CALL dvsm_flush(void *state) { ((dvsm_t *)state)->pend = MAKTABASAUTI_OK; return MAKTABASAUTI_OK; }
static void MAKTABASAUTI_CALL dvsm_close(void *state) {
    dvsm_t *d = (dvsm_t *)state;
    if (!d) return;
    if (d->cps) d->host->free(d->host->user, d->cps);
    d->host->free(d->host->user, d);
}
static const char *MAKTABASAUTI_CALL dvs_last_error(const void *state) { (void)state; return NULL; }

const maktabasauti_codec_t maktabasauti_dvs_avr_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "atari-avr-pcm", "Atari AVR PCM (streaming)", "maktabasauti_dvs",
    MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0u, NULL,
    avr_probe, avr_open, avr_read, avr_seek, avr_reset, avr_flush, NULL, dvs_last_error, NULL, avr_close, { NULL }, { NULL }
};
const maktabasauti_codec_t maktabasauti_dvs_dvsm_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "atari-falcon-dvsm-pcm", "Atari Falcon DVSM (PCM, Pack-2, Pack-4; streaming)", "maktabasauti_dvs",
    MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0u, NULL,
    dvsm_probe, dvsm_open, dvsm_read, dvsm_seek, dvsm_reset, dvsm_flush, NULL, dvs_last_error, NULL, dvsm_close, { NULL }, { NULL }
};
const maktabasauti_codec_t *maktabasauti_dvs_avr_codec(void) { return &maktabasauti_dvs_avr_desc; }
const maktabasauti_codec_t *maktabasauti_dvs_dvsm_codec(void) { return &maktabasauti_dvs_dvsm_desc; }
