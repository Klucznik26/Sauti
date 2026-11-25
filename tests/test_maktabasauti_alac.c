/* MaktabaSauti: ALAC w MP4 / M4A (modul maktabasauti_alac, src/decoders/modern/maktabasauti_alac.c).
 *   1. pliki z kodera FFmpeg `alac` z syntetycznych sygnalow (tests/data/alac/, tests/data/make_alac_fixtures.py): mono do 7.1, 16 i 24 bit, poziomy kompresji, rzedy predykcji 1 i 30, serie zer
 *      (cisza), wartosci skrajne, pakiety verbatim, niepelny ostatni pakiet, 1 ramka; wzorzec = dekoder FFmpeg, probki musza byc IDENTYCZNE (rozpoznanie przez rejestr, bez nazwy kodeka);
 *   2. odczyty po 1 / 7 / 4097 / 100000 ramek, seek dokladny (takze na granicach pakietow, do konca, poza koniec), reset;
 *   3. wejscie nieprzewijalne: czyste E_NOT_SEEKABLE (nic nie zuzyte, bez wyciekow);
 *   4. uszkodzone dane (losowe bajty w calym pliku, takze w moov): zadnych awarii pod ASan / UBSan, bez wyciekow, ograniczona pamiec.
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
static uint32_t rng = 0x2611u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

typedef struct { const char *name; unsigned ch, bits, rate; } Fx;
/* czyta wszystko porcjami po `chunk` ramek; zwraca ostatni wynik (EOF albo blad) */
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
static void check_fixture(const Fx *x) {
    static const size_t chunks[] = { 5000, 1, 7, 4097, 100000 };
    char pa[128], pr[128]; size_t fn, rn, k, nref; uint8_t *file, *rb; int32_t *ref; float *out; float scale;
    snprintf(pa, sizeof pa, "tests/data/alac/%s.m4a", x->name); snprintf(pr, sizeof pr, "tests/data/alac/%s.ref", x->name);
    file = slurp(pa, &fn); rb = slurp(pr, &rn);
    CHECK(file && rb && rn % 4u == 0, "%s: fixture missing (run from the repository root)", x->name);
    if (!file || !rb || rn % 4u) { free(file); free(rb); return; }
    nref = rn / 4u; ref = (int32_t *)malloc(rn); memcpy(ref, rb, rn); free(rb);
    out = (float *)malloc((nref + 64u) * sizeof(float)); scale = 1.0f / (float)(1u << (x->bits - 1u));
    for (k = 0; k < sizeof chunks / sizeof chunks[0]; k++) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info; size_t got = 0, i; maktabasauti_result_t r;
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = file; s.n = fn; s.seekable = 1; s.size_known = 1;
        if (chunks[k] == 7u) s.max_chunk = 3u;
        r = open_src(&s, &m, &d);
        CHECK(r == MAKTABASAUTI_OK, "%s: open (%d)", x->name, (int)r);
        if (r != MAKTABASAUTI_OK) continue;
        memset(&info, 0, sizeof info); info.struct_size = sizeof info; maktabasauti_decoder_get_info(d, &info);
        CHECK(info.channels == x->ch && info.sample_rate == x->rate && info.bits_per_sample == x->bits && info.total_frames == nref / x->ch && strcmp(info.codec_name, "alac") == 0,
              "%s: info %u ch %u Hz %u bit %llu frames codec %s", x->name, info.channels, info.sample_rate, info.bits_per_sample, (unsigned long long)info.total_frames, info.codec_name);
        r = drain(d, out, nref / x->ch + 16u, x->ch, chunks[k], &got);
        CHECK(r == MAKTABASAUTI_EOF && got == nref / x->ch, "%s chunk %zu: end %d, %zu frames vs %zu", x->name, chunks[k], (int)r, got, nref / x->ch);
        for (i = 0; i < got * x->ch && i < nref; i++) if (out[i] != (float)ref[i] * scale) { CHECK(0, "%s chunk %zu: sample %zu is %.9g, expected %.9g", x->name, chunks[k], i, (double)out[i], (double)((float)ref[i] * scale)); break; }
        if (k == 0) {                                                                       /* seek dokladny: losowe pozycje, granice pakietow, koniec, poza koniec, reset */
            static const uint64_t fixed[] = { 0, 1, 4095, 4096, 4097, 8191, 8192 }; unsigned q; uint64_t total = nref / x->ch, act = 0;
            for (q = 0; q < 12u + sizeof fixed / sizeof fixed[0]; q++) {
                const uint64_t t = q < sizeof fixed / sizeof fixed[0] ? fixed[q] : (rnd() % (total + 1u)); size_t want = 300, g2 = 0;
                if (t > total) continue;
                r = maktabasauti_decoder_seek(d, t, MAKTABASAUTI_SEEK_EXACT, &act);
                CHECK(r == MAKTABASAUTI_OK && act == t, "%s: seek %llu (%d)", x->name, (unsigned long long)t, (int)r);
                if (r != MAKTABASAUTI_OK) break;
                if (want > total - t) want = (size_t)(total - t);
                if (!want) { r = maktabasauti_decoder_read(d, out, 10, MAKTABASAUTI_FORMAT_F32, &g2); CHECK(r == MAKTABASAUTI_EOF && g2 == 0, "%s: read at the end (%d)", x->name, (int)r); continue; }
                r = maktabasauti_decoder_read(d, out, want, MAKTABASAUTI_FORMAT_F32, &g2);
                CHECK(r == MAKTABASAUTI_OK && g2 >= 1, "%s: read after seek %llu (%d)", x->name, (unsigned long long)t, (int)r);
                for (i = 0; i < g2 * x->ch; i++) if (out[i] != (float)ref[(size_t)t * x->ch + i] * scale) { CHECK(0, "%s: PCM after seek %llu differs at %zu", x->name, (unsigned long long)t, i); break; }
            }
            CHECK(maktabasauti_decoder_seek(d, total + 1u, MAKTABASAUTI_SEEK_EXACT, &act) == MAKTABASAUTI_E_OUT_OF_RANGE, "%s: seek past the end", x->name);
            CHECK(maktabasauti_decoder_reset(d) == MAKTABASAUTI_OK, "%s: reset", x->name);
            r = drain(d, out, nref / x->ch + 16u, x->ch, 5000, &got);
            CHECK(r == MAKTABASAUTI_EOF && got == nref / x->ch && out[0] == (float)ref[0] * scale, "%s: decode after reset", x->name);
        }
        maktabasauti_decoder_close(d);
        CHECK(m.live == 0 && s.closed == 1, "%s: leak %zu, close %d", x->name, m.live, s.closed);
    }
    {                                                                                       /* wejscie nieprzewijalne: czysty blad, nic nie zuzyte */
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r;
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = file; s.n = fn; s.seekable = 0; s.size_known = 0;
        r = open_src(&s, &m, &d);
        CHECK(r != MAKTABASAUTI_OK && !d && s.closed == 0 && m.live == 0, "%s non-seekable: must fail cleanly (%d)", x->name, (int)r);
    }
    free(out); free(ref); free(file);
}
/* uszkodzone dane: dowolny wynik poza awaria; pamiec ograniczona, brak wyciekow */
static void check_fuzz(const char *name, unsigned iters) {
    char pa[128]; size_t fn, it; uint8_t *file, *w; float *out = (float *)malloc(20000u * 8u * sizeof(float));
    snprintf(pa, sizeof pa, "tests/data/alac/%s.m4a", name);
    file = slurp(pa, &fn);
    CHECK(file != NULL, "%s: missing", name);
    if (!file) { free(out); return; }
    w = (uint8_t *)malloc(fn);
    for (it = 0; it < iters; it++) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; size_t nmut = 1u + rnd() % 6u, q, got = 0, cut = fn; maktabasauti_result_t r;
        memcpy(w, file, fn);
        for (q = 0; q < nmut; q++) {
            const size_t pos = (rnd() & 1u) ? (size_t)(rnd() % fn) : (size_t)(fn - 1u - rnd() % (fn < 400u ? fn : 400u));        /* cala plik, czesto koniec (moov) */
            w[pos] = (uint8_t)(rnd() & 1u ? rnd() : w[pos] ^ (1u << (rnd() & 7u)));
        }
        if (it % 7u == 0) cut = (size_t)(rnd() % fn);
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = w; s.n = cut; s.seekable = 1; s.size_known = 1;
        r = open_src(&s, &m, &d);
        if (r == MAKTABASAUTI_OK) {
            r = drain(d, out, 20000u, 1u, 4096, &got);          /* kanaly nieznane po zepsuciu: bufor na 8 kanalow, odczyt w jednostkach ramek*/
            (void)r; (void)maktabasauti_decoder_flush(d);
            { uint64_t act; (void)maktabasauti_decoder_seek(d, rnd() % 20000u, MAKTABASAUTI_SEEK_EXACT, &act); }
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
        { "m16", 1, 16, 44100 }, { "s16_noise", 2, 16, 44100 }, { "s24", 2, 24, 48000 }, { "c3", 3, 16, 44100 }, { "c5", 5, 16, 44100 }, { "c6", 6, 16, 48000 }, { "c7", 7, 16, 48000 },
        { "c8", 8, 16, 48000 }, { "c6_24", 6, 24, 48000 }, { "silence", 2, 16, 44100 }, { "extreme", 2, 16, 44100 }, { "tiny24", 2, 24, 48000 }, { "verbatim", 2, 16, 44100 },
        { "ord1", 2, 16, 44100 }, { "ord30", 2, 24, 44100 }, { "one", 2, 16, 44100 }, { "r8k", 1, 16, 8000 }, { "s24_silence", 1, 24, 96000 },
    };
    size_t k;
    for (k = 0; k < sizeof fx / sizeof fx[0]; k++) check_fixture(&fx[k]);
    check_fuzz("one", 400); check_fuzz("m16", 300); check_fuzz("silence", 300); check_fuzz("c6", 300);
    if (g_failures) { fprintf(stderr, "ALAC: %d FAILURES\n", g_failures); return 1; }
    printf("ALAC: PASS (%zu files bit-identical to FFmpeg)\n", sizeof fx / sizeof fx[0]);
    return 0;
}
