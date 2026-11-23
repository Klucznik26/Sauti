/* MaktabaSauti: G.711 (mu-law / A-law) i Sun AU w module maktabasauti_pcm (src/decoders/modern/maktabasauti_pcm.c).
 *   1. wszystkie 256 bajtow mu-law i A-law dekodowane z KAZDEGO kontenera (WAV format 6 / 7, WAVE_FORMAT_EXTENSIBLE, AIFF-C ulaw / ULAW / alaw / ALAW, AU kodowanie 1 / 27) daja DOKLADNIE wartosci
 *      tablicy z FFmpeg (tests/g711_golden.h, wyrocznia niezalezna od testowanego kodu), podzielone przez 32768;
 *   2. stereo: kanaly w kolejnosci, dlugosc = ramki; AU z kodowaniem PCM 8 / 16 / 24 / 32 bit (big-endian, ze znakiem);
 *   3. wejscie nieprzewijalne bez rozmiaru, odczyty po 1 / 7 bajtow, AU z rozmiarem danych 0xFFFFFFFF = reszta wejscia, przesuniecie danych (adnotacja) w AU;
 *   4. seek dokladny i reset; plik urwany: ramki sprzed urwania, potem E_TRUNCATED;
 *   5. odrzucenia: AU z kodowaniem zmiennoprzecinkowym / ADPCM, zerowa liczba kanalow / czestotliwosc, przesuniecie < 24; WAV format 6 z 16 bitami; AIFF-C z nieznana kompresja; urwany naglowek. */
#define _POSIX_C_SOURCE 200809L
#include "maktabasauti/maktabasauti.h"
#include "maktabasauti/maktabasauti_codec.h"
#include "g711_golden.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #c); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); g_failures++; } } while (0)

typedef struct { const uint8_t *data; size_t n, pos, max_chunk; int seekable, size_known; } Src;
static int64_t MAKTABASAUTI_CALL s_read(void *u, void *buf, size_t size) {
    Src *s = (Src *)u; size_t give = s->n - s->pos;
    if (s->max_chunk && size > s->max_chunk) size = s->max_chunk;
    if (give > size) give = size;
    if (give) memcpy(buf, s->data + s->pos, give);
    s->pos += give;
    return (int64_t)give;
}
static int64_t MAKTABASAUTI_CALL s_seek(void *u, int64_t off, int wh) {
    Src *s = (Src *)u; const int64_t base = wh == 0 ? 0 : wh == 1 ? (int64_t)s->pos : (int64_t)s->n;
    if (base + off < 0 || (uint64_t)(base + off) > (uint64_t)s->n) return MAKTABASAUTI_E_OUT_OF_RANGE;
    s->pos = (size_t)(base + off); return base + off;
}
static int64_t MAKTABASAUTI_CALL s_size(void *u) { return (int64_t)((Src *)u)->n; }
static void MAKTABASAUTI_CALL s_close(void *u) { (void)u; }
static maktabasauti_result_t open_src(Src *s, maktabasauti_decoder_t **d) {
    maktabasauti_open_params_t p;
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION;
    p.io.struct_size = sizeof p.io; p.io.user = s; p.io.read = s_read; p.io.close = s_close; p.io.seek = s->seekable ? s_seek : NULL; p.io.size = s->size_known ? s_size : NULL;
    return maktabasauti_decoder_open(&p, d);
}

typedef struct { uint8_t *b; size_t n, cap; } Buf;
static void bput(Buf *x, const void *p, size_t n) { if (x->n + n > x->cap) { x->cap = (x->n + n) * 2 + 64; x->b = (uint8_t *)realloc(x->b, x->cap); } memcpy(x->b + x->n, p, n); x->n += n; }
static void le16(Buf *x, unsigned v) { uint8_t c[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; bput(x, c, 2); }
static void le32(Buf *x, uint32_t v) { uint8_t c[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; bput(x, c, 4); }
static void be16(Buf *x, unsigned v) { uint8_t c[2] = { (uint8_t)(v >> 8), (uint8_t)v }; bput(x, c, 2); }
static void be32(Buf *x, uint32_t v) { uint8_t c[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v }; bput(x, c, 4); }
static void tag(Buf *x, const char *t) { bput(x, t, 4); }

/* payload: `frames` frames, `ch` channels; byte of frame f channel c = (f * 7 + c * 61 + 3) & 255 (every value turns up) */
static uint8_t pay(unsigned f, unsigned c) { return (uint8_t)(f * 7u + c * 61u + 3u); }
static void payload(Buf *x, unsigned frames, unsigned ch) { unsigned f, c; for (f = 0; f < frames; f++) for (c = 0; c < ch; c++) { const uint8_t v = pay(f, c); bput(x, &v, 1); } }

static void wav(Buf *x, unsigned format, int extensible, unsigned ch, unsigned rate, unsigned frames) {
    const uint32_t fmt_sz = extensible ? 40u : 18u, data = frames * ch;
    tag(x, "RIFF"); le32(x, 4u + 8u + fmt_sz + 8u + data + (data & 1u)); tag(x, "WAVE");
    tag(x, "fmt "); le32(x, fmt_sz); le16(x, extensible ? 0xfffeu : format); le16(x, ch); le32(x, rate); le32(x, rate * ch); le16(x, ch); le16(x, 8);
    if (extensible) { static const uint8_t g[14] = { 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71 }; le16(x, 22); le16(x, 8); le32(x, 0); le16(x, format); bput(x, g, 14); }   /* GUID: format, 0000, 0000, 0010, 8000 00aa00389b71 */
    else le16(x, 0);
    tag(x, "data"); le32(x, data); payload(x, frames, ch); if (data & 1u) { uint8_t z = 0; bput(x, &z, 1); }
}
static void aifc(Buf *x, const char *comp, unsigned ch, unsigned rate_ext_hi, unsigned frames) {
    static const uint8_t r8000[10] = { 0x40, 0x0b, 0xfa, 0, 0, 0, 0, 0, 0, 0 };
    const uint32_t data = frames * ch; (void)rate_ext_hi;
    tag(x, "FORM"); be32(x, 4u + 12u + 8u + 24u + 8u + 8u + data + (data & 1u)); tag(x, "AIFC");
    tag(x, "FVER"); be32(x, 4); be32(x, 0xa2805140u);
    tag(x, "COMM"); be32(x, 24u); be16(x, ch); be32(x, frames); be16(x, 16); bput(x, r8000, 10); tag(x, comp); { uint8_t pn[2] = { 1, 'x' }; bput(x, pn, 2); }
    tag(x, "SSND"); be32(x, 8u + data); be32(x, 0); be32(x, 0); payload(x, frames, ch); if (data & 1u) { uint8_t z = 0; bput(x, &z, 1); }
}
static void au(Buf *x, unsigned off, uint32_t size, uint32_t enc, uint32_t rate, uint32_t ch, unsigned frames, unsigned width) {
    unsigned f, c, k;
    tag(x, ".snd"); be32(x, off); be32(x, size); be32(x, enc); be32(x, rate); be32(x, ch);
    { unsigned i; for (i = 24; i < off; i++) { const uint8_t a = (uint8_t)('A' + (i & 15)); bput(x, &a, 1); } }
    for (f = 0; f < frames; f++) for (c = 0; c < ch; c++) { for (k = 0; k < width; k++) { const uint8_t v = width == 1 ? pay(f, c) : (uint8_t)(pay(f, c) + 17u * k); bput(x, &v, 1); } }
}

typedef struct { const int16_t *tab; } Law;
/* expected float of one sample; law 0 = none (PCM big-endian of `width` bytes) */
static float expect_law(const int16_t *tab, uint8_t v) { return (float)tab[v] / 32768.0f; }

static int read_all(Src *s, unsigned *ch, uint32_t *rate, uint64_t *total, float **out, size_t *nframes, maktabasauti_result_t *last) {
    maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info; maktabasauti_result_t r; size_t cap = 0, n = 0; float *buf = NULL;
    r = open_src(s, &d);
    if (r != MAKTABASAUTI_OK) { *last = r; return 0; }
    memset(&info, 0, sizeof info); info.struct_size = sizeof info; maktabasauti_decoder_get_info(d, &info);
    *ch = info.channels; *rate = info.sample_rate; *total = info.total_frames;
    for (;;) {
        float tmp[512]; size_t got = 0; const size_t want = 512u / info.channels;
        r = maktabasauti_decoder_read(d, tmp, want, MAKTABASAUTI_FORMAT_F32, &got);
        if (got) { if (n + got > cap) { cap = (n + got) * 2 + 16; buf = (float *)realloc(buf, cap * info.channels * sizeof(float)); } memcpy(buf + n * info.channels, tmp, got * info.channels * sizeof(float)); n += got; }
        if (r != MAKTABASAUTI_OK) break;
    }
    *last = r; *out = buf; *nframes = n;
    maktabasauti_decoder_close(d);
    return 1;
}

static void check_law(const char *what, const Buf *file, const int16_t *tab, unsigned ch, unsigned rate, unsigned frames, size_t max_chunk, int seekable, int size_known, const char *codec_hint) {
    Src s; unsigned c; uint32_t r_rate = 0; uint64_t total = 0; float *out = NULL; size_t n = 0, f; maktabasauti_result_t last; unsigned got_ch = 0;
    memset(&s, 0, sizeof s); s.data = file->b; s.n = file->n; s.max_chunk = max_chunk; s.seekable = seekable; s.size_known = size_known;
    if (!read_all(&s, &got_ch, &r_rate, &total, &out, &n, &last)) { CHECK(0, "%s: open failed (%d)", what, (int)last); return; }
    CHECK(got_ch == ch && r_rate == rate, "%s: %u ch %u Hz, expected %u ch %u Hz", what, got_ch, r_rate, ch, rate);
    CHECK(n == frames && last == MAKTABASAUTI_EOF, "%s: %zu frames (%u expected), end %d", what, n, frames, (int)last);
    if (size_known || seekable) CHECK(total == frames || total == MAKTABASAUTI_UNKNOWN_LENGTH, "%s: length %llu", what, (unsigned long long)total);
    for (f = 0; f < n && f < frames; f++) for (c = 0; c < ch; c++) if (out[f * ch + c] != expect_law(tab, pay((unsigned)f, c))) { CHECK(0, "%s: frame %zu ch %u: %g, expected %g", what, f, c, out[f * ch + c], expect_law(tab, pay((unsigned)f, c))); return; }
    free(out); (void)codec_hint;
}

int main(void) {
    Buf b; unsigned law, ch, fr; static const unsigned chs[2] = { 1, 2 };
    /* 0. the golden tables themselves: spot checks of the definition (mu-law 0xff = 0, A-law 0xd5 = +8 ..) */
    CHECK(g711_ulaw_golden[0xff] == 0 && g711_ulaw_golden[0x7f] == 0 && g711_ulaw_golden[0x00] == -32124 && g711_ulaw_golden[0x80] == 32124, "mu-law table");
    CHECK(g711_alaw_golden[0xd5] == 8 && g711_alaw_golden[0x55] == -8 && g711_alaw_golden[0xaa] == 32256 && g711_alaw_golden[0x2a] == -32256, "A-law table");
    for (law = 0; law < 2; law++) {
        const int16_t *tab = law ? g711_alaw_golden : g711_ulaw_golden; const char *nm = law ? "alaw" : "ulaw";
        for (ch = 0; ch < 2; ch++) for (fr = 0; fr < 2; fr++) {
            const unsigned c = chs[ch], frames = fr ? 700u : 256u; char what[96];
            /* WAV, format 6 (A-law) / 7 (mu-law), plain and EXTENSIBLE */
            memset(&b, 0, sizeof b); wav(&b, law ? 6u : 7u, 0, c, 8000, frames);
            snprintf(what, sizeof what, "wav %s %uch %uf", nm, c, frames); check_law(what, &b, tab, c, 8000, frames, 0, 1, 1, NULL); check_law(what, &b, tab, c, 8000, frames, 1, 0, 0, NULL); check_law(what, &b, tab, c, 8000, frames, 7, 0, 0, NULL); free(b.b);
            memset(&b, 0, sizeof b); wav(&b, law ? 6u : 7u, 1, c, 16000, frames);
            snprintf(what, sizeof what, "wav-ext %s %uch %uf", nm, c, frames); check_law(what, &b, tab, c, 16000, frames, 0, 1, 1, NULL); check_law(what, &b, tab, c, 16000, frames, 3, 0, 0, NULL); free(b.b);
            /* AIFF-C: ulaw / ULAW / alaw / ALAW */
            { static const char *const comps[2][2] = { { "ulaw", "ULAW" }, { "alaw", "ALAW" } }; unsigned k;
              for (k = 0; k < 2; k++) {
                  memset(&b, 0, sizeof b); aifc(&b, comps[law][k], c, 0, frames);
                  snprintf(what, sizeof what, "aifc %s %uch %uf", comps[law][k], c, frames); check_law(what, &b, tab, c, 8000, frames, 0, 1, 1, NULL); check_law(what, &b, tab, c, 8000, frames, 1, 0, 0, NULL); free(b.b);
              } }
            /* AU: encoding 1 (mu-law) / 27 (A-law), offsets 24 / 40 (annotation), size known and 0xFFFFFFFF */
            { static const unsigned offs[2] = { 24, 40 }; unsigned k;
              for (k = 0; k < 2; k++) {
                  memset(&b, 0, sizeof b); au(&b, offs[k], frames * c, law ? 27u : 1u, 8000, c, frames, 1);
                  snprintf(what, sizeof what, "au %s %uch %uf off %u", nm, c, frames, offs[k]); check_law(what, &b, tab, c, 8000, frames, 0, 1, 1, NULL); check_law(what, &b, tab, c, 8000, frames, 1, 0, 0, NULL); free(b.b);
                  memset(&b, 0, sizeof b); au(&b, offs[k], 0xffffffffu, law ? 27u : 1u, 8000, c, frames, 1);
                  snprintf(what, sizeof what, "au-unsized %s %uch %uf", nm, c, frames); check_law(what, &b, tab, c, 8000, frames, 0, 1, 1, NULL); check_law(what, &b, tab, c, 8000, frames, 5, 0, 0, NULL); free(b.b);
              } }
        }
    }
    /* 2. AU PCM 8 / 16 / 24 / 32 bit big-endian signed */
    { static const unsigned enc[4] = { 2, 3, 4, 5 }; unsigned k;
      for (k = 0; k < 4; k++) {
          Src s; uint32_t rate = 0; uint64_t total = 0; float *out = NULL; size_t n = 0, f; maktabasauti_result_t last; unsigned got_ch = 0; const unsigned w = k + 1u, frames = 40;
          memset(&b, 0, sizeof b); au(&b, 24, frames * 2u * w, enc[k], 22050, 2, frames, w);
          memset(&s, 0, sizeof s); s.data = b.b; s.n = b.n; s.seekable = 1; s.size_known = 1;
          if (!read_all(&s, &got_ch, &rate, &total, &out, &n, &last)) { CHECK(0, "au pcm %u: open failed", w); free(b.b); continue; }
          CHECK(got_ch == 2 && rate == 22050 && n == frames && last == MAKTABASAUTI_EOF, "au pcm %u: %u ch %u Hz %zu frames end %d", w, got_ch, rate, n, (int)last);
          for (f = 0; f < n; f++) { unsigned c; for (c = 0; c < 2; c++) {
              uint32_t raw = 0, bytes = 0; double sign = (double)(1ull << (8u * w - 1u)); int64_t v; unsigned j;
              for (j = 0; j < w; j++) { const uint8_t x = w == 1 ? pay((unsigned)f, c) : (uint8_t)(pay((unsigned)f, c) + 17u * j); raw = (raw << 8) | x; bytes++; }
              v = (raw & (uint32_t)sign) ? (int64_t)raw - (int64_t)(sign * 2.0) : (int64_t)raw; (void)bytes;
              if (out[f * 2 + c] != (float)((double)v / sign)) { CHECK(0, "au pcm %u: frame %zu ch %u: %g vs %g", w, f, c, out[f * 2 + c], (double)v / sign); f = n; break; }
          } }
          free(out); free(b.b);
      } }
    /* 3. seek and reset (AU mu-law stereo, 700 frames), truncation */
    { Src s; maktabasauti_decoder_t *d = NULL; float tmp[8]; size_t got = 0; uint64_t at = 0; maktabasauti_result_t r;
      memset(&b, 0, sizeof b); au(&b, 24, 700u * 2u, 1, 8000, 2, 700, 1);
      memset(&s, 0, sizeof s); s.data = b.b; s.n = b.n; s.seekable = 1; s.size_known = 1;
      CHECK(open_src(&s, &d) == MAKTABASAUTI_OK, "open");
      r = maktabasauti_decoder_seek(d, 345, 0, &at); CHECK(r == MAKTABASAUTI_OK && at == 345, "seek %d %llu", (int)r, (unsigned long long)at);
      r = maktabasauti_decoder_read(d, tmp, 4, MAKTABASAUTI_FORMAT_F32, &got); CHECK(r == MAKTABASAUTI_OK && got == 4 && tmp[0] == expect_law(g711_ulaw_golden, pay(345, 0)) && tmp[7] == expect_law(g711_ulaw_golden, pay(348, 1)), "after seek");
      r = maktabasauti_decoder_seek(d, 701, 0, &at); CHECK(r == MAKTABASAUTI_E_OUT_OF_RANGE, "seek past the end: %d", (int)r);
      r = maktabasauti_decoder_seek(d, 700, 0, &at); CHECK(r == MAKTABASAUTI_OK && at == 700, "seek to the end");
      r = maktabasauti_decoder_read(d, tmp, 4, MAKTABASAUTI_FORMAT_F32, &got); CHECK(r == MAKTABASAUTI_EOF && got == 0, "read at the end: %d", (int)r);
      r = maktabasauti_decoder_reset(d); CHECK(r == MAKTABASAUTI_OK, "reset");
      r = maktabasauti_decoder_read(d, tmp, 4, MAKTABASAUTI_FORMAT_F32, &got); CHECK(r == MAKTABASAUTI_OK && got == 4 && tmp[0] == expect_law(g711_ulaw_golden, pay(0, 0)), "after reset");
      maktabasauti_decoder_close(d);
      /* truncated at every length of the data: the frames that are there, then E_TRUNCATED */
      { size_t cut; for (cut = 24; cut < b.n; cut += 97) {
            Src t; uint32_t rate = 0; uint64_t total = 0; float *out = NULL; size_t n = 0; maktabasauti_result_t last; unsigned got_ch = 0; const size_t frames_there = (cut - 24u) / 2u;
            memset(&t, 0, sizeof t); t.data = b.b; t.n = cut; t.seekable = 1; t.size_known = 1;
            if (!read_all(&t, &got_ch, &rate, &total, &out, &n, &last)) { CHECK(0, "cut %zu: open failed %d", cut, (int)last); continue; }
            CHECK(n == frames_there && last == MAKTABASAUTI_E_TRUNCATED, "cut %zu: %zu frames (expected %zu), end %d", cut, n, frames_there, (int)last);
            free(out);
        } }
      free(b.b); }
    /* 4. rejections */
    { Src s; maktabasauti_decoder_t *d = NULL; unsigned k;
      struct { const char *what; unsigned off; uint32_t enc, rate, ch; } bad[] = {
          { "au float32 (6)", 24, 6, 8000, 1 }, { "au double (7)", 24, 7, 8000, 1 }, { "au G.721 ADPCM (23)", 24, 23, 8000, 1 }, { "au zero channels", 24, 1, 8000, 0 }, { "au zero rate", 24, 1, 0, 1 },
          { "au offset below 24", 20, 1, 8000, 1 }, { "au absurd offset", 0x7fffffffu, 1, 8000, 1 }, { "au 33 channels", 24, 1, 8000, 33 } };
      for (k = 0; k < sizeof bad / sizeof bad[0]; k++) {
          memset(&b, 0, sizeof b); au(&b, bad[k].off < 24 ? 24 : bad[k].off > 4096 ? 24 : bad[k].off, 64, bad[k].enc, bad[k].rate, bad[k].ch, 32, 1);
          if (bad[k].off < 24) { b.b[7] = (uint8_t)bad[k].off; } else if (bad[k].off > 4096) { b.b[4] = 0x7f; b.b[5] = b.b[6] = b.b[7] = 0xff; }
          memset(&s, 0, sizeof s); s.data = b.b; s.n = b.n; s.seekable = 1; s.size_known = 1; d = NULL;
          CHECK(open_src(&s, &d) != MAKTABASAUTI_OK && d == NULL, "%s must be rejected", bad[k].what);
          if (d) maktabasauti_decoder_close(d);
          free(b.b);
      }
      /* WAV A-law with 16 bits: not G.711 */
      memset(&b, 0, sizeof b); wav(&b, 6, 0, 1, 8000, 32); b.b[34] = 16;   /* wBitsPerSample */
      memset(&s, 0, sizeof s); s.data = b.b; s.n = b.n; s.seekable = 1; s.size_known = 1; d = NULL;
      CHECK(open_src(&s, &d) != MAKTABASAUTI_OK, "wav format 6 with 16 bits must be rejected"); if (d) maktabasauti_decoder_close(d); free(b.b);
      /* AIFF-C with an unknown compression */
      memset(&b, 0, sizeof b); aifc(&b, "ulaw", 1, 0, 32); memcpy(b.b + 12 + 12 + 8 + 18, "g722", 4);
      memset(&s, 0, sizeof s); s.data = b.b; s.n = b.n; s.seekable = 1; s.size_known = 1; d = NULL;
      CHECK(open_src(&s, &d) != MAKTABASAUTI_OK, "aifc g722 must be rejected"); if (d) maktabasauti_decoder_close(d); free(b.b);
      /* truncated headers: never opens, never crashes */
      memset(&b, 0, sizeof b); au(&b, 24, 64, 1, 8000, 1, 64, 1);
      { size_t cut; for (cut = 0; cut < 24; cut++) { memset(&s, 0, sizeof s); s.data = b.b; s.n = cut; s.seekable = 1; s.size_known = 1; d = NULL; { const maktabasauti_result_t r = open_src(&s, &d); CHECK(r != MAKTABASAUTI_OK, "au header cut at %zu opened", cut); if (d) maktabasauti_decoder_close(d); } } }
      free(b.b); }
    if (g_failures) { fprintf(stderr, "G.711 / AU: %d FAILURES\n", g_failures); return 1; }
    puts("G.711 / AU: PASS (0 failures)");
    return 0;
}
