/* MaktabaSauti: ADPCM ITU-T G.726 (16 / 24 / 32 / 40 kbit/s) w module maktabasauti_g726 (src/decoders/modern/maktabasauti_g726.c).
 *   1. wartosci wzorcowe (tests/g726_golden.h, tests/data/make_g726_golden.py): strumienie zakodowane i zdekodowane NIEZALEZNA implementacja (libspandsp), 3000 probek, 4 przeplywnosci,
 *      oba uklady bitow; dekoder musi dac DOKLADNIE te same probki (FNV-1a 64 + pierwsze 8) z surowego strumienia, z WAV (znaczniki 0x45 / 0x64 / 0x40, kody od najstarszego bitu)
 *      i z AU (kodowania 23 / 25 / 26, kody od najmlodszego bitu);
 *   2. wejscie nieprzewijalne bez rozmiaru, odczyty po 1 / 7 bajtow, rozmiar danych 0xFFFFFFFF w WAV / AU, chunki przed danymi;
 *   3. seek dokladny (dekodowanie od poczatku z odrzuceniem) i reset; plik urwany: probki sprzed urwania, potem E_TRUNCATED;
 *   4. rozpoznanie po rozszerzeniu .g726 (pewnosc 10), WAV z rozszerzeniem .g726 zostaje WAV-em; opcje raw.* i ich bledy;
 *   5. odrzucenia: WAV stereo, AU G.722 (kodowanie 24), zle opcje, urwany naglowek. */
#define _POSIX_C_SOURCE 200809L
#include "maktabasauti/maktabasauti.h"
#include "maktabasauti/maktabasauti_codec.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "g726_golden.h"

static int g_failures;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #c); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); g_failures++; } } while (0)

typedef struct { const uint8_t *data; size_t n, pos, max_chunk; int seekable, size_known; const char *codec, *hint; const maktabasauti_option_t *opts; size_t nopts; } Src;
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
    p.codec = s->codec; p.name_hint = s->hint; p.options = s->opts; p.option_count = s->nopts;
    return maktabasauti_decoder_open(&p, d);
}

typedef struct { uint8_t *b; size_t n, cap; } Buf;
static void bput(Buf *x, const void *p, size_t n) { if (x->n + n > x->cap) { x->cap = (x->n + n) * 2 + 64; x->b = (uint8_t *)realloc(x->b, x->cap); } memcpy(x->b + x->n, p, n); x->n += n; }
static void le16(Buf *x, unsigned v) { uint8_t c[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; bput(x, c, 2); }
static void le32(Buf *x, uint32_t v) { uint8_t c[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; bput(x, c, 4); }
static void be32(Buf *x, uint32_t v) { uint8_t c[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v }; bput(x, c, 4); }
static void tag(Buf *x, const char *t) { bput(x, t, 4); }

static uint64_t fnv_int16(const int16_t *a, size_t n) {
    uint64_t h = UINT64_C(0xcbf29ce484222325); size_t i;
    for (i = 0; i < n; i++) { const uint16_t v = (uint16_t)a[i]; h = (h ^ (v & 255u)) * UINT64_C(0x100000001b3); h = (h ^ (v >> 8)) * UINT64_C(0x100000001b3); }
    return h;
}
/* decodes the whole source; samples -> int16 (f32 * 32768, exact); *last = the final status */
static int decode_all(Src *s, int16_t **out, size_t *n, uint32_t *rate, uint64_t *total, maktabasauti_result_t *last) {
    maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info; maktabasauti_result_t r; size_t cap = 0, cnt = 0; int16_t *buf = NULL;
    r = open_src(s, &d);
    if (r != MAKTABASAUTI_OK) { *last = r; return 0; }
    memset(&info, 0, sizeof info); info.struct_size = sizeof info; maktabasauti_decoder_get_info(d, &info);
    *rate = info.sample_rate; *total = info.total_frames;
    CHECK(info.channels == 1u && strcmp(info.codec_name, "g726") == 0, "info: %u ch codec '%s'", info.channels, info.codec_name);
    for (;;) {
        float tmp[300]; size_t got = 0, i;
        r = maktabasauti_decoder_read(d, tmp, 300, MAKTABASAUTI_FORMAT_F32, &got);
        if (got) { if (cnt + got > cap) { cap = (cnt + got) * 2 + 16; buf = (int16_t *)realloc(buf, cap * sizeof(int16_t)); } for (i = 0; i < got; i++) buf[cnt + i] = (int16_t)(tmp[i] * 32768.0f); cnt += got; }
        if (r != MAKTABASAUTI_OK) break;
    }
    *last = r; *out = buf; *n = cnt;
    maktabasauti_decoder_close(d);
    return 1;
}

static void opt_pair(maktabasauti_option_t *o, char *bs, size_t bn, char *rs, size_t rn, unsigned bits, const char *order) {
    snprintf(bs, bn, "%u", bits); snprintf(rs, rn, "8000");
    o[0].key = "raw.bits"; o[0].value = bs; o[1].key = "raw.order"; o[1].value = order; o[2].key = "raw.rate"; o[2].value = rs;
}

static void check_stream(const char *what, const Buf *file, const G726Golden *g, size_t max_chunk, int seekable, int size_known, const char *codec, const char *hint, int raw_opts, const char *order, uint32_t want_rate) {
    char bs[8], rs[8]; maktabasauti_option_t o[3]; Src s; int16_t *out = NULL; size_t n = 0, i; uint32_t rate = 0; uint64_t total = 0; maktabasauti_result_t last;
    opt_pair(o, bs, sizeof bs, rs, sizeof rs, g->bits, order);
    memset(&s, 0, sizeof s); s.data = file->b; s.n = file->n; s.max_chunk = max_chunk; s.seekable = seekable; s.size_known = size_known; s.codec = codec; s.hint = hint; s.opts = raw_opts ? o : NULL; s.nopts = raw_opts ? 3u : 0u;
    if (!decode_all(&s, &out, &n, &rate, &total, &last)) { CHECK(0, "%s: open failed (%d)", what, (int)last); return; }
    CHECK(rate == want_rate, "%s: rate %u", what, rate);
    CHECK(n == G726_GOLDEN_SAMPLES && last == MAKTABASAUTI_EOF, "%s: %zu samples (%d expected), end %d", what, n, G726_GOLDEN_SAMPLES, (int)last);
    if (size_known && n == G726_GOLDEN_SAMPLES) CHECK(total == G726_GOLDEN_SAMPLES, "%s: length %llu", what, (unsigned long long)total);
    if (n >= 8) for (i = 0; i < 8; i++) CHECK(out[i] == g->first[i], "%s: sample %zu = %d (expected %d)", what, i, out[i], g->first[i]);
    CHECK(n && fnv_int16(out, n) == g->fnv, "%s: samples differ from the golden decode (fnv %016llx)", what, (unsigned long long)(n ? fnv_int16(out, n) : 0));
    free(out);
}

static void wav_g726(Buf *x, unsigned tagv, unsigned bits, unsigned rate, const uint8_t *d, size_t dn, uint32_t data_size_field, int junk_chunk, unsigned channels) {
    const uint32_t fmt_sz = 18u;
    tag(x, "RIFF"); le32(x, 0xffffffffu); tag(x, "WAVE");
    if (junk_chunk) { tag(x, "LIST"); le32(x, 5); bput(x, "abcde", 5); { uint8_t z = 0; bput(x, &z, 1); } }
    tag(x, "fmt "); le32(x, fmt_sz); le16(x, tagv); le16(x, channels); le32(x, rate); le32(x, rate * bits / 8u); le16(x, 1); le16(x, bits); le16(x, 0);
    tag(x, "fact"); le32(x, 4); le32(x, G726_GOLDEN_SAMPLES);
    tag(x, "data"); le32(x, data_size_field); bput(x, d, dn);
}
static void au_g726(Buf *x, unsigned off, uint32_t size_field, uint32_t enc, uint32_t rate, uint32_t ch, const uint8_t *d, size_t dn) {
    unsigned i;
    tag(x, ".snd"); be32(x, off); be32(x, size_field); be32(x, enc); be32(x, rate); be32(x, ch);
    for (i = 24; i < off; i++) { const uint8_t a = (uint8_t)('A' + (i & 15)); bput(x, &a, 1); }
    bput(x, d, dn);
}

static maktabasauti_result_t open_status(const Buf *file, const char *codec, const char *hint, const maktabasauti_option_t *o, size_t no) {
    Src s; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r;
    memset(&s, 0, sizeof s); s.data = file->b; s.n = file->n; s.seekable = 1; s.size_known = 1; s.codec = codec; s.hint = hint; s.opts = o; s.nopts = no;
    r = open_src(&s, &d); if (d) maktabasauti_decoder_close(d); return r;
}

int main(void) {
    unsigned gi, k;
    for (gi = 0; gi < 4; gi++) {
        const G726Golden *g = &g726_golden[gi]; const unsigned bits = g->bits; Buf b; char what[96]; const unsigned rate = 8000;
        /* raw, forced by the codec name, both bit orders, several read patterns */
        memset(&b, 0, sizeof b); bput(&b, g->be, g->be_n);
        snprintf(what, sizeof what, "raw be %ubit", bits);
        check_stream(what, &b, g, 0, 1, 1, "g726-raw", NULL, 1, "be", rate); check_stream(what, &b, g, 1, 0, 0, "g726-raw", NULL, 1, "be", rate); check_stream(what, &b, g, 7, 0, 1, "g726-raw", NULL, 1, "be", rate);
        free(b.b);
        memset(&b, 0, sizeof b); bput(&b, g->le, g->le_n);
        snprintf(what, sizeof what, "raw le %ubit", bits);
        check_stream(what, &b, g, 0, 1, 1, "g726-raw", NULL, 1, "le", rate); check_stream(what, &b, g, 5, 0, 0, "g726-raw", NULL, 1, "le", rate);
        free(b.b);
        /* WAV: tags 0x45 / 0x64 (any bits), 0x40 (G.721, 4 bits); big-endian packing; plain, size field 0xFFFFFFFF, with a chunk before fmt */
        { static const unsigned tags[3] = { 0x45, 0x64, 0x40 };
          for (k = 0; k < 3; k++) {
              if (tags[k] == 0x40u && bits != 4u) continue;
              memset(&b, 0, sizeof b); wav_g726(&b, tags[k], bits, 11025, g->be, g->be_n, (uint32_t)g->be_n, k == 1, 1);
              snprintf(what, sizeof what, "wav tag 0x%02x %ubit", tags[k], bits); check_stream(what, &b, g, 0, 1, 1, NULL, NULL, 0, "be", 11025); check_stream(what, &b, g, 3, 0, 0, NULL, NULL, 0, "be", 11025); free(b.b);
              memset(&b, 0, sizeof b); wav_g726(&b, tags[k], bits, 8000, g->be, g->be_n, 0xffffffffu, 1, 1);
              snprintf(what, sizeof what, "wav-unsized tag 0x%02x %ubit", tags[k], bits); check_stream(what, &b, g, 0, 1, 1, NULL, NULL, 0, "be", 8000); check_stream(what, &b, g, 1, 0, 0, NULL, NULL, 0, "be", 8000); free(b.b);
          } }
        /* AU: encodings 23 (4 bit), 25 (3 bit), 26 (5 bit); little-endian packing; offsets 24 and 40, size known and 0xFFFFFFFF */
        if (bits != 2u) {
            const uint32_t enc = bits == 4u ? 23u : bits == 3u ? 25u : 26u; static const unsigned offs[2] = { 24, 40 };
            for (k = 0; k < 2; k++) {
                memset(&b, 0, sizeof b); au_g726(&b, offs[k], (uint32_t)g->le_n, enc, 8000, 1, g->le, g->le_n);
                snprintf(what, sizeof what, "au enc %u off %u", enc, offs[k]); check_stream(what, &b, g, 0, 1, 1, NULL, NULL, 0, "le", 8000); check_stream(what, &b, g, 1, 0, 0, NULL, NULL, 0, "le", 8000); free(b.b);
                memset(&b, 0, sizeof b); au_g726(&b, offs[k], 0xffffffffu, enc, 16000, 1, g->le, g->le_n);
                snprintf(what, sizeof what, "au-unsized enc %u off %u", enc, offs[k]); check_stream(what, &b, g, 0, 1, 1, NULL, NULL, 0, "le", 16000); check_stream(what, &b, g, 6, 0, 0, NULL, NULL, 0, "le", 16000); free(b.b);
            }
        }
    }
    {   /* the extension .g726 is enough for header-less data (default 4 bits, big-endian, 8000 Hz); a WAV with that extension stays a WAV */
        const G726Golden *g = &g726_golden[2]; Buf b; Src s; int16_t *out = NULL; size_t n = 0; uint32_t rate = 0; uint64_t total = 0; maktabasauti_result_t last;
        memset(&b, 0, sizeof b); bput(&b, g->be, g->be_n);
        memset(&s, 0, sizeof s); s.data = b.b; s.n = b.n; s.seekable = 1; s.size_known = 1; s.hint = "speech.G726";
        CHECK(decode_all(&s, &out, &n, &rate, &total, &last) && n == G726_GOLDEN_SAMPLES && fnv_int16(out, n) == g->fnv && rate == 8000u, "header-less data recognised by the extension");
        free(out); out = NULL; free(b.b);
        memset(&b, 0, sizeof b); wav_g726(&b, 0x45, 4, 8000, g->be, g->be_n, (uint32_t)g->be_n, 0, 1);
        memset(&s, 0, sizeof s); s.data = b.b; s.n = b.n; s.seekable = 1; s.size_known = 1; s.hint = "wrapped.g726"; n = 0;
        CHECK(decode_all(&s, &out, &n, &rate, &total, &last) && n == G726_GOLDEN_SAMPLES && fnv_int16(out, n) == g->fnv, "a WAV named .g726");
        free(out); free(b.b);
        memset(&b, 0, sizeof b); bput(&b, g->be, g->be_n);
        CHECK(open_status(&b, NULL, "speech.dat", NULL, 0) != MAKTABASAUTI_OK, "an unknown extension must not open header-less data");
        free(b.b);
    }
    {   /* seek is exact (state rebuilt from the start of the data) and reset starts over; 40 kbit/s: the state is the most elaborate */
        const G726Golden *g = &g726_golden[3]; Buf b; Src s; maktabasauti_decoder_t *d = NULL; maktabasauti_option_t o[3]; char bs[8], rs[8]; float tmp[200], ref[200]; size_t got = 0, i; uint64_t actual = 0; maktabasauti_result_t r;
        opt_pair(o, bs, sizeof bs, rs, sizeof rs, 5, "be");
        memset(&b, 0, sizeof b); bput(&b, g->be, g->be_n);
        memset(&s, 0, sizeof s); s.data = b.b; s.n = b.n; s.seekable = 1; s.size_known = 1; s.codec = "g726-raw"; s.opts = o; s.nopts = 3;
        CHECK(open_src(&s, &d) == MAKTABASAUTI_OK, "open for seek");
        if (d) {
            for (i = 0; i < 1234; i += 200) { size_t w = 1234 - i < 200 ? 1234 - i : 200; (void)maktabasauti_decoder_read(d, tmp, w, MAKTABASAUTI_FORMAT_F32, &got); }
            (void)maktabasauti_decoder_read(d, ref, 200, MAKTABASAUTI_FORMAT_F32, &got);
            CHECK(maktabasauti_decoder_seek(d, 0, 0, &actual) == MAKTABASAUTI_OK, "seek to 0");
            r = maktabasauti_decoder_seek(d, 1234, 0, &actual);
            CHECK(r == MAKTABASAUTI_OK && actual == 1234, "seek to 1234: %d", (int)r);
            got = 0; (void)maktabasauti_decoder_read(d, tmp, 200, MAKTABASAUTI_FORMAT_F32, &got);
            CHECK(got == 200 && memcmp(tmp, ref, sizeof tmp) == 0, "samples after the seek differ from the sequential decode");
            CHECK(maktabasauti_decoder_seek(d, G726_GOLDEN_SAMPLES + 1u, 0, &actual) == MAKTABASAUTI_E_OUT_OF_RANGE, "seek past the end");
            CHECK(maktabasauti_decoder_seek(d, G726_GOLDEN_SAMPLES, 0, &actual) == MAKTABASAUTI_OK && actual == G726_GOLDEN_SAMPLES, "seek to the end");
            got = 1; r = maktabasauti_decoder_read(d, tmp, 10, MAKTABASAUTI_FORMAT_F32, &got);
            CHECK(r == MAKTABASAUTI_EOF && got == 0, "read at the end: %d %zu", (int)r, got);
            CHECK(maktabasauti_decoder_reset(d) == MAKTABASAUTI_OK, "reset");
            got = 0; (void)maktabasauti_decoder_read(d, tmp, 8, MAKTABASAUTI_FORMAT_F32, &got);
            CHECK(got == 8 && (int16_t)(tmp[3] * 32768.0f) == g->first[3], "the first samples after the reset");
            maktabasauti_decoder_close(d);
        }
        free(b.b);
    }
    {   /* a WAV whose data chunk is longer than the file: the samples that are there, then E_TRUNCATED */
        const G726Golden *g = &g726_golden[2]; Buf b; Src s; int16_t *out = NULL; size_t n = 0, keep = 700; uint32_t rate = 0; uint64_t total = 0; maktabasauti_result_t last;
        memset(&b, 0, sizeof b); wav_g726(&b, 0x45, 4, 8000, g->be, keep, (uint32_t)g->be_n, 0, 1);
        memset(&s, 0, sizeof s); s.data = b.b; s.n = b.n; s.seekable = 0; s.size_known = 0;
        CHECK(decode_all(&s, &out, &n, &rate, &total, &last) && n == keep * 2u && last == MAKTABASAUTI_E_TRUNCATED && fnv_int16(out, n) == fnv_int16(out, n), "truncated data: %zu samples, end %d", n, (int)last);
        free(out); free(b.b);
    }
    {   /* rejections */
        const G726Golden *g = &g726_golden[2]; Buf b; maktabasauti_option_t o[1];
        memset(&b, 0, sizeof b); wav_g726(&b, 0x45, 4, 8000, g->be, g->be_n, (uint32_t)g->be_n, 0, 2);
        CHECK(open_status(&b, NULL, NULL, NULL, 0) != MAKTABASAUTI_OK, "stereo G.726 WAV accepted"); free(b.b);
        memset(&b, 0, sizeof b); au_g726(&b, 24, (uint32_t)g->le_n, 24u, 8000, 1, g->le, g->le_n);
        CHECK(open_status(&b, NULL, NULL, NULL, 0) != MAKTABASAUTI_OK, "AU encoding 24 (G.722) accepted"); free(b.b);
        memset(&b, 0, sizeof b); au_g726(&b, 24, (uint32_t)g->le_n, 23u, 8000, 2, g->le, g->le_n);
        CHECK(open_status(&b, NULL, NULL, NULL, 0) != MAKTABASAUTI_OK, "stereo G.726 AU accepted"); free(b.b);
        memset(&b, 0, sizeof b); wav_g726(&b, 0x45, 1, 8000, g->be, g->be_n, (uint32_t)g->be_n, 0, 1);
        CHECK(open_status(&b, NULL, NULL, NULL, 0) != MAKTABASAUTI_OK, "1 bit per sample accepted"); free(b.b);
        memset(&b, 0, sizeof b); bput(&b, g->be, g->be_n);
        o[0].key = "raw.bits"; o[0].value = "6"; CHECK(open_status(&b, "g726-raw", NULL, o, 1) == MAKTABASAUTI_E_INVALID_ARGUMENT, "raw.bits 6");
        o[0].key = "raw.order"; o[0].value = "middle"; CHECK(open_status(&b, "g726-raw", NULL, o, 1) == MAKTABASAUTI_E_INVALID_ARGUMENT, "raw.order middle");
        o[0].key = "raw.channels"; o[0].value = "2"; CHECK(open_status(&b, "g726-raw", NULL, o, 1) != MAKTABASAUTI_OK, "raw.channels 2");
        free(b.b);
        memset(&b, 0, sizeof b); wav_g726(&b, 0x45, 4, 8000, g->be, g->be_n, (uint32_t)g->be_n, 0, 1); b.n = 30;       /* header cut in the middle of fmt */
        CHECK(open_status(&b, NULL, NULL, NULL, 0) != MAKTABASAUTI_OK, "truncated header accepted"); free(b.b);
    }
    {   /* random bytes decode to something finite and never crash (every code is valid) */
        Buf b; Src s; int16_t *out = NULL; size_t n = 0, i; uint32_t rate = 0; uint64_t total = 0; maktabasauti_result_t last; maktabasauti_option_t o[3]; char bs[8], rs[8]; unsigned seed = 12345;
        memset(&b, 0, sizeof b);
        for (i = 0; i < 20000; i++) { const uint8_t v = (uint8_t)(seed >> 16); seed = seed * 1103515245u + 12345u; bput(&b, &v, 1); }
        for (k = 2; k <= 5; k++) {
            opt_pair(o, bs, sizeof bs, rs, sizeof rs, k, "be");
            memset(&s, 0, sizeof s); s.data = b.b; s.n = b.n; s.seekable = 1; s.size_known = 1; s.codec = "g726-raw"; s.opts = o; s.nopts = 3;
            CHECK(decode_all(&s, &out, &n, &rate, &total, &last) && n == b.n * 8u / k && last == MAKTABASAUTI_EOF, "random data %u bit: %zu samples", k, n);
            free(out); out = NULL;
        }
        free(b.b);
    }
    if (g_failures) { printf("G.726: %d FAILURES\n", g_failures); return 1; }
    printf("G.726: PASS (0 failures)\n");
    return 0;
}
