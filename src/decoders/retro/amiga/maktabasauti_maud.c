/* MaktabaSauti, modul maktabasauti_maud: STRUMIENIOWY dekoder Amiga IFF MAUD, napisany w czystym C.
 *
 * Opis Richarda Koerbera (autor multisnd.library, MacroSystem), 8 sierpnia 1997, MAUDINFO:
 * FORM MAUD, MHDR 32 bajty, MDAT to same ramki audio.  mhdr_Samples bywa smieciem (Samplitude),
 * wiec dlugosc bierze sie z rozmiaru MDAT.  Czestotliwosc = mhdr_RateSource / mhdr_RateDevide.
 * Reszta z dzielenia nie jest calkowitym Hz: odmawiamy, zamiast zaokraglac.  Sufit 384 kHz jest
 * ten sam co w 8SVX.
 *
 * MCOMP_NONE: bity MSB jako pierwsze, bez dopelniania do bajtu.  Dokladnie 8 bitow jest bez znaku.
 * Kazda inna szerokosc od 1 do 32 jest ze znakiem, dopelnienie do dwoch, big-endian.  Ramka to
 * po jednej probce z kazdego kanalu: lewy, prawy, potem kolejne.  Wiecej niz 32 kanaly: odmowa,
 * bo tyle przyjmuje sciezka PCM WAV w tej bibliotece.
 *
 * MCI_MONO (0) wymaga 1 kanalu, MCI_STEREO (1) dwoch.  MCI_MULTIMONO (2) to co najmniej 2.
 * MCI_MULTISTEREO (3) to parzysta liczba od 4.  Sprzecznosc naglowka jest FORMAT.
 * MCI_MULTICHANNEL (4) wymaga MINF, a autor oznacza MINF jako przyszle i nie podaje ukladu: odmowa.
 *
 * MCOMP_ALAW (2) i MCOMP_ULAW (3): publiczny G.711, ten sam wzor co w maktabasauti_pcm.c
 * (ITU-T, implementacja referencyjna Sun, domena publiczna).  W pliku 8 bitow na kod.
 * Wyjscie f32 = wartosc liniowa / 32768.  Inna szerokosc kodu: odmowa.
 *
 * ID 11 (nieoficjalne, program ma2s Lorence'a Lombardo z Aminet): 8 bitow ZE ZNAKIEM zamiast bez znaku; autor opisuje to jednym zdaniem ("8bit IFF-MAUD signed ... compression ID 11"), wiec inna szerokosc: odmowa.
 * ID 9 (float, ten sam autor, aw2m) nie jest obslugiwane: ani szerokosc, ani kolejnosc bajtow, ani rodzaj (IEEE / FFP) nie sa nigdzie podane, a program nie dal sie uruchomic, zeby je zmierzyc: odmowa.
 *
 * MCOMP_FIBDELTA, ADPCM 2..5 i LONGDAT: autor pisze, ze biblioteka ich nie obsluguje i nie zna
 * programu, ktory je zapisuje.  Uklad bitow nie jest podany poza nazwa (FIBDELTA tylko "jak 8SVX",
 * a przyklad upakowania bitow dotyczy braku kompresji).  Odmawiamy.
 * Petli nie ma w tym opisie.  SoundFX mowi tylko, ze petla w przod istnieje, bez chunka.
 * ANNO, AUTH, (c) i NAME sa pomijane.
 *
 * PCM 8 bit: (v-128)/128.  PCM 16 bit: v/32768.  Wejscie przeplatane, wiec stereo gra bez przewijania.
 * Plik urwany: cale ramki sprzed urwania, potem lepki E_TRUNCATED. */
#include "maktabasauti/maktabasauti_codec.h"
#include <string.h>

#define STAGE_BYTES 4096u
#define PREFIX_BYTES 512u
#define MAX_CHUNKS 256u
#define MAX_CHANNELS 32u
#define MHDR_BYTES 32u

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
static uint16_t be16(const uint8_t *p) { return (uint16_t)(((unsigned)p[0] << 8) | p[1]); }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static int is_tag(const uint8_t *p, const char *tag) { return memcmp(p, tag, 4) == 0; }

typedef struct {
    const maktabasauti_host_t *h; maktabasauti_reader_t *rd;
    uint8_t pre[PREFIX_BYTES]; size_t pre_n;
    uint64_t pos;
    int seekable;
} scan_t;
static maktabasauti_result_t sc_move(scan_t *s, uint64_t off) {
    if (s->pos == off) return MAKTABASAUTI_OK;
    if (s->seekable) {
        const int64_t r = s->h->seek(s->h->user, s->rd, (int64_t)off, 0);
        if (r < 0) return r == MAKTABASAUTI_E_OUT_OF_RANGE ? MAKTABASAUTI_E_UNSUPPORTED_FORMAT : (maktabasauti_result_t)r;
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
        if (m == 0) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        o += m; len -= (size_t)m; s->pos += (uint64_t)m;
    }
    return MAKTABASAUTI_OK;
}

/* G.711: ten sam wzor co g711_ulaw / g711_alaw w maktabasauti_pcm.c. */
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

typedef struct {
    const maktabasauti_host_t *host; maktabasauti_reader_t *rd;
    stage_t st;
    uint32_t channels, bits, comp, bpf;
    uint64_t base, total, pos, next_byte;
    int bitcount;
    uint64_t bitbuf;
    int seekable, tail, total_known;
    maktabasauti_result_t pend;
} maud_t;

static maktabasauti_result_t pull_byte(maud_t *d, uint8_t *b) {
    maktabasauti_result_t r;
    if (st_avail(&d->st) == 0) {
        r = st_need(d->host, d->rd, &d->st, 1u);
        if (r != MAKTABASAUTI_OK) return r == MAKTABASAUTI_EOF ? MAKTABASAUTI_E_TRUNCATED : r;
    }
    *b = d->st.buf[d->st.at++];
    d->next_byte++;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t take_bits(maud_t *d, unsigned n, uint32_t *out) {
    while (d->bitcount < (int)n) {
        uint8_t b; maktabasauti_result_t r = pull_byte(d, &b);
        if (r != MAKTABASAUTI_OK) return r;
        d->bitbuf = (d->bitbuf << 8) | b;
        d->bitcount += 8;
    }
    d->bitcount -= (int)n;
    if (n == 32u) *out = (uint32_t)(d->bitbuf >> d->bitcount);
    else *out = (uint32_t)((d->bitbuf >> d->bitcount) & ((1ull << n) - 1ull));
    if (d->bitcount == 0) d->bitbuf = 0;
    else d->bitbuf &= (1ull << d->bitcount) - 1ull;
    return MAKTABASAUTI_OK;
}
static float scale_sample(uint32_t raw, unsigned bits, unsigned comp) {
    if (comp == 11u) return (float)(int8_t)raw * (1.0f / 128.0f);                                 /* 8 bitow ze znakiem (nieoficjalne ID 11, ma2s) */
    if (comp == 2u) return (float)g711_alaw((uint8_t)raw) * (1.0f / 32768.0f);
    if (comp == 3u) return (float)g711_ulaw((uint8_t)raw) * (1.0f / 32768.0f);
    if (bits == 8u) return (float)((int)raw - 128) * (1.0f / 128.0f);
    if (bits == 16u) return (float)(int16_t)raw * (1.0f / 32768.0f);
    if (bits == 32u) return (float)((double)(int32_t)raw * (1.0 / 2147483648.0));
    {
        const uint32_t sign = 1u << (bits - 1u);
        int32_t s = (int32_t)(raw & (sign - 1u));
        if (raw & sign) s -= (int32_t)sign;
        return (float)((double)s / (double)sign);
    }
}
static maktabasauti_result_t maud_frame(maud_t *d, float *out) {
    unsigned c;
    for (c = 0; c < d->channels; c++) {
        uint32_t raw; maktabasauti_result_t r = take_bits(d, d->bits, &raw);
        if (r != MAKTABASAUTI_OK) return r;
        out[c] = scale_sample(raw, d->bits, d->comp);
    }
    d->pos++;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL maud_read(void *state, void *dst, size_t n, size_t *frames) {
    maud_t *d = (maud_t *)state; float *o = (float *)dst; size_t done = 0; maktabasauti_result_t r = MAKTABASAUTI_OK;
    *frames = 0;
    if (d->pend != MAKTABASAUTI_OK) return d->pend;
    while (done < n) {
        if (d->pos >= d->total) { r = d->tail ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_EOF; break; }
        r = maud_frame(d, o + done * d->channels);
        if (r != MAKTABASAUTI_OK) break;
        done++;
    }
    *frames = done;
    if (r == MAKTABASAUTI_E_TRUNCATED) d->pend = r;
    if (done) return MAKTABASAUTI_OK;
    return r == MAKTABASAUTI_OK ? MAKTABASAUTI_EOF : r;
}
static maktabasauti_result_t maud_goto(maud_t *d, uint64_t frame) {
    const uint64_t bit = frame * (uint64_t)d->bpf;
    const uint64_t byte = bit >> 3;
    const unsigned skip = (unsigned)(bit & 7u);
    if (d->host->seek(d->host->user, d->rd, (int64_t)(d->base + byte), 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;
    st_clear(&d->st);
    d->next_byte = d->base + byte;
    d->bitcount = 0;
    d->bitbuf = 0;
    d->pos = frame;
    d->pend = MAKTABASAUTI_OK;
    if (skip) {
        uint32_t junk;
        const maktabasauti_result_t r = take_bits(d, skip, &junk);
        if (r != MAKTABASAUTI_OK) return r;
        (void)junk;
    }
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL maud_seek(void *state, uint64_t frame, uint32_t flags, uint64_t *actual) {
    maud_t *d = (maud_t *)state; (void)flags;
    if (!d->seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;
    if (frame > d->total) return MAKTABASAUTI_E_OUT_OF_RANGE;
    if (frame == d->total) { d->pos = frame; d->pend = MAKTABASAUTI_OK; *actual = frame; return MAKTABASAUTI_OK; }
    {
        const maktabasauti_result_t r = maud_goto(d, frame);
        if (r == MAKTABASAUTI_OK) *actual = frame;
        return r;
    }
}
static maktabasauti_result_t MAKTABASAUTI_CALL maud_reset(void *state) {
    maud_t *d = (maud_t *)state;
    return d->seekable ? maud_goto(d, 0) : MAKTABASAUTI_E_NOT_SEEKABLE;
}
static maktabasauti_result_t MAKTABASAUTI_CALL maud_flush(void *state) { ((maud_t *)state)->pend = MAKTABASAUTI_OK; return MAKTABASAUTI_OK; }
static const char *MAKTABASAUTI_CALL maud_last_error(const void *state) { (void)state; return NULL; }
static void MAKTABASAUTI_CALL maud_close(void *state) {
    maud_t *d = (maud_t *)state;
    if (!d) return;
    d->host->free(d->host->user, d);
}

static int32_t MAKTABASAUTI_CALL maud_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    (void)self; (void)hint;
    return (p && n >= 12u && is_tag(p, "FORM") && is_tag(p + 8, "MAUD")) ? 60 : 0;
}

static maktabasauti_result_t channels_of(uint32_t info, uint32_t ch) {
    if (ch == 0u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if (ch > MAX_CHANNELS) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    switch (info) {
    case 0u: return ch == 1u ? MAKTABASAUTI_OK : MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    case 1u: return ch == 2u ? MAKTABASAUTI_OK : MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    case 2u: return ch >= 2u ? MAKTABASAUTI_OK : MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    case 3u: return (ch >= 4u && (ch & 1u) == 0u) ? MAKTABASAUTI_OK : MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    case 4u: return MAKTABASAUTI_E_UNSUPPORTED_FEATURE; /* MINF, przyszle */
    default: return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    }
}

static maktabasauti_result_t MAKTABASAUTI_CALL maud_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *h, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    scan_t s; uint8_t ck[8], mh[MHDR_BYTES]; maktabasauti_result_t r;
    uint64_t end, off = 12, body_off = 0, body_len = 0;
    uint32_t form_size, bits = 0, comp = 0, channels = 0, chinfo = 0, rate_src = 0, rate = 0;
    uint16_t rate_div = 0;
    int saw_mhdr = 0, saw_mdat = 0; unsigned chunks = 0; maud_t *d; int64_t size;
    (void)self; *state = NULL;
    memset(&s, 0, sizeof s);
    {   int64_t n; const uint32_t fl = h->input_flags ? h->input_flags(h->user, rd) : 0u;
        s.h = h; s.rd = rd; s.pos = 0; s.seekable = (fl & MAKTABASAUTI_INPUT_SEEKABLE) != 0;
        n = h->peek(h->user, rd, s.pre, sizeof s.pre);
        if (n < 0) return (maktabasauti_result_t)n;
        s.pre_n = (size_t)n; }
    if (s.pre_n < 12u || !is_tag(s.pre, "FORM") || !is_tag(s.pre + 8, "MAUD")) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    form_size = be32(s.pre + 4);
    if (form_size < 4u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    end = 8u + (uint64_t)form_size;
    size = h->size ? h->size(h->user, rd) : -1;
    while (off + 8u <= end && chunks++ < MAX_CHUNKS) {
        uint32_t csize; uint64_t at;
        r = sc_fetch(&s, off, ck, sizeof ck);
        if (r != MAKTABASAUTI_OK) { if (saw_mhdr && saw_mdat) break; return r; }
        csize = be32(ck + 4); off += 8u; at = off;
        if (is_tag(ck, "MHDR")) {
            if (saw_mhdr++ || csize < MHDR_BYTES) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            r = sc_fetch(&s, at, mh, sizeof mh);
            if (r != MAKTABASAUTI_OK) return r;
            bits = be16(mh + 4); rate_src = be32(mh + 8); rate_div = be16(mh + 12);
            chinfo = be16(mh + 14); channels = be16(mh + 16); comp = be16(mh + 18);
        } else if (is_tag(ck, "MDAT")) {
            if (saw_mdat++) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
            body_off = at; body_len = csize;
        }
        if (!s.seekable && saw_mhdr && saw_mdat) break;
        if (saw_mdat && !saw_mhdr && !s.seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;
        if ((uint64_t)csize > UINT64_MAX - off) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        off += csize; if (csize & 1u) ++off;
    }
    if (!saw_mhdr || !saw_mdat) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    r = channels_of(chinfo, channels);
    if (r != MAKTABASAUTI_OK) return r;
    if (comp == 0u) { if (bits < 1u || bits > 32u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT; }
    else if (comp == 2u || comp == 3u || comp == 11u) { if (bits != 8u) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE; }
    else return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    if (rate_div == 0u || rate_src == 0u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if (rate_src % rate_div) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    rate = rate_src / rate_div;
    if (!rate || rate > 384000u) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if (bits > UINT32_MAX / channels) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    d = (maud_t *)h->alloc(h->user, sizeof *d);
    if (!d) return MAKTABASAUTI_E_NO_MEMORY;
    memset(d, 0, sizeof *d);
    d->host = h; d->rd = rd; d->channels = channels; d->bits = bits; d->comp = comp;
    d->bpf = bits * channels; d->base = body_off; d->seekable = s.seekable;
    {
        uint64_t avail = body_len; int deficit = 0; uint64_t frames;
        if (size >= 0) {
            if ((uint64_t)size <= body_off) avail = 0;
            else if ((uint64_t)size - body_off < body_len) { avail = (uint64_t)size - body_off; deficit = 1; }
        }
        frames = d->bpf ? (avail * 8u) / d->bpf : 0;
        if (!frames) { h->free(h->user, d); return deficit ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_E_UNSUPPORTED_FORMAT; }
        d->total = frames; d->tail = deficit; d->total_known = !deficit;
    }
    r = sc_move(&s, d->base);
    if (r != MAKTABASAUTI_OK) { h->free(h->user, d); return r; }
    d->next_byte = d->base;
    memset(info, 0, sizeof *info);
    info->struct_size = sizeof *info; info->sample_rate = rate; info->channels = channels;
    info->channel_mask = channels == 1u ? MAKTABASAUTI_LAYOUT_MONO : channels == 2u && chinfo == 1u ? MAKTABASAUTI_LAYOUT_STEREO : 0u;
    info->native_format = MAKTABASAUTI_FORMAT_F32;
    info->bits_per_sample = comp && comp != 11u ? 16u : bits;
    info->total_frames = d->total_known || d->seekable ? d->total : MAKTABASAUTI_UNKNOWN_LENGTH;
    info->flags = (d->seekable ? MAKTABASAUTI_STREAM_SEEKABLE : 0u) | (comp && comp != 11u ? 0u : MAKTABASAUTI_STREAM_LOSSLESS);
    strncpy(info->codec_name, comp == 2u ? "g711-alaw" : comp == 3u ? "g711-ulaw" : "pcm", sizeof info->codec_name - 1u);
    strncpy(info->container_name, "iff", sizeof info->container_name - 1u);
    *state = d;
    return MAKTABASAUTI_OK;
}

const maktabasauti_codec_t maktabasauti_maud_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "amiga-maud",
    "Amiga IFF MAUD (PCM 1..32 bit, G.711 A-law/mu-law; streaming)", "maktabasauti_maud",
    MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0u, NULL,
    maud_probe, maud_open, maud_read, maud_seek, maud_reset, maud_flush, NULL, maud_last_error, NULL, maud_close, { NULL }, { NULL }
};
