/* MaktabaSauti: ITU-T G.711.1 w module maktabasauti_g7111 (src/decoders/modern/maktabasauti_g7111.c).
 *   1. wartosci wzorcowe (tests/g7111_golden.h, tests/data/make_g7111_golden.py) z programu referencyjnego ITU-T: strumien zakodowany koderem referencyjnym i probki dekodera referencyjnego;
 *      R1 i R2a (dolne pasmo, 8 kHz) musza byc IDENTYCZNE (FNV-1a 64 wszystkich probek), R2b i R3 (16 kHz, MDCT gornego pasma w podwojnej precyzji) maja SNR >= 55 dB wzgledem wzorca albo
 *      roznice nie wieksze niz 2;
 *   2. ten sam strumien w formacie ITU-T G.192 (slowa 16 bitowe, bity 0x007f / 0x0081, najmlodszy bit bajtu pierwszy) rozpoznawany bez podpowiedzi daje DOKLADNIE te same probki co surowy;
 *   3. wejscie nieprzewijalne bez rozmiaru, odczyty po 1 / 7 bajtow, seek dokladny i reset, plik urwany w srodku ramki (ramki sprzed urwania, potem E_TRUNCATED);
 *   4. rozpoznanie po rozszerzeniu .g7111 (pewnosc 10), opcje g7111.mode / g7111.law i ich bledy, ramka G.192 skasowana (cisza), losowe bajty pod ASan / UBSan. */
#define _POSIX_C_SOURCE 200809L
#include "maktabasauti/maktabasauti.h"
#include "maktabasauti/maktabasauti_codec.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "g7111_golden.h"

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

static uint64_t fnv_int16(const int16_t *a, size_t n) {
    uint64_t h = UINT64_C(0xcbf29ce484222325); size_t i;
    for (i = 0; i < n; i++) { const uint16_t v = (uint16_t)a[i]; h = (h ^ (v & 255u)) * UINT64_C(0x100000001b3); h = (h ^ (v >> 8)) * UINT64_C(0x100000001b3); }
    return h;
}
static int decode_all(Src *s, int16_t **out, size_t *n, uint32_t *rate, uint64_t *total, maktabasauti_result_t *last) {
    maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info; maktabasauti_result_t r; size_t cap = 0, cnt = 0; int16_t *buf = NULL;
    r = open_src(s, &d);
    if (r != MAKTABASAUTI_OK) { *last = r; return 0; }
    memset(&info, 0, sizeof info); info.struct_size = sizeof info; maktabasauti_decoder_get_info(d, &info);
    *rate = info.sample_rate; *total = info.total_frames;
    CHECK(info.channels == 1u && strcmp(info.codec_name, "g711.1") == 0, "info: %u ch codec '%s'", info.channels, info.codec_name);
    for (;;) {
        float tmp[300]; size_t got = 0, i;
        r = maktabasauti_decoder_read(d, tmp, 300, MAKTABASAUTI_FORMAT_F32, &got);
        if (got) { if (cnt + got > cap) { cap = (cnt + got) * 2 + 16; buf = (int16_t *)realloc(buf, cap * sizeof(int16_t)); } for (i = 0; i < got; i++) buf[cnt + i] = (int16_t)lrintf(tmp[i] * 32768.0f); cnt += got; }
        if (r != MAKTABASAUTI_OK) break;
    }
    *last = r; *out = buf; *n = cnt;
    maktabasauti_decoder_close(d);
    return 1;
}
static void opts_for(maktabasauti_option_t *o, const G7111Golden *g, char *ms) {
    snprintf(ms, 8, "%d", g->mode);
    o[0].key = "g7111.mode"; o[0].value = ms; o[1].key = "g7111.law"; o[1].value = g->alaw ? "a" : "u";
}
/* R1 / R2a: identical; R2b / R3: SNR >= 55 dB or a difference of at most 2 */
static void compare(const char *what, const G7111Golden *g, const int16_t *out, size_t n, const uint64_t *own_fnv) {
    size_t i; double sig = 0, err = 0; int maxd = 0;
    CHECK(n == g->samples, "%s: %zu samples (%zu expected)", what, n, g->samples);
    if (n != g->samples) return;
    if (g->mode >= 3) for (i = 0; i < 80; i++) CHECK(out[i] == 0 && g->ref[i] == 0, "%s: the first frame after a reset is silent: sample %zu = %d (reference %d)", what, i, out[i], g->ref[i]);
    if (!g->ref) { CHECK(fnv_int16(out, n) == g->fnv, "%s: samples differ from the reference decoder (fnv %016llx)", what, (unsigned long long)fnv_int16(out, n)); return; }
    for (i = 0; i < n; i++) { const int d = abs((int)out[i] - g->ref[i]); if (d > maxd) maxd = d; sig += (double)g->ref[i] * g->ref[i]; err += (double)d * d; }
    CHECK(maxd <= 2 || (err > 0 && 10.0 * log10(sig / err) >= 55.0), "%s: SNR %.1f dB, max diff %d", what, err > 0 ? 10.0 * log10(sig / err) : 999.0, maxd);
    if (own_fnv) CHECK(fnv_int16(out, n) == *own_fnv, "%s: the second decode differs from the first", what);
}
static void check_stream(const char *what, const G7111Golden *g, size_t max_chunk, int seekable, int size_known, const char *hint, int use_opts) {
    maktabasauti_option_t o[2]; char ms[8]; Src s; int16_t *out = NULL; size_t n = 0; uint32_t rate = 0; uint64_t total = 0; maktabasauti_result_t last;
    opts_for(o, g, ms);
    memset(&s, 0, sizeof s); s.data = g->bits; s.n = g->bits_n; s.max_chunk = max_chunk; s.seekable = seekable; s.size_known = size_known; s.codec = hint ? NULL : "g7111-raw"; s.hint = hint; s.opts = use_opts ? o : NULL; s.nopts = use_opts ? 2u : 0u;
    if (!decode_all(&s, &out, &n, &rate, &total, &last)) { CHECK(0, "%s: open failed (%d)", what, (int)last); return; }
    CHECK(rate == (g->mode >= 3 ? 16000u : 8000u) && last == MAKTABASAUTI_EOF, "%s: rate %u end %d", what, rate, (int)last);
    if (size_known) CHECK(total == g->samples, "%s: length %llu", what, (unsigned long long)total);
    compare(what, g, out, n, NULL);
    free(out);
}
/* the same stream as ITU-T G.192: a sync word, the number of bits and every bit as 0x007f / 0x0081; the bits of a byte go in from the least significant one */
static void to_g192(const G7111Golden *g, Buf *x, int erase_frame) {
    const unsigned fb = g->mode == 1 ? 40u : g->mode == 4 ? 60u : 50u; size_t f, i;
    for (f = 0; f * fb < g->bits_n; f++) {
        const uint8_t head[4] = { (uint8_t)(f == (size_t)erase_frame ? 0x20 : 0x21), 0x6b, (uint8_t)((fb * 8u) & 255u), (uint8_t)((fb * 8u) >> 8) };
        bput(x, head, 4);
        for (i = 0; i < fb * 8u; i++) { const uint16_t w = ((g->bits[f * fb + (i >> 3)] >> (i & 7u)) & 1u) ? 0x0081u : 0x007fu; const uint8_t b2[2] = { (uint8_t)w, (uint8_t)(w >> 8) }; bput(x, b2, 2); }
    }
}

int main(void) {
    unsigned gi, k;
    for (gi = 0; gi < G7111_GOLDEN_COUNT; gi++) {
        const G7111Golden *g = &g7111_golden[gi]; char what[96];
        snprintf(what, sizeof what, "raw mode %d %s", g->mode, g->alaw ? "A" : "u");
        check_stream(what, g, 0, 1, 1, NULL, 1); check_stream(what, g, 1, 0, 0, NULL, 1); check_stream(what, g, 7, 0, 1, NULL, 1);
        snprintf(what, sizeof what, "raw by extension mode %d %s", g->mode, g->alaw ? "A" : "u");
        check_stream(what, g, 0, 1, 1, "speech.g7111", 1);
        {   /* G.192: autodetected, the same samples as the raw stream */
            Buf x; maktabasauti_option_t o[2]; char ms[8]; Src s; int16_t *out = NULL, *raw = NULL; size_t n = 0, nr = 0; uint32_t rate = 0; uint64_t total = 0; maktabasauti_result_t last;
            opts_for(o, g, ms); memset(&x, 0, sizeof x); to_g192(g, &x, -1);
            memset(&s, 0, sizeof s); s.data = x.b; s.n = x.n; s.seekable = 1; s.size_known = 1; s.opts = o; s.nopts = 2u;
            CHECK(decode_all(&s, &out, &n, &rate, &total, &last) && last == MAKTABASAUTI_EOF && n == g->samples, "g192 mode %d: %zu samples, end %d", g->mode, n, (int)last);
            if (n == g->samples) compare("g192", g, out, n, NULL);
            s.data = g->bits; s.n = g->bits_n; s.codec = "g7111-raw"; s.pos = 0;
            if (decode_all(&s, &raw, &nr, &rate, &total, &last) && n == nr) CHECK(memcmp(out, raw, n * sizeof *out) == 0, "g192 mode %d differs from the raw decode", g->mode);
            free(out); free(raw); free(x.b);
        }
    }
    {   /* seek is exact (the state is rebuilt from the start) and reset starts over: R3, the most elaborate state */
        const G7111Golden *g = &g7111_golden[5]; Src s; maktabasauti_decoder_t *d = NULL; maktabasauti_option_t o[2]; char ms[8]; float tmp[200], ref[200]; size_t got = 0, i; uint64_t actual = 0; maktabasauti_result_t r;
        opts_for(o, g, ms);
        memset(&s, 0, sizeof s); s.data = g->bits; s.n = g->bits_n; s.seekable = 1; s.size_known = 1; s.codec = "g7111-raw"; s.opts = o; s.nopts = 2;
        CHECK(open_src(&s, &d) == MAKTABASAUTI_OK, "open for seek");
        if (d) {
            for (i = 0; i < 1234; i += 200) { size_t w = 1234 - i < 200 ? 1234 - i : 200; (void)maktabasauti_decoder_read(d, tmp, w, MAKTABASAUTI_FORMAT_F32, &got); }
            (void)maktabasauti_decoder_read(d, ref, 200, MAKTABASAUTI_FORMAT_F32, &got);
            CHECK(maktabasauti_decoder_seek(d, 0, 0, &actual) == MAKTABASAUTI_OK, "seek to 0");
            r = maktabasauti_decoder_seek(d, 1234, 0, &actual);
            CHECK(r == MAKTABASAUTI_OK && actual == 1234, "seek to 1234: %d", (int)r);
            got = 0; (void)maktabasauti_decoder_read(d, tmp, 200, MAKTABASAUTI_FORMAT_F32, &got);
            CHECK(got == 200 && memcmp(tmp, ref, sizeof tmp) == 0, "samples after the seek differ from the sequential decode");
            CHECK(maktabasauti_decoder_seek(d, g->samples + 1u, 0, &actual) == MAKTABASAUTI_E_OUT_OF_RANGE, "seek past the end");
            CHECK(maktabasauti_decoder_reset(d) == MAKTABASAUTI_OK, "reset");
            got = 0; (void)maktabasauti_decoder_read(d, tmp, 8, MAKTABASAUTI_FORMAT_F32, &got);
            CHECK(got == 8 && lrintf(tmp[5] * 32768.0f) == g->ref[5], "the first samples after the reset");
            maktabasauti_decoder_close(d);
        }
    }
    {   /* a stream cut inside a frame: the whole frames, then E_TRUNCATED */
        const G7111Golden *g = &g7111_golden[1]; Src s; int16_t *out = NULL; size_t n = 0; uint32_t rate = 0; uint64_t total = 0; maktabasauti_result_t last; maktabasauti_option_t o[2]; char ms[8];
        opts_for(o, g, ms);
        memset(&s, 0, sizeof s); s.data = g->bits; s.n = 40u * 7u + 13u; s.codec = "g7111-raw"; s.opts = o; s.nopts = 2;
        CHECK(decode_all(&s, &out, &n, &rate, &total, &last) && n == 40u * 7u && last == MAKTABASAUTI_E_TRUNCATED, "cut frame: %zu samples, end %d", n, (int)last);
        free(out);
    }
    {   /* G.192: an erased frame (sync 0x6b20) is silence, option errors, a mode that contradicts the frame length */
        const G7111Golden *g = &g7111_golden[1]; Buf x; Src s; int16_t *out = NULL; size_t n = 0, i; uint32_t rate = 0; uint64_t total = 0; maktabasauti_result_t last; maktabasauti_option_t o[2]; char ms[8];
        opts_for(o, g, ms); memset(&x, 0, sizeof x); to_g192(g, &x, 3);
        memset(&s, 0, sizeof s); s.data = x.b; s.n = x.n; s.seekable = 1; s.size_known = 1; s.opts = o; s.nopts = 2;
        CHECK(decode_all(&s, &out, &n, &rate, &total, &last) && n == g->samples, "erased frame stream");
        for (i = 3 * 40; i < 4 * 40 && n == g->samples; i++) CHECK(out[i] == 0, "erased frame sample %zu = %d", i, out[i]);
        free(out); out = NULL;
        o[0].key = "g7111.mode"; o[0].value = "4";
        memset(&s, 0, sizeof s); s.data = x.b; s.n = x.n; s.opts = o; s.nopts = 1;
        { maktabasauti_decoder_t *d = NULL; CHECK(open_src(&s, &d) != MAKTABASAUTI_OK, "mode R3 on 320 bit frames accepted"); if (d) maktabasauti_decoder_close(d); }
        free(x.b);
        {   Buf b; maktabasauti_option_t bo[1]; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r;
            memset(&b, 0, sizeof b); bput(&b, g->bits, g->bits_n); memset(&s, 0, sizeof s); s.data = b.b; s.n = b.n; s.codec = "g7111-raw"; s.opts = bo; s.nopts = 1;
            bo[0].key = "g7111.mode"; bo[0].value = "5"; CHECK(open_src(&s, &d) == MAKTABASAUTI_E_INVALID_ARGUMENT, "mode 5"); if (d) maktabasauti_decoder_close(d);
            bo[0].key = "g7111.law"; bo[0].value = "x"; d = NULL; s.pos = 0; r = open_src(&s, &d); CHECK(r == MAKTABASAUTI_E_INVALID_ARGUMENT, "law x: %d", (int)r); if (d) maktabasauti_decoder_close(d);
            s.codec = NULL; s.hint = "speech.dat"; s.opts = NULL; s.nopts = 0; s.pos = 0; d = NULL; CHECK(open_src(&s, &d) != MAKTABASAUTI_OK, "an unknown extension must not open header-less data"); if (d) maktabasauti_decoder_close(d);
            free(b.b); }
    }
    {   /* random bytes decode to finite samples in every mode and never crash (every bit pattern is a valid frame) */
        Buf b; Src s; int16_t *out = NULL; size_t n = 0, i; uint32_t rate = 0; uint64_t total = 0; maktabasauti_result_t last; maktabasauti_option_t o[2]; char ms[8]; unsigned seed = 7111;
        memset(&b, 0, sizeof b);
        for (i = 0; i < 30000; i++) { const uint8_t v = (uint8_t)(seed >> 16); seed = seed * 1103515245u + 12345u; bput(&b, &v, 1); }
        for (k = 1; k <= 4; k++) {
            const unsigned fb = k == 1 ? 40u : k == 4 ? 60u : 50u; G7111Golden g; memset(&g, 0, sizeof g); g.mode = (int)k; g.alaw = (int)(k & 1u);
            opts_for(o, &g, ms);
            memset(&s, 0, sizeof s); s.data = b.b; s.n = b.n; s.seekable = 1; s.size_known = 1; s.codec = "g7111-raw"; s.opts = o; s.nopts = 2;
            CHECK(decode_all(&s, &out, &n, &rate, &total, &last) && n == (b.n / fb) * (k >= 3 ? 80u : 40u) && last == MAKTABASAUTI_EOF, "random data mode %u: %zu samples", k, n);
            free(out); out = NULL;
        }
        free(b.b);
    }
    if (g_failures) { printf("G.711.1: %d FAILURES\n", g_failures); return 1; }
    printf("G.711.1: PASS (0 failures)\n");
    return 0;
}
