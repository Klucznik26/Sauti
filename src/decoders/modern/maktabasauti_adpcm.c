/* MaktabaSauti, modul maktabasauti_adpcm: ADPCM w kontenerach WAV i AIFF-C, napisany od zera wedlug opisow formatow (Microsoft ADPCM = WAVE_FORMAT_ADPCM 0x0002, IMA / DVI ADPCM = 0x0011;
 * AIFF-C `ima4` is prepared in the code but refused: see adpcm_open); tablice (krok IMA, adaptacja MS, domyslne wspolczynniki MS z pliku) to dane z opisow formatow.
 * Plik jest czytany w calosci do pamieci (limit 256 MiB, jak moduly z dlugoscia znana dopiero po przejrzeniu danych); blok ADPCM jest dekodowany na zadanie (jeden blok w pamieci
 * podrecznej), seek jest dokladny.  Wyjscie f32 = probka / 32768.
 *   MS ADPCM : blok = na kanal predyktor (1 B), delta (s16), probka1 (s16), probka2 (s16), potem polbajty (wyzszy pierwszy; przy stereo wyzszy = lewy, nizszy = prawy); 2 pierwsze probki
 *              bloku to probka2, probka1; wspolczynniki z pliku.
 *   IMA WAV  : blok = na kanal naglowek 4 B (s16 predyktor, indeks kroku, 0), potem grupy po 4 B na kanal (8 probek, nizszy polbajt pierwszy); probka naglowka jest pierwsza probka bloku.
 *   IMA4 AIFF-C: pakiet 34 B na kanal (naglowek 16 bit: wyzsze 9 bitow predyktor, nizsze 7 indeks kroku; 32 B = 64 probek, nizszy polbajt pierwszy), kanaly kolejno pakiet po pakiecie.
 * WAVE_FORMAT_EXTENSIBLE z podformatem ADPCM jest obslugiwane.  Nieobslugiwane (E_UNSUPPORTED_FORMAT, wiec rejestr szuka dalej): inne formaty. */
#include "maktabasauti/maktabasauti_codec.h"
#include <stdint.h>
#include <string.h>

#define FILE_MAX ((size_t)256u << 20)
#define CH_MAX 8u

typedef struct {
    const maktabasauti_host_t *host;
    uint8_t *file; size_t file_n;
    const uint8_t *data; size_t data_n;
    int kind;                                                   /* 1 MS, 2 IMA WAV, 3 IMA4 */
    unsigned ch, align, spb; uint32_t rate;                     /* spb: samples per block and channel */
    int16_t coef[32][2]; unsigned ncoef;
    uint64_t total, pos;
    uint64_t cached_block; int cache_ok; int16_t *cache;       /* spb * ch samples of the block */
    maktabasauti_result_t pend;
} adpcm_t;

static const int16_t ima_step[89] = {
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,
    1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767 };
static const int8_t ima_index[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
static const int ms_adapt[16] = { 230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230 };

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return le16(p) | (le16(p + 2) << 16); }
static uint32_t be16(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }
static uint32_t be32(const uint8_t *p) { return (be16(p) << 16) | be16(p + 2); }
static int16_t clamp16(int v) { return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }

static int ima_nibble(int *pred, int *idx, unsigned nib, int qt) {
    const int step = ima_step[*idx];
    int diff = step >> 3;
    if (qt) diff = ((2 * (int)(nib & 7u) + 1) * step) >> 3;                         /* QuickTime IMA4: the sum before the shift (as the Apple decoder and FFmpeg) */
    else {
        if (nib & 1u) diff += step >> 2;
        if (nib & 2u) diff += step >> 1;
        if (nib & 4u) diff += step;
    }
    *pred = (nib & 8u) ? *pred - diff : *pred + diff;
    if (*pred > 32767) *pred = 32767; else if (*pred < -32768) *pred = -32768;
    *idx += ima_index[nib & 7u];
    if (*idx < 0) *idx = 0; else if (*idx > 88) *idx = 88;
    return *pred;
}
static void adpcm_free(adpcm_t *s) {
    const maktabasauti_host_t *h;
    if (!s) return;
    h = s->host;
    if (s->file) h->free(h->user, s->file);
    if (s->cache) h->free(h->user, s->cache);
    h->free(h->user, s);
}

/* decodes block `b` into s->cache (spb * ch, interleaved); returns the number of valid samples per channel (a short last block) */
static unsigned decode_block(adpcm_t *s, uint64_t b) {
    const unsigned ch = s->ch; const uint8_t *p; size_t len; unsigned n = 0, c, i;
    if (s->kind == 3) { p = s->data + b * 34u * ch; len = 34u * ch; }
    else { p = s->data + b * s->align; len = s->data_n - (size_t)(b * s->align); if (len > s->align) len = s->align; }
    if (s->kind == 1) {
        int c1[CH_MAX], c2[CH_MAX], delta[CH_MAX], s1[CH_MAX], s2[CH_MAX]; const uint8_t *q = p + 7u * ch;
        if (len < 7u * ch) return 0;
        for (c = 0; c < ch; c++) { const unsigned pi = p[c] < s->ncoef ? p[c] : 0u; c1[c] = s->coef[pi][0]; c2[c] = s->coef[pi][1]; }
        for (c = 0; c < ch; c++) delta[c] = (int16_t)le16(p + ch + 2u * c);
        for (c = 0; c < ch; c++) s1[c] = (int16_t)le16(p + 3u * ch + 2u * c);
        for (c = 0; c < ch; c++) s2[c] = (int16_t)le16(p + 5u * ch + 2u * c);
        for (c = 0; c < ch; c++) { s->cache[c] = (int16_t)s2[c]; s->cache[ch + c] = (int16_t)s1[c]; }
        n = 2u;
        { const size_t nib = (len - 7u * ch) * 2u; unsigned total = (unsigned)(nib / ch) + 2u; if (total > s->spb) total = s->spb;
          for (i = 2u; i < total; i++) for (c = 0; c < ch; c++) {
              const size_t k = (size_t)(i - 2u) * ch + c; const unsigned byte = q[k >> 1]; const unsigned nb = (k & 1u) ? (byte & 15u) : (byte >> 4);
              const int sn = (int)nb >= 8 ? (int)nb - 16 : (int)nb;
              int pred = (s1[c] * c1[c] + s2[c] * c2[c]) / 256 + sn * delta[c];
              int d = (ms_adapt[nb] * delta[c]) / 256;
              pred = clamp16(pred); if (d < 16) d = 16;
              s2[c] = s1[c]; s1[c] = pred; delta[c] = d;
              s->cache[(size_t)i * ch + c] = (int16_t)pred;
          }
          n = total; }
    } else if (s->kind == 2) {
        int pred[CH_MAX], idx[CH_MAX];
        if (len < 4u * ch) return 0;
        for (c = 0; c < ch; c++) { pred[c] = (int16_t)le16(p + 4u * c); idx[c] = p[4u * c + 2u] > 88 ? 88 : p[4u * c + 2u]; s->cache[c] = (int16_t)pred[c]; }
        { const size_t groups = (len - 4u * ch) / (4u * ch); n = 1u;
          for (size_t g = 0; g < groups && n + 8u <= s->spb + 0u + 7u; g++) for (c = 0; c < ch; c++) {
              const uint8_t *q = p + 4u * ch + (g * ch + c) * 4u;
              for (i = 0; i < 8u; i++) { const unsigned byte = q[i >> 1], nb = (i & 1u) ? (byte >> 4) : (byte & 15u); const int v = ima_nibble(&pred[c], &idx[c], nb, 0); const unsigned at = 1u + (unsigned)g * 8u + i; if (at < s->spb) s->cache[(size_t)at * ch + c] = (int16_t)v; }
          }
          n = 1u + (unsigned)groups * 8u; if (n > s->spb) n = s->spb; }
    } else {
        for (c = 0; c < ch; c++) {
            const uint8_t *q = p + 34u * c; const unsigned hdr = be16(q); int pred, idx = (int)(hdr & 0x7fu);
            pred = (int16_t)(hdr & 0xff80u); if (idx > 88) idx = 88;
            for (i = 0; i < 32u; i++) for (unsigned h2 = 0; h2 < 2u; h2++) {
                const unsigned byte = q[2u + i], nb = h2 ? (byte >> 4) : (byte & 15u);
                s->cache[(size_t)(2u * i + h2) * ch + c] = (int16_t)ima_nibble(&pred, &idx, nb, 0);
            }
        }
        n = 64u;
    }
    return n;
}

static int32_t MAKTABASAUTI_CALL adpcm_probe(
    const maktabasauti_codec_t *self,
    const uint8_t *p, size_t n, const char *hint)
{
    size_t off = 12u;
    (void)self;
    (void)hint;

    if (!p || n < 12u)
        return 0;

    if (memcmp(p, "RIFF", 4) ||
        memcmp(p + 8, "WAVE", 4))
        return 0;

    while (off <= n && n - off >= 8u) {
        const uint8_t *c = p + off;
        uint32_t sz = le32(c + 4);
        const uint8_t *b = c + 8;
        size_t available = n - off - 8u;

        if (!memcmp(c, "fmt ", 4)) {
            unsigned tag;

            if (sz < 16u || available < 16u)
                return 0;

            tag = le16(b);

            if (tag == 0xfffeu) {
                if (sz < 42u || available < 42u)
                    return 0;

                tag = le16(b + 24);
            }

            if (le16(b + 14) != 4u)
                return 0;

            return (tag == 2u || tag == 0x11u)
                ? 40 : 0;
        }

        if (!memcmp(c, "data", 4))
            return 0;

        if ((size_t)sz > available)
            return 0;

        off += 8u + (size_t)sz;

        if (sz & 1u) {
            if (off == n)
                return 0;
            ++off;
        }
    }

    return 0;
}

static maktabasauti_result_t MAKTABASAUTI_CALL adpcm_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *h, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    adpcm_t *s; size_t cap = 1u << 20, n = 0; uint8_t *buf; int wav; size_t off, end;
    (void)self; *state = NULL;
    buf = (uint8_t *)h->alloc(h->user, cap);
    if (!buf) return MAKTABASAUTI_E_NO_MEMORY;
    for (;;) {
        int64_t m;
        if (n == cap) { uint8_t *nb; if (cap >= FILE_MAX) { h->free(h->user, buf); return MAKTABASAUTI_E_UNSUPPORTED_FEATURE; } nb = (uint8_t *)h->alloc(h->user, cap * 2u); if (!nb) { h->free(h->user, buf); return MAKTABASAUTI_E_NO_MEMORY; } memcpy(nb, buf, n); h->free(h->user, buf); buf = nb; cap *= 2u; }
        m = h->read(h->user, rd, buf + n, cap - n);
        if (m < 0) { h->free(h->user, buf); return (maktabasauti_result_t)m; }
        if (m == 0) break;
        n += (size_t)m;
    }
    s = (adpcm_t *)h->alloc(h->user, sizeof *s);
    if (!s) { h->free(h->user, buf); return MAKTABASAUTI_E_NO_MEMORY; }
    memset(s, 0, sizeof *s); s->host = h; s->file = buf; s->file_n = n;
    wav = n >= 12u && !memcmp(buf, "RIFF", 4);
    if (n < 12u || (!wav && memcmp(buf, "FORM", 4))) { adpcm_free(s); return MAKTABASAUTI_E_UNSUPPORTED_FORMAT; }
    end = n; off = 12u;
    { int got_fmt = 0; unsigned frames = 0; uint32_t bits = 0;
      while (off + 8u <= end) {
          const uint8_t *c = buf + off; uint32_t sz = wav ? le32(c + 4) : be32(c + 4); const uint8_t *b = c + 8;
          if ((size_t)sz > end - off - 8u) sz = (uint32_t)(end - off - 8u);
          if (wav && !memcmp(c, "fmt ", 4) && sz >= 16u) {
              unsigned tag = le16(b); unsigned coef_at = 20u, spb_at = 18u; s->ch = le16(b + 2); s->rate = le32(b + 4); s->align = le16(b + 12); bits = le16(b + 14);
              if (tag == 0xfffeu && sz >= 42u) { tag = le16(b + 24); spb_at = 40u; coef_at = 42u; }                              /* WAVE_FORMAT_EXTENSIBLE: the sub-format tag is the first word of the GUID */
              if (tag == 2u && sz >= coef_at + 2u && bits == 4u) {
                  s->spb = le16(b + spb_at); s->ncoef = le16(b + coef_at);
                  if (s->ncoef < 1u || s->ncoef > 32u || sz < coef_at + 2u + 4u * s->ncoef) { adpcm_free(s); return MAKTABASAUTI_E_UNSUPPORTED_FORMAT; }
                  for (unsigned k = 0; k < s->ncoef; k++) { s->coef[k][0] = (int16_t)le16(b + coef_at + 2u + 4u * k); s->coef[k][1] = (int16_t)le16(b + coef_at + 4u + 4u * k); }
                  s->kind = 1; got_fmt = 1;
              } else if (tag == 0x11u && sz >= 20u && bits == 4u) { s->spb = le16(b + spb_at); s->kind = 2; got_fmt = 1; }
              else { adpcm_free(s); return MAKTABASAUTI_E_UNSUPPORTED_FORMAT; }
          } else if (wav && !memcmp(c, "data", 4)) { s->data = b; s->data_n = sz; break; }
          else if (!wav && !memcmp(c, "COMM", 4) && sz >= 22u) {
              s->ch = be16(b); frames = be32(b + 2);
              { const uint32_t e = be16(b + 8); const uint64_t mant = ((uint64_t)be32(b + 10) << 32) | be32(b + 14); const int ex = (int)(e & 0x7fffu) - 16383 - 63;
                s->rate = mant ? (uint32_t)(ex >= 0 ? mant << ex : mant >> -ex) : 0u; }
              { adpcm_free(s); return MAKTABASAUTI_E_UNSUPPORTED_FORMAT; }          /* AIFF-C ima4: the packets after the first differ from FFmpeg (the running predictor), not enabled until understood */
          } else if (!wav && !memcmp(c, "SSND", 4) && sz >= 8u) { const uint32_t o = be32(b); if ((size_t)o + 8u > sz) { adpcm_free(s); return MAKTABASAUTI_E_UNSUPPORTED_FORMAT; } s->data = b + 8u + o; s->data_n = sz - 8u - o; break; }
          off += 8u + (size_t)sz + (sz & 1u);
      }
      if (!got_fmt || !s->data || s->ch < 1u || s->ch > CH_MAX || s->rate < 1u || s->rate > 768000u || s->spb < 2u || s->spb > 65535u) { adpcm_free(s); return MAKTABASAUTI_E_UNSUPPORTED_FORMAT; }
      if (s->kind == 3) { s->total = (uint64_t)frames * 64u; if (s->total > (s->data_n / (34u * s->ch)) * 64u) s->total = (s->data_n / (34u * s->ch)) * 64u; }      /* COMM counts packets of 64 samples */
      else {
          uint64_t blocks, last;
          if (s->align < 4u * s->ch + 1u) { adpcm_free(s); return MAKTABASAUTI_E_UNSUPPORTED_FORMAT; }
          blocks = s->data_n / s->align; last = s->data_n % s->align;
          s->total = blocks * s->spb;
          if (last >= (s->kind == 1 ? 7u : 4u) * s->ch) {                          /* a short last block: its samples */
              const uint64_t extra = s->kind == 1 ? 2u + ((last - 7u * s->ch) * 2u) / s->ch : 1u + ((last - 4u * s->ch) / (4u * s->ch)) * 8u;
              s->total += extra > s->spb ? s->spb : extra;
          }
      }
      s->cache = (int16_t *)h->alloc(h->user, (size_t)s->spb * s->ch * sizeof(int16_t));
      if (!s->cache) { adpcm_free(s); return MAKTABASAUTI_E_NO_MEMORY; }
    }
    memset(info, 0, sizeof *info);
    info->struct_size = sizeof *info; info->sample_rate = s->rate; info->channels = s->ch;
    info->channel_mask = s->ch == 1u ? MAKTABASAUTI_LAYOUT_MONO : (s->ch == 2u ? MAKTABASAUTI_LAYOUT_STEREO : 0u);
    info->native_format = MAKTABASAUTI_FORMAT_F32; info->bits_per_sample = 16;
    info->total_frames = s->total;
    info->flags = MAKTABASAUTI_STREAM_SEEKABLE;
    strncpy(info->codec_name, s->kind == 1 ? "adpcm-ms" : "adpcm-ima", sizeof info->codec_name - 1u);
    strncpy(info->container_name, wav ? "wav" : "aiff", sizeof info->container_name - 1u);
    *state = s;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL adpcm_read(void *state, void *dst, size_t n, size_t *frames) {
    adpcm_t *s = (adpcm_t *)state; float *o = (float *)dst; size_t done = 0; const unsigned ch = s->ch;
    *frames = 0;
    while (done < n && s->pos < s->total) {
        const uint64_t b = s->pos / s->spb; const unsigned at = (unsigned)(s->pos % s->spb); size_t take; unsigned i;
        if (!s->cache_ok || s->cached_block != b) { (void)decode_block(s, b); s->cached_block = b; s->cache_ok = 1; }
        take = s->spb - at; if (take > n - done) take = n - done; if ((uint64_t)take > s->total - s->pos) take = (size_t)(s->total - s->pos);
        for (i = 0; i < take * ch; i++) o[done * ch + i] = (float)s->cache[(size_t)at * ch + i] * (1.0f / 32768.0f);
        s->pos += take; done += take;
    }
    *frames = done;
    return done ? MAKTABASAUTI_OK : MAKTABASAUTI_EOF;
}
static maktabasauti_result_t MAKTABASAUTI_CALL adpcm_seek(void *state, uint64_t frame, uint32_t flags, uint64_t *actual) {
    adpcm_t *s = (adpcm_t *)state; (void)flags;
    if (frame > s->total) return MAKTABASAUTI_E_OUT_OF_RANGE;
    s->pos = frame; *actual = frame; return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL adpcm_reset(void *state) { uint64_t a; return adpcm_seek(state, 0, 0, &a); }
static maktabasauti_result_t MAKTABASAUTI_CALL adpcm_flush(void *state) { (void)state; return MAKTABASAUTI_OK; }
static void MAKTABASAUTI_CALL adpcm_close(void *state) { adpcm_free((adpcm_t *)state); }

const maktabasauti_codec_t maktabasauti_adpcm_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "adpcm", "ADPCM (Microsoft, IMA / DVI, QuickTime IMA4) in WAV / AIFF-C", "maktabasauti_adpcm",
    MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0, NULL,
    adpcm_probe, adpcm_open, adpcm_read, adpcm_seek, adpcm_reset, adpcm_flush, NULL, NULL, NULL, adpcm_close, { NULL }, { NULL }
};
