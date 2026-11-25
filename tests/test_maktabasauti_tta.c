/* MaktabaSauti: The True Audio (TTA 1.x) w module maktabasauti_tta (src/decoders/modern/maktabasauti_tta.c).
 *   1. pliki z kodera FFmpeg `tta` z syntetycznych sygnalow (tests/data/tta/, tests/data/make_tta_fixtures.py): mono do 5.1, 8 / 16 / 24 bit, cisza, wartosci skrajne, szum, 1 probka,
 *      dokladnie jedna ramka, jedna ramka + 1 probka, 44,1 kHz (ramka 46080); wzorzec = dekoder FFmpeg, probki IDENTYCZNE (rozpoznanie przez rejestr, bez nazwy kodeka);
 *   2. odczyty po 1 / 7 / 4097 / 100000 ramek, seek dokladny (granice ramek, koniec, poza koniec), reset;
 *   3. wejscie NIEPRZEWIJALNE bez rozmiaru dekoduje cale (ramki leza kolejno), a seek daje E_NOT_SEEKABLE;
 *   4. znacznik ID3v2 przed plikiem, plik urwany (ramki sprzed urwania, potem E_TRUNCATED);
 *   5. uszkodzone dane (losowe bajty w calym pliku, takze w naglowku i tabeli): bez awarii pod ASan / UBSan, bez wyciekow, ograniczona pamiec.
 * Test czyta pliki wzgledem katalogu repozytorium. */
#define _POSIX_C_SOURCE 200809L
#include "maktabasauti/maktabasauti.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #c); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); g_failures++; } } while (0)

typedef struct { size_t live, peak; long bad; } Mem;
typedef struct { size_t n; size_t pad[3]; } Hdr;
static void *MAKTABASAUTI_CALL h_alloc(void *u, size_t n) { Mem *m = (Mem *)u; Hdr *h = (Hdr *)malloc(sizeof(Hdr) + n); if (!h) return NULL; h->n = n; m->live += n; if (m->live > m->peak) m->peak = m->live; return h + 1; }
static void MAKTABASAUTI_CALL h_free(void *u, void *p) { Mem *m = (Mem *)u; Hdr *h; if (!p) { m->bad++; return; } h = (Hdr *)p - 1; m->live -= h->n; free(h); }

typedef struct { const uint8_t *data; size_t n, pos, max_chunk; int seekable, size_known, closed; } Src;
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
static void MAKTABASAUTI_CALL s_close(void *u) { ((Src *)u)->closed++; }
static maktabasauti_result_t open_src(Src *s, Mem *m, maktabasauti_decoder_t **d) {
    maktabasauti_allocator_t a; maktabasauti_open_params_t p;
    memset(&a, 0, sizeof a); a.struct_size = sizeof a; a.user = m; a.alloc = h_alloc; a.free = h_free;
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION; p.allocator = &a;
    p.io.struct_size = sizeof p.io; p.io.user = s; p.io.read = s_read; p.io.close = s_close; p.io.seek = s->seekable ? s_seek : NULL; p.io.size = s->size_known ? s_size : NULL;
    return maktabasauti_decoder_open(&p, d);
}
static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); uint8_t *b; long sz;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    b = (uint8_t *)malloc((size_t)sz + 1u);
    if (b && fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); b = NULL; }
    fclose(f); *n = (size_t)sz; return b;
}
static uint32_t rng = 0x1128u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

typedef struct { const char *name; unsigned ch, bits, rate; } Fx;
static maktabasauti_result_t drain(maktabasauti_decoder_t *d, float *out, size_t cap_frames, unsigned ch, size_t chunk, size_t *got) {
    maktabasauti_result_t r; size_t cnt = 0;
    for (;;) {
        size_t g = 0, want = chunk; if (cnt + want > cap_frames) want = cap_frames - cnt;
        r = maktabasauti_decoder_read(d, out + cnt * ch, want, MAKTABASAUTI_FORMAT_F32, &g);
        cnt += g;
        if (r != MAKTABASAUTI_OK || cnt >= cap_frames) break;
    }
    *got = cnt; return r;
}
/* wartosc oczekiwana jak w FFmpeg: 8 bit = (u8 - 128) / 128, 16 bit = s16 / 32768, 24 bit = (wartosc * 256) / 2^31 */
static float expect(int32_t v, unsigned bits) { return bits <= 8u ? (float)v * (1.0f / 128.0f) : bits <= 16u ? (float)v * (1.0f / 32768.0f) : (float)v * (1.0f / 8388608.0f); }

static void run_decode(const Fx *x, const uint8_t *file, size_t fn, const int32_t *ref, size_t nref, int seekable, size_t chunk, size_t max_chunk, const char *what, uint64_t id3_skip) {
    Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info; size_t got = 0, i; maktabasauti_result_t r; float *out = (float *)malloc((nref + 64u) * sizeof(float));
    memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = file; s.n = fn; s.seekable = seekable; s.size_known = seekable; s.max_chunk = max_chunk;
    (void)id3_skip;
    r = open_src(&s, &m, &d);
    CHECK(r == MAKTABASAUTI_OK, "%s %s: open (%d)", x->name, what, (int)r);
    if (r != MAKTABASAUTI_OK) { free(out); return; }
    memset(&info, 0, sizeof info); info.struct_size = sizeof info; maktabasauti_decoder_get_info(d, &info);
    CHECK(info.channels == x->ch && info.sample_rate == x->rate && info.bits_per_sample == x->bits && info.total_frames == nref / x->ch && strcmp(info.codec_name, "tta") == 0,
          "%s %s: info %u ch %u Hz %u bit %llu frames codec %s", x->name, what, info.channels, info.sample_rate, info.bits_per_sample, (unsigned long long)info.total_frames, info.codec_name);
    r = drain(d, out, nref / x->ch + 16u, x->ch, chunk, &got);
    CHECK(r == MAKTABASAUTI_EOF && got == nref / x->ch, "%s %s chunk %zu: end %d, %zu frames vs %zu", x->name, what, chunk, (int)r, got, nref / x->ch);
    for (i = 0; i < got * x->ch && i < nref; i++) if (out[i] != expect(ref[i], x->bits)) { CHECK(0, "%s %s: sample %zu is %.9g, expected %.9g", x->name, what, i, (double)out[i], (double)expect(ref[i], x->bits)); break; }
    maktabasauti_decoder_close(d);
    CHECK(m.live == 0 && s.closed == 1, "%s %s: leak %zu, close %d", x->name, what, m.live, s.closed);
    free(out);
}
static void check_fixture(const Fx *x) {
    static const size_t chunks[] = { 5000, 1, 7, 4097, 100000 };
    char pa[128], pr[128]; size_t fn, rn, k, nref, w, i; uint8_t *file, *rb; int32_t *ref; const unsigned wide = x->bits > 16u;
    snprintf(pa, sizeof pa, "tests/data/tta/%s.tta", x->name); snprintf(pr, sizeof pr, "tests/data/tta/%s.ref", x->name);
    file = slurp(pa, &fn); rb = slurp(pr, &rn);
    w = wide ? 4u : 2u;
    CHECK(file && rb && rn % w == 0, "%s: fixture missing (run from the repository root)", x->name);
    if (!file || !rb || rn % w) { free(file); free(rb); return; }
    nref = rn / w; ref = (int32_t *)malloc(nref * sizeof(int32_t));
    for (i = 0; i < nref; i++) ref[i] = wide ? (int32_t)((uint32_t)rb[4 * i] | ((uint32_t)rb[4 * i + 1] << 8) | ((uint32_t)rb[4 * i + 2] << 16) | ((uint32_t)rb[4 * i + 3] << 24)) : (int32_t)(int16_t)(rb[2 * i] | (rb[2 * i + 1] << 8));
    free(rb);
    for (k = 0; k < sizeof chunks / sizeof chunks[0]; k++) run_decode(x, file, fn, ref, nref, 1, chunks[k], chunks[k] == 7u ? 3u : 0u, "seekable", 0);
    run_decode(x, file, fn, ref, nref, 0, 4097, 5u, "non-seekable", 0);                              /* ramki leza kolejno: wejscie bez seek i bez rozmiaru dziala */
    {                                                                                               /* seek dokladny, reset, poza koniec */
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r; float *out = (float *)malloc((nref + 64u) * sizeof(float));
        uint64_t total = nref / x->ch, act = 0; size_t got = 0; unsigned q;
        static const uint64_t fixed[] = { 0, 1, 8358, 8359, 8360, 16717, 16718, 46079, 46080, 46081 };
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = file; s.n = fn; s.seekable = 1; s.size_known = 1;
        r = open_src(&s, &m, &d); CHECK(r == MAKTABASAUTI_OK, "%s: open for seek", x->name);
        if (r == MAKTABASAUTI_OK) {
            for (q = 0; q < 14u + sizeof fixed / sizeof fixed[0]; q++) {
                const uint64_t t = q < sizeof fixed / sizeof fixed[0] ? fixed[q] : (rnd() % (total + 1u)); size_t want = 300, g2 = 0;
                if (t > total) continue;
                r = maktabasauti_decoder_seek(d, t, MAKTABASAUTI_SEEK_EXACT, &act);
                CHECK(r == MAKTABASAUTI_OK && act == t, "%s: seek %llu (%d)", x->name, (unsigned long long)t, (int)r);
                if (r != MAKTABASAUTI_OK) break;
                if (want > total - t) want = (size_t)(total - t);
                if (!want) { r = maktabasauti_decoder_read(d, out, 10, MAKTABASAUTI_FORMAT_F32, &g2); CHECK(r == MAKTABASAUTI_EOF && g2 == 0, "%s: read at the end (%d)", x->name, (int)r); continue; }
                r = maktabasauti_decoder_read(d, out, want, MAKTABASAUTI_FORMAT_F32, &g2);
                CHECK(r == MAKTABASAUTI_OK && g2 >= 1, "%s: read after seek %llu (%d)", x->name, (unsigned long long)t, (int)r);
                for (i = 0; i < g2 * x->ch; i++) if (out[i] != expect(ref[(size_t)t * x->ch + i], x->bits)) { CHECK(0, "%s: PCM after seek %llu differs at %zu", x->name, (unsigned long long)t, i); break; }
            }
            CHECK(maktabasauti_decoder_seek(d, total + 1u, MAKTABASAUTI_SEEK_EXACT, &act) == MAKTABASAUTI_E_OUT_OF_RANGE, "%s: seek past the end", x->name);
            CHECK(maktabasauti_decoder_reset(d) == MAKTABASAUTI_OK, "%s: reset", x->name);
            r = drain(d, out, nref / x->ch + 16u, x->ch, 5000, &got);
            CHECK(r == MAKTABASAUTI_EOF && got == nref / x->ch && out[0] == expect(ref[0], x->bits), "%s: decode after reset", x->name);
            maktabasauti_decoder_close(d);
        }
        CHECK(m.live == 0, "%s: leak after seek test", x->name);
        free(out);
    }
    {                                                                                               /* seek na wejsciu nieprzewijalnym */
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; uint64_t act;
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = file; s.n = fn;
        if (open_src(&s, &m, &d) == MAKTABASAUTI_OK) { CHECK(maktabasauti_decoder_seek(d, 5, MAKTABASAUTI_SEEK_EXACT, &act) == MAKTABASAUTI_E_NOT_SEEKABLE, "%s: seek on a non-seekable input", x->name); maktabasauti_decoder_close(d); }
        CHECK(m.live == 0, "%s: leak", x->name);
    }
    if (!strcmp(x->name, "s16") || !strcmp(x->name, "m8_noise")) {                                  /* znacznik ID3v2 przed plikiem i plik urwany */
        const size_t tag = 3000u; uint8_t *t = (uint8_t *)calloc(tag + fn, 1); size_t cut;
        memcpy(t, "ID3\x04\0\0", 6); t[6] = 0; t[7] = 0; t[8] = (uint8_t)(((tag - 10u) >> 7) & 0x7f); t[9] = (uint8_t)((tag - 10u) & 0x7f);
        memcpy(t + tag, file, fn);
        run_decode(x, t, tag + fn, ref, nref, 1, 5000, 0, "id3 seekable", tag);
        run_decode(x, t, tag + fn, ref, nref, 0, 5000, 11u, "id3 non-seekable", tag);
        free(t);
        for (cut = 0; cut < fn; cut += 1u + rnd() % 997u) {
            Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r; float *out = (float *)malloc((nref + 64u) * sizeof(float)); size_t got = 0;
            memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = file; s.n = cut; s.seekable = (int)(cut & 1u); s.size_known = s.seekable;
            r = open_src(&s, &m, &d);
            if (r == MAKTABASAUTI_OK) {
                r = drain(d, out, nref / x->ch + 16u, x->ch, 4096, &got);
                CHECK(r == MAKTABASAUTI_E_TRUNCATED && got < nref / x->ch, "%s cut %zu: end %d after %zu frames", x->name, cut, (int)r, got);
                for (i = 0; i < got * x->ch; i++) if (out[i] != expect(ref[i], x->bits)) { CHECK(0, "%s cut %zu: frame data before the cut differs at %zu", x->name, cut, i); break; }
                maktabasauti_decoder_close(d);
            } else CHECK(!d && s.closed == 0, "%s cut %zu: failed open must not own the input (%d)", x->name, cut, (int)r);
            CHECK(m.live == 0, "%s cut %zu: leak", x->name, cut);
            free(out);
        }
    }
    free(ref); free(file);
}
static void check_fuzz(const char *name, unsigned iters) {
    char pa[128]; size_t fn, it; uint8_t *file, *w; float *out = (float *)malloc(100000u * 16u * sizeof(float));
    snprintf(pa, sizeof pa, "tests/data/tta/%s.tta", name);
    file = slurp(pa, &fn);
    CHECK(file != NULL, "%s: missing", name);
    if (!file) { free(out); return; }
    w = (uint8_t *)malloc(fn);
    for (it = 0; it < iters; it++) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; size_t nmut = 1u + rnd() % 6u, q, got = 0, cut = fn; maktabasauti_result_t r;
        memcpy(w, file, fn);
        for (q = 0; q < nmut; q++) {
            const size_t pos = (rnd() & 1u) ? (size_t)(rnd() % fn) : (size_t)(rnd() % (fn < 200u ? fn : 200u));        /* cala plik, czesto poczatek (naglowek i tabela) */
            w[pos] = (uint8_t)(rnd() & 1u ? rnd() : w[pos] ^ (1u << (rnd() & 7u)));
        }
        if (it % 7u == 0) cut = (size_t)(rnd() % fn);
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = w; s.n = cut; s.seekable = (int)(it & 1u); s.size_known = s.seekable;
        r = open_src(&s, &m, &d);
        if (r == MAKTABASAUTI_OK) {
            r = drain(d, out, 100000u, 1u, 4096, &got);
            (void)r; (void)maktabasauti_decoder_flush(d);
            { uint64_t act; (void)maktabasauti_decoder_seek(d, rnd() % 50000u, MAKTABASAUTI_SEEK_EXACT, &act); }
            (void)maktabasauti_decoder_read(d, out, 500, MAKTABASAUTI_FORMAT_F32, &got);
            maktabasauti_decoder_close(d);
        }
        CHECK(m.live == 0 && m.bad == 0, "%s fuzz %zu: leak %zu bad %ld", name, it, m.live, m.bad);
        CHECK(m.peak < (size_t)96u << 20, "%s fuzz %zu: peak memory %zu", name, it, m.peak);
    }
    free(w); free(file); free(out);
}
int main(void) {
    static const Fx fx[] = {
        { "m16", 1, 16, 8000 }, { "s16", 2, 16, 8000 }, { "s8", 2, 8, 8000 }, { "s24", 2, 24, 8000 }, { "c3", 3, 16, 8000 }, { "c6", 6, 16, 8000 }, { "silence", 2, 16, 8000 },
        { "extreme", 2, 16, 8000 }, { "noise24", 1, 24, 8000 }, { "tiny", 2, 16, 8000 }, { "one", 2, 16, 8000 }, { "exact", 2, 16, 8000 }, { "plus1", 2, 16, 8000 },
        { "m8_noise", 1, 8, 8000 }, { "r44k", 1, 16, 44100 },
    };
    size_t k;
    for (k = 0; k < sizeof fx / sizeof fx[0]; k++) check_fixture(&fx[k]);
    check_fuzz("one", 400); check_fuzz("s16", 300); check_fuzz("c6", 300); check_fuzz("s24", 300);
    if (g_failures) { fprintf(stderr, "TTA: %d FAILURES\n", g_failures); return 1; }
    printf("TTA: PASS (%zu files bit-identical to FFmpeg)\n", sizeof fx / sizeof fx[0]);
    return 0;
}
