/* MaktabaSauti, modul maktabasauti_pcm: STRUMIENIOWE dekodery RIFF/WAVE PCM, AIFF / AIFF-C (NONE, sowt) i Sun/NeXT AU (.au / .snd), napisane w czystym C.
 *
 * G.711 (ITU-T, 8 bit, 8 kHz w telefonii, tu dowolna czestotliwosc zapisana w pliku): mu-law i A-law w WAV (format 6 / 7, takze WAVE_FORMAT_EXTENSIBLE), AIFF-C (ulaw / ULAW / alaw / ALAW) i AU (kodowanie 1 / 27).
 * Dekodowanie = definicja ITU-T G.711 (segmenty, wartosc srodka przedzialu; jak implementacja referencyjna Sun, domena publiczna): 16-bitowa liczba ze znakiem, wyjscie f32 = wartosc / 32768.
 *
 * Plik NIE jest wczytywany do pamieci: dane ida przez maly bufor posredni (4 KiB), wiec wejscie moze byc nieprzewijalne, bez znanego rozmiaru i dowolnie dlugie.  Dla poprawnych plikow wynik jest
 * IDENTYCZNY bit w bit z dawnymi adapterami (sauti_decoder_wav.c / sauti_decoder_aiff.c): te same wyrazenia (8 bit bez znaku: (v-128)/128, pozostale: wartosc ze znakiem / 2^(bits-1)).
 *
 * Naglowek jest czytany z prefiksu okna czytnika (peek; do czasu otwarcia nic nie jest zuzywane, wiec E_WOULD_BLOCK jest wznawialny); gdy nie miesci sie w prefiksie (duzy chunk przed danymi),
 * moduly przechodza chunk po chunku (seek albo odczyt z odrzuceniem).  Kontrole naglowka jak w dawnych adapterach (rozmiary chunkow, nagłowek fmt, rozmiary ramek, czestotliwosc AIFF), z roznicami
 * wynikajacymi ze strumieniowania:
 *   - plik ucięty: ramki sprzed ucięcia sa wydawane, potem E_TRUNCATED (stary adapter odrzucal caly plik przy otwarciu);
 *   - koniec naglowka dalej niz dane (chunki za danymi, np. LIST) nie sa sprawdzane, gdy fmt/COMM wystepuje przed danymi; wejscie przewijalne szuka ich za danymi;
 *   - WAV z rozmiarem RIFF 0 / 0xFFFFFFFF albo rozmiarem data 0xFFFFFFFF (zapis strumieniowy, np. potok): dlugosc = reszta wejscia (nieznana, gdy rozmiar wejscia nieznany);
 *   - AU: kodowania 1 (mu-law), 27 (A-law), 2..5 (PCM ze znakiem 8 / 16 / 24 / 32 bit, big-endian); rozmiar danych 0xffffffff = reszta wejscia; niepelna ostatnia ramka jest pomijana;
 *   - WAV IEEE float 32/64 bit (format 3, takze WAVE_FORMAT_EXTENSIBLE): dekoder to czyta; dawny adapter odrzucal te pliki (stala kontrola format == 1).
 *
 * Przewijanie: dokladne z arytmetyki pozycji (wejscie przewijalne).  Bledy sa lepkie (E_TRUNCATED), czyszczone przez seek/reset/flush. */
#include "maktabasauti/maktabasauti_codec.h"
#include <math.h>
#include <string.h>

#define STAGE_BYTES 4096u
#define PREFIX_BYTES 512u
#define MAX_CHUNKS 65536u

typedef struct { uint8_t buf[STAGE_BYTES]; size_t at, have; } stage_t;
static maktabasauti_result_t st_need(const maktabasauti_host_t *h, maktabasauti_reader_t *rd, stage_t *s, size_t n) {
    if (s->have - s->at >= n) return MAKTABASAUTI_OK;
    if (s->at) { memmove(s->buf, s->buf + s->at, s->have - s->at); s->have -= s->at; s->at = 0; }
    while (s->have < n) {
        const int64_t m = h->read(h->user, rd, s->buf + s->have, STAGE_BYTES - s->have);
        if (m < 0) return (maktabasauti_result_t)m;
        if (m == 0) return MAKTABASAUTI_EOF;
        s->have += (size_t)m;
    }
    return MAKTABASAUTI_OK;
}
static size_t st_avail(const stage_t *s) { return s->have - s->at; }
static void st_clear(stage_t *s) { s->at = s->have = 0; }

static uint16_t le16(const uint8_t *p) { return (uint16_t)((unsigned)p[0] | ((unsigned)p[1] << 8)); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(((unsigned)p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static int is_tag(const uint8_t *p, const char *tag) { return memcmp(p, tag, 4) == 0; }
static uint64_t default_mask(uint32_t ch) {
    switch (ch) {
    case 1: return MAKTABASAUTI_LAYOUT_MONO;
    case 2: return MAKTABASAUTI_LAYOUT_STEREO;
    case 6: return MAKTABASAUTI_LAYOUT_5_1;
    case 8: return MAKTABASAUTI_LAYOUT_7_1;
    default: return 0;
    }
}

/* ---- skaner naglowka: odczyt na bezwzglednych przesunieciach, z prefiksu albo z wejscia ---- */
typedef struct {
    const maktabasauti_host_t *h; maktabasauti_reader_t *rd;
    uint8_t pre[PREFIX_BYTES]; size_t pre_n;
    uint64_t pos;                                                  /* pozycja wejscia (bajty zuzyte lub przewiniete) */
    int seekable;
} scan_t;

static maktabasauti_result_t sc_move(scan_t *s, uint64_t off) {
    if (s->pos == off) return MAKTABASAUTI_OK;
    if (s->seekable) {
        const int64_t r = s->h->seek(s->h->user, s->rd, (int64_t)off, 0);
        if (r < 0) return r == MAKTABASAUTI_E_OUT_OF_RANGE ? MAKTABASAUTI_E_UNSUPPORTED_FORMAT : (maktabasauti_result_t)r;   /* chunk poza koncem pliku = uszkodzony naglowek */
        s->pos = off;
        return MAKTABASAUTI_OK;
    }
    if (off < s->pos) return MAKTABASAUTI_E_NOT_SEEKABLE;
    while (s->pos < off) {
        uint8_t tmp[256]; const uint64_t left = off - s->pos; const size_t want = left < sizeof tmp ? (size_t)left : sizeof tmp;
        const int64_t m = s->h->read(s->h->user, s->rd, tmp, want);
        if (m < 0) return (maktabasauti_result_t)m;
        if (m == 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        s->pos += (uint64_t)m;
    }
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t sc_fetch(scan_t *s, uint64_t off, void *dst, size_t len) {
    uint8_t *o = (uint8_t *)dst; maktabasauti_result_t r;
    if (off + len <= s->pre_n) { memcpy(dst, s->pre + off, len); return MAKTABASAUTI_OK; }
    r = sc_move(s, off);
    if (r != MAKTABASAUTI_OK) return r;
    while (len) {
        const int64_t m = s->h->read(s->h->user, s->rd, o, len);
        if (m < 0) return (maktabasauti_result_t)m;
        if (m == 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;                                   /* naglowek urwany */
        o += m; len -= (size_t)m; s->pos += (uint64_t)m;
    }
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t sc_begin(scan_t *s, const maktabasauti_host_t *h, maktabasauti_reader_t *rd) {
    int64_t n; const uint32_t fl = h->input_flags ? h->input_flags(h->user, rd) : 0u;
    memset(s->pre, 0, sizeof s->pre);                                                              /* nic nie czytamy spoza pre_n, ale bufor ma byc okreslony */
    s->h = h; s->rd = rd; s->pos = 0; s->seekable = (fl & MAKTABASAUTI_INPUT_SEEKABLE) != 0;
    n = h->peek(h->user, rd, s->pre, sizeof s->pre);
    if (n < 0) return (maktabasauti_result_t)n;
    s->pre_n = (size_t)n;
    return MAKTABASAUTI_OK;
}

/* ---- G.711 (ITU-T): mu-law i A-law -> 16 bit ze znakiem; wzor implementacji referencyjnej Sun (g711.c, domena publiczna), tablice tylko z definicji segmentow ---- */
static int g711_ulaw(uint8_t u) {
    int t; u = (uint8_t)~u;
    t = (((int)u & 0x0f) << 3) + 0x84;
    t <<= ((int)u & 0x70) >> 4;
    return (u & 0x80u) ? 0x84 - t : t - 0x84;
}
static int g711_alaw(uint8_t a) {
    int t, seg; a ^= 0x55u;
    t = ((int)a & 0x0f) << 4;
    seg = ((int)a & 0x70) >> 4;
    if (seg == 0) t += 8;
    else { t += 0x108; if (seg > 1) t <<= seg - 1; }
    return (a & 0x80u) ? t : -t;
}

/* ---- wspolny stan odczytu ---- */
typedef struct {
    const maktabasauti_host_t *host; maktabasauti_reader_t *rd; stage_t st;
    uint32_t channels, bits, width; int kind;                         /* kind: 0 = WAV PCM, 1 = WAV float, 2 = AIFF / AU big-endian, 3 = AIFF little-endian (sowt), 4 = G.711 mu-law, 5 = G.711 A-law */
    uint64_t data_off, total, pos; int seekable, total_known;
    maktabasauti_result_t pend;
} pcm_t;

static void fill_info(maktabasauti_stream_info_t *info, const pcm_t *p, uint32_t rate, const char *container) {
    memset(info, 0, sizeof *info);
    info->struct_size = sizeof *info;
    info->sample_rate = rate; info->channels = p->channels; info->channel_mask = default_mask(p->channels);
    info->native_format = MAKTABASAUTI_FORMAT_F32;
    info->bits_per_sample = 0;
    info->total_frames = (p->total_known && p->total) ? p->total : MAKTABASAUTI_UNKNOWN_LENGTH;
    info->flags = (p->seekable ? MAKTABASAUTI_STREAM_SEEKABLE : 0u) | (p->kind >= 4 ? 0u : MAKTABASAUTI_STREAM_LOSSLESS);
    strncpy(info->codec_name, p->kind == 4 ? "g711-ulaw" : p->kind == 5 ? "g711-alaw" : "pcm", sizeof info->codec_name - 1u);
    strncpy(info->container_name, container, sizeof info->container_name - 1u);
}

static float sample_value(const pcm_t *p, const uint8_t *v) {
    switch (p->kind) {
    case 4: return (float)g711_ulaw(v[0]) / 32768.0f;
    case 5: return (float)g711_alaw(v[0]) / 32768.0f;
    case 1:
        if (p->bits == 32u) { uint32_t raw = le32(v); float x; memcpy(&x, &raw, 4); return isfinite(x) ? x : 0.0f; }
        else { uint64_t raw = (uint64_t)le32(v) | ((uint64_t)le32(v + 4) << 32); double x; memcpy(&x, &raw, 8); return isfinite(x) ? (float)x : 0.0f; }
    case 0:
        if (p->bits == 8u) return ((float)v[0] - 128.0f) / 128.0f;
        { const uint32_t raw = p->bits == 16u ? le16(v) : p->bits == 24u ? ((uint32_t)v[0] | ((uint32_t)v[1] << 8) | ((uint32_t)v[2] << 16)) : le32(v);
          const int32_t value = (int32_t)(raw << (32u - p->bits));
          return (float)value / 2147483648.0f; }
    default: {
        uint32_t raw = 0; unsigned j; const uint64_t sign = UINT64_C(1) << (p->bits - 1u); int64_t value;
        for (j = 0; j < p->width; j++) { if (p->kind == 3) raw |= (uint32_t)v[j] << (8u * j); else raw = (raw << 8) | v[j]; }
        value = (raw & sign) ? (int64_t)raw - (int64_t)(sign * 2u) : (int64_t)raw;
        return (float)((double)value / (double)sign);
    }
    }
}

static maktabasauti_result_t MAKTABASAUTI_CALL pcm_read(void *state, void *dst, size_t n, size_t *frames) {
    pcm_t *p = (pcm_t *)state; float *o = (float *)dst; const size_t fb = (size_t)p->channels * p->width; size_t done = 0; maktabasauti_result_t r = MAKTABASAUTI_OK;
    *frames = 0;
    if (p->pend != MAKTABASAUTI_OK) return p->pend;
    while (done < n) {
        size_t k, i, avail_f, want = n - done;
        if (p->total_known && p->pos >= p->total) { r = MAKTABASAUTI_EOF; break; }
        if (p->total_known && (uint64_t)want > p->total - p->pos) want = (size_t)(p->total - p->pos);
        if (want > STAGE_BYTES / fb) want = STAGE_BYTES / fb;
        r = st_need(p->host, p->rd, &p->st, fb);
        if (r == MAKTABASAUTI_EOF) { r = p->total_known ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_EOF; break; }          /* dane urwane przed zadeklarowana dlugoscia */
        if (r != MAKTABASAUTI_OK) break;
        avail_f = st_avail(&p->st) / fb;
        if (want > avail_f) want = avail_f;
        for (k = 0; k < want; k++) {
            const uint8_t *s = p->st.buf + p->st.at + k * fb;
            for (i = 0; i < p->channels; i++) o[(done + k) * p->channels + i] = sample_value(p, s + (size_t)i * p->width);
        }
        p->st.at += want * fb; p->pos += want; done += want; r = MAKTABASAUTI_OK;
    }
    *frames = done;
    if (done) { if (r == MAKTABASAUTI_E_TRUNCATED) p->pend = r; return MAKTABASAUTI_OK; }       /* blad po wydanych ramkach: najpierw ramki, blad przy nastepnym wywolaniu */
    if (r == MAKTABASAUTI_E_TRUNCATED) p->pend = r;
    return r;
}
static maktabasauti_result_t pcm_goto(pcm_t *p, uint64_t frame) {
    const uint64_t off = p->data_off + frame * p->channels * p->width;
    if (p->host->seek(p->host->user, p->rd, (int64_t)off, 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;
    st_clear(&p->st); p->pos = frame; p->pend = MAKTABASAUTI_OK;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL pcm_seek(void *state, uint64_t frame, uint32_t flags, uint64_t *actual) {
    pcm_t *p = (pcm_t *)state; maktabasauti_result_t r; (void)flags;
    if (!p->seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;
    if (p->total_known && frame > p->total) return MAKTABASAUTI_E_OUT_OF_RANGE;
    r = pcm_goto(p, frame);
    if (r == MAKTABASAUTI_OK) *actual = frame;
    return r;
}
static maktabasauti_result_t MAKTABASAUTI_CALL pcm_reset(void *state) { pcm_t *p = (pcm_t *)state; return p->seekable ? pcm_goto(p, 0) : MAKTABASAUTI_E_NOT_SEEKABLE; }
static maktabasauti_result_t MAKTABASAUTI_CALL pcm_flush(void *state) { ((pcm_t *)state)->pend = MAKTABASAUTI_OK; return MAKTABASAUTI_OK; }
static const char *MAKTABASAUTI_CALL pcm_last_error(const void *state) { (void)state; return NULL; }
static void MAKTABASAUTI_CALL pcm_close(void *state) { pcm_t *p = (pcm_t *)state; if (p) p->host->free(p->host->user, p); }

/* po udanym skanie: ustawia wejscie na poczatku danych i buduje stan */
static maktabasauti_result_t pcm_finish(scan_t *s, const maktabasauti_host_t *h, maktabasauti_reader_t *rd, pcm_t *proto, uint32_t rate, const char *container,
                                        maktabasauti_stream_info_t *info, void **state) {
    pcm_t *p; maktabasauti_result_t r;
    r = sc_move(s, proto->data_off);
    if (r != MAKTABASAUTI_OK) return r;
    p = (pcm_t *)h->alloc(h->user, sizeof *p);
    if (!p) return MAKTABASAUTI_E_NO_MEMORY;
    *p = *proto;
    p->host = h; p->rd = rd; p->seekable = s->seekable;
    fill_info(info, p, rate, container);
    *state = p;
    return MAKTABASAUTI_OK;
}

/* ====================================================================== WAV ====================================================================== */
static int32_t MAKTABASAUTI_CALL wav_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    (void)self; (void)hint;
    return (p && n >= 12u && !memcmp(p, "RIFF", 4) && !memcmp(p + 8, "WAVE", 4)) ? 50 : 0;
}
static maktabasauti_result_t MAKTABASAUTI_CALL wav_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *h, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    scan_t s; pcm_t proto; uint8_t hd[12], ch[8], body[40] = { 0 }; maktabasauti_result_t r; uint64_t riff_end, off = 12, data_sz = 0, size_in = 0; unsigned chunks = 0;
    uint32_t riff_sz, rate = 0, align = 0; unsigned format = 0, channels = 0, bits = 0; int fmt_seen = 0, data_seen = 0, placeholder;
    (void)self; *state = NULL;
    r = sc_begin(&s, h, rd);
    if (r != MAKTABASAUTI_OK) return r;
    if (s.pre_n < sizeof hd || memcmp(s.pre, "RIFF", 4) != 0 || memcmp(s.pre + 8, "WAVE", 4) != 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    memcpy(hd, s.pre, sizeof hd);
    riff_sz = le32(hd + 4);
    placeholder = riff_sz == 0u || riff_sz == 0xffffffffu;                                       /* zapis strumieniowy: rozmiar RIFF nieznany */
    riff_end = placeholder ? UINT64_MAX : (uint64_t)riff_sz + 8u;
    if (!placeholder && riff_end < 12u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    memset(&proto, 0, sizeof proto);
    while (off <= riff_end && riff_end - off >= 8u && chunks++ < MAX_CHUNKS) {
        uint32_t sz; uint64_t start, step;
        r = sc_fetch(&s, off, ch, sizeof ch);
        if (r != MAKTABASAUTI_OK) return r;
        sz = le32(ch + 4); start = off + 8u;
        if (!placeholder && (uint64_t)sz > riff_end - start) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        if (is_tag(ch, "fmt ")) {
            if (sz < 16u || fmt_seen) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            r = sc_fetch(&s, start, body, sz >= 40u ? 40u : 16u);
            if (r != MAKTABASAUTI_OK) return r;
            format = le16(body); channels = le16(body + 2); rate = le32(body + 4); align = le16(body + 12); bits = le16(body + 14);
            if (format == 0xfffeu) {
                static const uint8_t guid_tail[12] = { 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71 };
                if (sz < 40u || le16(body + 16) < 22u || memcmp(body + 28, guid_tail, 12) != 0 || le16(body + 18) == 0u || le16(body + 18) > bits || body[26] != 0 || body[27] != 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
                format = le16(body + 24);
            }
            fmt_seen = 1;
        } else if (is_tag(ch, "data") && !data_seen) {
            proto.data_off = start; data_sz = sz; data_seen = 1;
        }
        if (fmt_seen && data_seen) break;
        if (data_seen && !fmt_seen && !s.seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;           /* fmt za danymi: trzeba wrocic */
        if (sz == 0xffffffffu && placeholder) break;                                             /* chunk bez konca: nic za nim */
        step = (uint64_t)sz + (sz & 1u);
        if (!placeholder && step > riff_end - start) break;
        off = start + step;
    }
    if (!fmt_seen || !data_seen) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if (!rate || channels == 0u || channels > 32u || !((format == 1u && (bits == 8u || bits == 16u || bits == 24u || bits == 32u)) || (format == 3u && (bits == 32u || bits == 64u)) || ((format == 6u || format == 7u) && bits == 8u)) ||
        align != channels * (bits / 8u)) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    proto.channels = channels; proto.bits = bits; proto.width = bits / 8u; proto.kind = format == 3u ? 1 : format == 6u ? 5 : format == 7u ? 4 : 0;   /* WAVE_FORMAT_ALAW = 6, WAVE_FORMAT_MULAW = 7 */
    if (data_sz == 0xffffffffu || (placeholder && data_sz == 0u)) {                              /* dlugosc = reszta wejscia */
        const int64_t sz_in = h->size ? h->size(h->user, rd) : -1;
        if (sz_in >= 0 && (uint64_t)sz_in >= proto.data_off) { size_in = (uint64_t)sz_in - proto.data_off; proto.total = size_in / align; proto.total_known = 1; }
        else { proto.total = 0; proto.total_known = 0; }
    } else {
        if (data_sz % align) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        proto.total = data_sz / align; proto.total_known = 1;
    }
    return pcm_finish(&s, h, rd, &proto, rate, "wav", info, state);
}

/* ====================================================================== AIFF ====================================================================== */
static uint32_t parse_rate(const uint8_t *p) {
    const unsigned exp = be16(p);
    uint64_t mantissa; double rate; uint32_t rounded;
    if (exp == 0 || exp >= 0x7fffu || (exp & 0x8000u)) return 0;
    mantissa = ((uint64_t)be32(p + 2) << 32) | be32(p + 6);
    if (!(mantissa & UINT64_C(0x8000000000000000))) return 0;
    rate = ldexp((double)mantissa, (int)exp - 16383 - 63);
    if (!isfinite(rate) || rate < 1000.0 || rate > 384000.0) return 0;
    rounded = (uint32_t)(rate + 0.5);
    return fabs(rate - (double)rounded) < 0.01 ? rounded : 0;
}
static int32_t MAKTABASAUTI_CALL aiff_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    (void)self; (void)hint;
    return (p && n >= 12u && is_tag(p, "FORM") && (is_tag(p + 8, "AIFF") || is_tag(p + 8, "AIFC"))) ? 50 : 0;
}
static maktabasauti_result_t MAKTABASAUTI_CALL aiff_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *h, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    scan_t s; pcm_t proto; uint8_t ch[8], body[22] = { 0 }; maktabasauti_result_t r; uint64_t end, off = 12, pcm_bytes = 0; unsigned chunks = 0;
    uint32_t form_size, frames = 0, rate = 0, channels = 0, bits = 0; int aifc, saw_comm = 0, saw_ssnd = 0, little = 0, law = 0;
    (void)self; *state = NULL;
    r = sc_begin(&s, h, rd);
    if (r != MAKTABASAUTI_OK) return r;
    if (s.pre_n < 12u || !is_tag(s.pre, "FORM") || !(is_tag(s.pre + 8, "AIFF") || is_tag(s.pre + 8, "AIFC"))) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    aifc = is_tag(s.pre + 8, "AIFC");
    form_size = be32(s.pre + 4);
    if (form_size < 4u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    end = 8u + (uint64_t)form_size;
    memset(&proto, 0, sizeof proto);
    while (end - off >= 8u && off + 8u <= end && chunks++ < MAX_CHUNKS) {
        uint32_t size; uint64_t body_at;
        r = sc_fetch(&s, off, ch, sizeof ch);
        if (r != MAKTABASAUTI_OK) return r;
        size = be32(ch + 4); off += 8u; body_at = off;
        if ((uint64_t)size > end - off) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        if (is_tag(ch, "COMM")) {
            const uint32_t need = aifc ? 22u : 18u;
            if (saw_comm++ || size < need) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            r = sc_fetch(&s, body_at, body, need);
            if (r != MAKTABASAUTI_OK) return r;
            channels = be16(body); frames = be32(body + 2); bits = be16(body + 6); rate = parse_rate(body + 8);
            if (aifc) {
                if (is_tag(body + 18, "sowt")) little = 1;
                else if (is_tag(body + 18, "ulaw") || is_tag(body + 18, "ULAW")) law = 4;               /* G.711 w AIFF-C: sampleSize 16 (rozmiar po dekodowaniu), w pliku 8 bit na probke */
                else if (is_tag(body + 18, "alaw") || is_tag(body + 18, "ALAW")) law = 5;
                else if (!is_tag(body + 18, "NONE")) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            }
        } else if (is_tag(ch, "SSND")) {
            uint32_t offset;
            if (saw_ssnd++ || size < 8u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            r = sc_fetch(&s, body_at, body, 8u);
            if (r != MAKTABASAUTI_OK) return r;
            offset = be32(body);
            if ((uint64_t)offset > (uint64_t)size - 8u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            proto.data_off = body_at + 8u + offset;
            pcm_bytes = (uint64_t)size - 8u - offset;
        }
        if (saw_comm && saw_ssnd) break;
        if (saw_ssnd && !saw_comm && !s.seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;            /* COMM za danymi: trzeba wrocic */
        off += size;
        if (size & 1u) { if (off >= end) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT; ++off; }
    }
    if (law && bits != 16u && bits != 8u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if (law) bits = 8u;                                                                         /* w pliku bajt na probke */
    if (!saw_comm || !saw_ssnd || !frames || !rate || channels < 1u || channels > 2u || (bits != 8u && bits != 16u && bits != 24u && bits != 32u)) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if ((uint64_t)frames > pcm_bytes / ((bits / 8u) * channels)) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    proto.channels = channels; proto.bits = bits; proto.width = bits / 8u; proto.kind = law ? law : little ? 3 : 2; proto.total = frames; proto.total_known = 1;
    return pcm_finish(&s, h, rd, &proto, rate, "aiff", info, state);
}

/* ======================================================================= AU ======================================================================= */
#define AU_HEADER_MAX (16u << 20)
static int32_t MAKTABASAUTI_CALL au_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    (void)self; (void)hint;
    return (p && n >= 24u && !memcmp(p, ".snd", 4)) ? 50 : 0;
}
static maktabasauti_result_t MAKTABASAUTI_CALL au_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *h, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    scan_t s; pcm_t proto; maktabasauti_result_t r; uint32_t off, size, enc, rate, channels, align; uint64_t size_in = 0;
    (void)self; *state = NULL;
    r = sc_begin(&s, h, rd);
    if (r != MAKTABASAUTI_OK) return r;
    if (s.pre_n < 24u || memcmp(s.pre, ".snd", 4) != 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    off = be32(s.pre + 4); size = be32(s.pre + 8); enc = be32(s.pre + 12); rate = be32(s.pre + 16); channels = be32(s.pre + 20);
    if (off < 24u || off > AU_HEADER_MAX || rate < 1u || rate > 768000u || channels < 1u || channels > 32u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    memset(&proto, 0, sizeof proto);
    switch (enc) {
    case 1: proto.kind = 4; proto.bits = 8u; break;                                             /* G.711 mu-law */
    case 27: proto.kind = 5; proto.bits = 8u; break;                                            /* G.711 A-law */
    case 2: proto.kind = 2; proto.bits = 8u; break;
    case 3: proto.kind = 2; proto.bits = 16u; break;
    case 4: proto.kind = 2; proto.bits = 24u; break;
    case 5: proto.kind = 2; proto.bits = 32u; break;
    default: return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;                                          /* float, ADPCM (G.721 / G.723), ... */
    }
    proto.channels = channels; proto.width = proto.bits / 8u; align = channels * proto.width; proto.data_off = off;
    if (size == 0xffffffffu) {                                                                  /* rozmiar nieznany (zapis strumieniowy): reszta wejscia */
        const int64_t sz_in = h->size ? h->size(h->user, rd) : -1;
        if (sz_in >= 0 && (uint64_t)sz_in >= off) { size_in = (uint64_t)sz_in - off; proto.total = size_in / align; proto.total_known = 1; }
        else { proto.total = 0; proto.total_known = 0; }
    } else { proto.total = size / align; proto.total_known = 1; }
    return pcm_finish(&s, h, rd, &proto, rate, "au", info, state);
}

const maktabasauti_codec_t maktabasauti_pcm_wav_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "wav-pcm", "RIFF/WAVE PCM (streaming)", "maktabasauti_pcm",
    MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0u, NULL,
    wav_probe, wav_open, pcm_read, pcm_seek, pcm_reset, pcm_flush, NULL, pcm_last_error, NULL, pcm_close, { NULL }, { NULL }
};
const maktabasauti_codec_t maktabasauti_pcm_aiff_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "aiff-pcm", "AIFF / AIFF-C PCM (streaming)", "maktabasauti_pcm",
    MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0u, NULL,
    aiff_probe, aiff_open, pcm_read, pcm_seek, pcm_reset, pcm_flush, NULL, pcm_last_error, NULL, pcm_close, { NULL }, { NULL }
};
const maktabasauti_codec_t maktabasauti_pcm_au_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "au-pcm", "Sun/NeXT AU: PCM, G.711 mu-law / A-law (streaming)", "maktabasauti_pcm",
    MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0u, NULL,
    au_probe, au_open, pcm_read, pcm_seek, pcm_reset, pcm_flush, NULL, pcm_last_error, NULL, pcm_close, { NULL }, { NULL }
};
