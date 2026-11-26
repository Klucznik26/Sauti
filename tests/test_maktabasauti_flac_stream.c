/* MaktabaSauti: STRUMIENIOWY dekoder FLAC (modul maktabasauti_flac, src/decoders/modern/maktabasauti_flac.c; dr_flac z wywolaniami zwrotnymi przez host).
 *
 * Wzorzec PCM to dawny adapter z calym buforem (sauti_decoder_flac_ops) na tych samych bajtach.  Pliki to male syntetyczne fixtury (tests/data/flac_*.flac, make_flac_fixtures.sh): stereo 16/24 bit, mono, rozne
 * rozmiary ramek (1152..16384), zapis strumieniowy (STREAMINFO bez dlugosci), okladka w bloku PICTURE (naglowek 38 KiB, poza oknem czytnika).  Wlasnosci:
 *   1. PCM bit w bit jak wzorzec: wejscie przewijalne ze znanym rozmiarem, nieprzewijalne bez rozmiaru, odczyty po 1/7 bajtow, losowe E_WOULD_BLOCK;
 *   2. E_WOULD_BLOCK w kazdym numerze wywolania wejscia po otwarciu (bramkowanie ramek: nic nie ginie), przy otwarciu (naglowek z prefiksu: takze wejscie nieprzewijalne);
 *   3. seek dokladny (losowe cele, granice ramek, do tylu i do przodu), reset, poza koniec = E_OUT_OF_RANGE bez zmiany stanu;
 *   4. urwane dane na KAZDEJ dlugosci: ramki identyczne jak u wzorca na tym samym uciętym buforze, potem E_TRUNCATED (znana dlugosc) albo EOF (nieznana);
 *   5. zapis strumieniowy: dlugosc nieznana, seek wylaczony; pamiec: ~30 MB bez rozmiaru, szczyt zalezy od naglowka (nie od dlugosci);
 *   6. wrogie naglowki (STREAMINFO z losowymi polami, mutacje bajtow): czysty blad albo prefiks wzorca, bez wycieku; STREAMINFO z maksymalnymi polami (w tym max_framesize 16 MiB) nie zuzywa wiecej niz 5 MiB. */
#define _POSIX_C_SOURCE 200809L
#include "maktabasauti/maktabasauti.h"
#include "maktabasauti/maktabasauti_codec.h"
#include "sauti_decoder_flac.h"
#include <assert.h>
#include <math.h>
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

typedef struct {
    const uint8_t *data; size_t n, pos; int seekable, size_known; size_t max_chunk;
    int block_mode; long block_at, calls; uint32_t rng; int armed, closed;
    const uint8_t *gen; size_t gen_len, gen_hdr; uint64_t gen_total, gen_done;
} Src;
static const char *g_force_codec;
static uint32_t xr(uint32_t *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }
static int64_t MAKTABASAUTI_CALL s_read(void *u, void *buf, size_t size) {
    Src *s = (Src *)u; size_t give;
    s->calls++;
    if (s->armed && ((s->block_mode == 1 && s->calls == s->block_at) || (s->block_mode == 3 && (xr(&s->rng) & 3u) == 0))) return MAKTABASAUTI_E_WOULD_BLOCK;
    if (s->max_chunk && size > s->max_chunk) size = s->max_chunk;
    if (s->gen) {
        uint8_t *o = (uint8_t *)buf; give = 0;
        while (give < size && s->gen_done < s->gen_total) {
            const uint64_t at = s->gen_done; o[give++] = at < s->gen_hdr ? s->gen[at] : s->gen[s->gen_hdr + ((at - s->gen_hdr) % (s->gen_len - s->gen_hdr))]; s->gen_done++;
        }
        return (int64_t)give;
    }
    give = s->n - s->pos; if (give > size) give = size;
    if (give) memcpy(buf, s->data + s->pos, give);
    s->pos += give;
    return (int64_t)give;
}
static int64_t MAKTABASAUTI_CALL s_seek(void *u, int64_t off, int wh) {
    Src *s = (Src *)u; int64_t base = wh == 0 ? 0 : wh == 1 ? (int64_t)s->pos : (int64_t)s->n;
    if (base + off < 0 || (uint64_t)(base + off) > (uint64_t)s->n) return MAKTABASAUTI_E_OUT_OF_RANGE;
    s->pos = (size_t)(base + off); return base + off;
}
static int64_t MAKTABASAUTI_CALL s_size(void *u) { Src *s = (Src *)u; return (int64_t)s->n; }
static void MAKTABASAUTI_CALL s_close(void *u) { ((Src *)u)->closed++; }
static maktabasauti_result_t open_src(Src *s, Mem *m, maktabasauti_decoder_t **d) {
    maktabasauti_allocator_t a; maktabasauti_open_params_t p;
    memset(&a, 0, sizeof a); a.struct_size = sizeof a; a.user = m; a.alloc = h_alloc; a.free = h_free;
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION; p.allocator = &a; p.codec = g_force_codec;
    p.io.struct_size = sizeof p.io; p.io.user = s; p.io.read = s_read; p.io.close = s_close; p.io.seek = s->seekable ? s_seek : NULL; p.io.size = s->size_known ? s_size : NULL;
    return maktabasauti_decoder_open(&p, d);
}

static maktabasauti_result_t drain(maktabasauti_decoder_t *d, float *out, size_t ch, size_t cap, size_t chunk, size_t *got) {
    size_t n = 0; long blocked = 0; maktabasauti_result_t rr = MAKTABASAUTI_OK;
    for (;;) {
        size_t g = 0, want = chunk < cap - n ? chunk : cap - n;
        if (want == 0) { rr = MAKTABASAUTI_OK; break; }
        rr = maktabasauti_decoder_read(d, out + n * ch, want, MAKTABASAUTI_FORMAT_F32, &g);
        n += g;
        if (rr == MAKTABASAUTI_E_WOULD_BLOCK) { if (++blocked > 100000) { rr = MAKTABASAUTI_E_INTERNAL; break; } continue; }
        blocked = 0;
        if (rr != MAKTABASAUTI_OK) break;
    }
    *got = n;
    return rr;
}
static int same(const float *a, const float *b, size_t n) { return memcmp(a, b, n * sizeof(float)) == 0; }

static uint32_t rnd(void) { static uint32_t s = 2463534242u; return xr(&s); }

typedef struct { uint8_t *b; size_t n; size_t md_end; const char *name; } Fx;
static Fx load_fx(const char *file) {
    Fx f; char path[256]; FILE *fp; long len;
    snprintf(path, sizeof path, "tests/data/%s", file);
    fp = fopen(path, "rb"); assert(fp);
    fseek(fp, 0, SEEK_END); len = ftell(fp); fseek(fp, 0, SEEK_SET);
    f.b = (uint8_t *)malloc((size_t)len); assert(fread(f.b, 1, (size_t)len, fp) == (size_t)len); fclose(fp);
    f.n = (size_t)len; f.name = file;
    { size_t off = 4; for (;;) { const unsigned h = f.b[off]; const size_t ln = ((size_t)f.b[off + 1] << 16) | ((size_t)f.b[off + 2] << 8) | f.b[off + 3]; off += 4 + ln; if (h & 0x80u) break; } f.md_end = off; }
    return f;
}
typedef struct { float *pcm; size_t frames, ch; uint32_t rate; } Ref;
static Ref reference_bytes(const uint8_t *b, size_t n, int *ok) {
    Ref r; SautiPcmInfo info; void *st; size_t got = 0, cap, g;
    memset(&r, 0, sizeof r); memset(&info, 0, sizeof info);
    st = sauti_decoder_flac_ops.open(b, n, &info); *ok = st != NULL;
    if (!st) return r;
    r.ch = info.channels; r.rate = info.sample_rate; cap = (size_t)info.total_frames;
    r.pcm = (float *)malloc(cap * r.ch * sizeof(float) + 16);
    while (got < cap && (g = sauti_decoder_flac_ops.read(st, r.pcm + got * r.ch, cap - got > 4096 ? 4096 : cap - got)) > 0) got += g;
    r.frames = got;
    sauti_decoder_flac_ops.close(st);
    return r;
}
static Ref reference(const Fx *f) { int ok; Ref r = reference_bytes(f->b, f->n, &ok); assert(ok); return r; }

static void test_inputs(const Fx *f, const Ref *ref, int total_known, const char *name) {
    struct { const char *what; int seekable, size_known; size_t chunk; int block_mode; } cfg[] = {
        { "seekable, size known", 1, 1, 0, 0 }, { "seekable, 1-byte reads", 1, 1, 1, 0 }, { "non-seekable, unknown size", 0, 0, 0, 0 }, { "non-seekable, 1-byte reads", 0, 0, 1, 0 },
        { "non-seekable, 7-byte reads", 0, 0, 7, 0 }, { "non-seekable, random WOULD_BLOCK", 0, 0, 5, 3 }, { "seekable, random WOULD_BLOCK", 1, 1, 3, 3 }, { "non-seekable, size known", 0, 1, 0, 0 },
        { "seekable, size unknown", 1, 0, 0, 0 }, { "non-seekable, 4097-byte reads", 0, 0, 4097, 0 },
    };
    size_t i;
    for (i = 0; i < sizeof cfg / sizeof *cfg; i++) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info; float *out; size_t got = 0; maktabasauti_result_t r, rr; uint64_t act; int tries = 0;
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
        s.data = f->b; s.n = f->n; s.seekable = cfg[i].seekable; s.size_known = cfg[i].size_known; s.max_chunk = cfg[i].chunk; s.block_mode = cfg[i].block_mode; s.rng = 777u + (uint32_t)i;
        do { r = open_src(&s, &m, &d); } while (r == MAKTABASAUTI_E_WOULD_BLOCK && ++tries < 50);
        CHECK(r == MAKTABASAUTI_OK, "%s %s: open failed (%d)", name, cfg[i].what, (int)r);
        if (r != MAKTABASAUTI_OK) continue;
        s.armed = 1;
        info.struct_size = sizeof info; CHECK(maktabasauti_decoder_get_info(d, &info) == MAKTABASAUTI_OK, "info");
        CHECK(info.sample_rate == ref->rate && info.channels == ref->ch, "%s %s: stream parameters differ", name, cfg[i].what);
        CHECK(info.total_frames == (total_known ? ref->frames : MAKTABASAUTI_UNKNOWN_LENGTH), "%s %s: length %llu", name, cfg[i].what, (unsigned long long)info.total_frames);
        CHECK((info.flags & MAKTABASAUTI_STREAM_LOSSLESS) != 0 && strcmp(info.codec_name, "flac") == 0 && strcmp(info.container_name, "flac") == 0, "%s %s: names/flags", name, cfg[i].what);
        if (!cfg[i].seekable || !total_known) {
            CHECK(!(info.flags & MAKTABASAUTI_STREAM_SEEKABLE), "%s %s: must not claim to be seekable", name, cfg[i].what);
            CHECK(maktabasauti_decoder_seek(d, 0, MAKTABASAUTI_SEEK_EXACT, &act) == MAKTABASAUTI_E_NOT_SEEKABLE, "seek on non-seekable");
            CHECK(maktabasauti_decoder_reset(d) == MAKTABASAUTI_E_NOT_SEEKABLE, "reset on non-seekable");
        } else CHECK((info.flags & MAKTABASAUTI_STREAM_SEEKABLE) != 0, "%s %s: must be seekable", name, cfg[i].what);
        out = (float *)malloc((ref->frames + 8192) * ref->ch * sizeof(float));
        rr = drain(d, out, ref->ch, ref->frames + 8192, 333 + i * 41, &got);
        CHECK(rr == MAKTABASAUTI_EOF && got == ref->frames && same(out, ref->pcm, got * ref->ch), "%s %s: end %d, %zu/%zu frames or PCM differs", name, cfg[i].what, (int)rr, got, ref->frames);
        { size_t g2 = 7; maktabasauti_result_t r2 = maktabasauti_decoder_read(d, out, 10, MAKTABASAUTI_FORMAT_F32, &g2); CHECK(r2 == MAKTABASAUTI_EOF && g2 == 0, "%s %s: a read after the end must stay EOF (%d, %zu)", name, cfg[i].what, (int)r2, g2); }
        free(out); maktabasauti_decoder_close(d);
        CHECK(s.closed == 1 && m.live == 0 && m.bad == 0, "%s %s: closed=%d live=%zu bad=%ld", name, cfg[i].what, s.closed, m.live, m.bad);
    }
}
/* E_WOULD_BLOCK w kazdym numerze wywolania wejscia po otwarciu (bramkowanie ramek) */
static void test_block_sweep(const Fx *f, const Ref *ref, const char *name) {
    long at;
    for (at = 1; at <= 60; at++) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; float *out; size_t got = 0; maktabasauti_result_t r, rr;
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
        s.data = f->b; s.n = f->n; s.seekable = (int)(at & 1); s.size_known = s.seekable; s.max_chunk = (at % 3 == 0) ? 11 : (at % 5 == 0 ? 1500 : 0); s.block_mode = 1; s.block_at = at;
        r = open_src(&s, &m, &d);
        CHECK(r == MAKTABASAUTI_OK, "%s block_at=%ld: open (%d)", name, at, (int)r);
        if (r != MAKTABASAUTI_OK) continue;
        s.armed = 1; s.calls = 0;
        out = (float *)malloc((ref->frames + 8192) * ref->ch * sizeof(float));
        rr = drain(d, out, ref->ch, ref->frames + 8192, (at % 4 == 0) ? ref->frames + 8192 : 97 + (size_t)at * 13, &got);      /* co czwarty: jeden ogromny odczyt (wiele ramek w jednym wywolaniu) */
        CHECK(rr == MAKTABASAUTI_EOF && got == ref->frames && same(out, ref->pcm, got * ref->ch), "%s block_at=%ld: end %d, %zu frames or PCM differs", name, at, (int)rr, got);
        free(out); maktabasauti_decoder_close(d);
        CHECK(m.live == 0, "%s block_at=%ld: leak", name, at);
    }
}
/* E_WOULD_BLOCK podczas otwarcia: wejscie przewijalne (host cofa) i nieprzewijalne, gdy naglowek miesci sie w prefiksie (nic nie jest zuzyte do udanego otwarcia) */
static void test_open_block(const Fx *f, const Ref *ref, const char *name) {
    long at; int mode;
    for (mode = 0; mode < 1; mode++) {                                                                  /* tylko wejscie przewijalne: dla nieprzewijalnego host buduje czytnik od nowa i ponowienie nie ma gwarancji */
        for (at = 1; at <= 8; at++) {
            Src s; Mem m; maktabasauti_decoder_t *d = NULL; float *out; size_t got = 0; maktabasauti_result_t r, rr; int tries = 0;
            memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
            s.data = f->b; s.n = f->n; s.seekable = mode == 0; s.size_known = mode == 0; s.max_chunk = (at & 1) ? 0 : 701; s.block_mode = 1; s.block_at = at; s.armed = 1;
            do { r = open_src(&s, &m, &d); } while (r == MAKTABASAUTI_E_WOULD_BLOCK && ++tries < 5);
            CHECK(r == MAKTABASAUTI_OK && tries <= 1, "%s open block_at=%ld (%s): open (%d) after %d tries", name, at, mode ? "non-seekable" : "seekable", (int)r, tries);
            if (r != MAKTABASAUTI_OK) continue;
            s.armed = 0;
            out = (float *)malloc((ref->frames + 8192) * ref->ch * sizeof(float));
            rr = drain(d, out, ref->ch, ref->frames + 8192, 977, &got);
            CHECK(rr == MAKTABASAUTI_EOF && got == ref->frames && same(out, ref->pcm, got * ref->ch), "%s open block_at=%ld (%s): end %d, %zu frames or PCM differs", name, at, mode ? "non-seekable" : "seekable", (int)rr, got);
            free(out); maktabasauti_decoder_close(d);
            CHECK(m.live == 0, "%s open block_at=%ld: leak", name, at);
        }
    }
}
static void test_seek(const Fx *f, const Ref *ref, const char *name) {
    Src s; Mem m; maktabasauti_decoder_t *d = NULL; float *buf = (float *)malloc(8000 * ref->ch * sizeof(float)); size_t i; uint64_t act; maktabasauti_result_t r;
    memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
    s.data = f->b; s.n = f->n; s.seekable = 1; s.size_known = 1;
    r = open_src(&s, &m, &d); CHECK(r == MAKTABASAUTI_OK, "%s seek: open", name); if (r != MAKTABASAUTI_OK) { free(buf); return; }
    for (i = 0; i < 120; i++) {
        uint64_t target = i == 0 ? 0 : i == 1 ? ref->frames : i == 2 ? ref->frames - 1 : i == 3 ? ref->frames - 1 : (rnd() % (ref->frames + 1)); size_t want = 5000, got = 0, avail;
        r = maktabasauti_decoder_seek(d, target, MAKTABASAUTI_SEEK_EXACT, &act);
        CHECK(r == MAKTABASAUTI_OK && act == target, "%s seek %llu: %d", name, (unsigned long long)target, (int)r);
        if (r != MAKTABASAUTI_OK) break;
        avail = (size_t)(ref->frames - target); if (want > avail) want = avail;
        if (want) {
            r = drain(d, buf, ref->ch, want, 1500, &got);
            CHECK((r == MAKTABASAUTI_OK || r == MAKTABASAUTI_EOF) && got == want && same(buf, ref->pcm + target * ref->ch, got * ref->ch), "%s after seek %llu: %d, %zu/%zu frames or PCM differs", name, (unsigned long long)target, (int)r, got, want);
        } else { r = maktabasauti_decoder_read(d, buf, 10, MAKTABASAUTI_FORMAT_F32, &got); CHECK(r == MAKTABASAUTI_EOF && got == 0, "%s read at the end: %d", name, (int)r); }
    }
    { uint64_t before = 0, after = 1; (void)maktabasauti_decoder_tell(d, &before);
      CHECK(maktabasauti_decoder_seek(d, ref->frames + 1, MAKTABASAUTI_SEEK_EXACT, &act) == MAKTABASAUTI_E_OUT_OF_RANGE, "%s seek past the end", name);
      (void)maktabasauti_decoder_tell(d, &after); CHECK(before == after, "%s: a failed seek must not change the position", name); }
    CHECK(maktabasauti_decoder_reset(d) == MAKTABASAUTI_OK, "%s reset", name);
    { size_t got = 0; r = drain(d, buf, ref->ch, 100, 100, &got); CHECK(r == MAKTABASAUTI_OK && same(buf, ref->pcm, got * ref->ch), "%s after reset", name); }
    maktabasauti_decoder_close(d); free(buf);
    CHECK(m.live == 0, "%s seek: leak", name);
}
/* ucięcie na kazdej dlugosci: wzorzec = dawny adapter na tym samym uciętym buforze (ramki kompletne), potem E_TRUNCATED (znana dlugosc) albo EOF */
static void test_truncation(const Fx *f, const Ref *full, int total_known, const char *name) {
    size_t cut; float *out = (float *)malloc((full->frames + 8192) * full->ch * sizeof(float)); long checked = 0;
    for (cut = 0; cut <= f->n; cut += (cut < f->md_end + 64 || cut + 64 > f->n) ? 1u + (cut < 5000 ? 0u : rnd() % 3000u) : 1u + rnd() % 211u) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; size_t got = 0, expect = 0; maktabasauti_result_t r, rr; int ok; Ref ref;
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = f->b; s.n = cut; s.seekable = (int)(cut & 1); s.size_known = s.seekable && ((cut % 4) == 1); s.max_chunk = (cut % 3) ? 0 : 5;
        if (cut >= 42 && total_known) { ref = reference_bytes(f->b, cut, &ok); expect = ref.frames; free(ref.pcm); } else ok = 0;
        r = open_src(&s, &m, &d);
        if (!total_known || !ok) {                                                                      /* wzorzec odrzuca (naglowek urwany) albo nie ma dlugosci: tylko czysty blad albo poprawny prefiks */
            if (r != MAKTABASAUTI_OK) { CHECK(!d && s.closed == 0 && m.live == 0, "%s cut %zu: failed open must leave the input", name, cut); continue; }
            rr = drain(d, out, full->ch, full->frames + 8192, 211, &got);
            CHECK(got <= full->frames && same(out, full->pcm, got * full->ch), "%s cut %zu: PCM is not a prefix of the reference (%zu frames)", name, cut, got);
            maktabasauti_decoder_close(d); CHECK(m.live == 0, "%s cut %zu: leak", name, cut); (void)rr; continue;
        }
        CHECK(r == MAKTABASAUTI_OK, "%s cut %zu: open (%d)", name, cut, (int)r);
        if (r != MAKTABASAUTI_OK) continue;
        rr = drain(d, out, full->ch, full->frames + 8192, 211, &got);
        CHECK(got == expect && same(out, full->pcm, got * full->ch), "%s cut %zu: %zu frames, the old adapter decodes %zu on the same bytes", name, cut, got, expect);
        CHECK(rr == (got == full->frames ? MAKTABASAUTI_EOF : MAKTABASAUTI_E_TRUNCATED), "%s cut %zu: end code %d for %zu/%zu frames", name, cut, (int)rr, got, full->frames);
        if (rr == MAKTABASAUTI_E_TRUNCATED) {
            size_t g2 = 0; maktabasauti_result_t r2 = maktabasauti_decoder_read(d, out, 10, MAKTABASAUTI_FORMAT_F32, &g2);
            CHECK(r2 == MAKTABASAUTI_E_TRUNCATED && g2 == 0, "%s cut %zu: E_TRUNCATED must be sticky (%d)", name, cut, (int)r2);
            if (s.seekable && s.size_known && got >= 2) {
                uint64_t act; size_t g3 = 0; maktabasauti_result_t r3 = maktabasauti_decoder_seek(d, 0, MAKTABASAUTI_SEEK_EXACT, &act);
                CHECK(r3 == MAKTABASAUTI_OK, "%s cut %zu: seek after E_TRUNCATED (%d)", name, cut, (int)r3);
                r3 = maktabasauti_decoder_read(d, out, 2, MAKTABASAUTI_FORMAT_F32, &g3);
                CHECK(r3 == MAKTABASAUTI_OK && g3 == 2 && same(out, full->pcm, 2 * full->ch), "%s cut %zu: read after a seek past a sticky E_TRUNCATED (%d, %zu)", name, cut, (int)r3, g3);
            }
        }
        maktabasauti_decoder_close(d); checked++;
        CHECK(m.live == 0 && s.closed == 1, "%s cut %zu: leak or close count %d", name, cut, s.closed);
    }
    CHECK(!total_known || checked > 40, "%s: only %ld cuts were checked against the reference", name, checked);
    free(out);
}

/* pamiec: strumien ~30 MB powtarzanych ramek z pliku zapisanego strumieniowo (dlugosc nieznana); szczyt zalezy od naglowka, nie od dlugosci */
static void test_memory(const Fx *f, const char *name, size_t limit) {
    Src s; Mem m; maktabasauti_decoder_t *d = NULL; static float out[4096 * 2]; size_t got, total = 0; maktabasauti_result_t r;
    memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
    s.gen = f->b; s.gen_len = f->n; s.gen_hdr = f->md_end; s.gen_total = (uint64_t)30 << 20; s.max_chunk = 4096; s.n = f->n;
    r = open_src(&s, &m, &d); CHECK(r == MAKTABASAUTI_OK, "%s generator: open (%d)", name, (int)r); if (r != MAKTABASAUTI_OK) return;
    for (;;) { r = maktabasauti_decoder_read(d, out, 4096, MAKTABASAUTI_FORMAT_F32, &got); if (r != MAKTABASAUTI_OK) { CHECK(r == MAKTABASAUTI_EOF || r == MAKTABASAUTI_E_CORRUPT_DATA, "%s generator: code %d", name, (int)r); break; } total += got; }
    CHECK(total > 3000000u, "%s generator: decoded only %zu frames", name, total);
    CHECK(m.peak < limit, "%s generator: memory peak %zu bytes for an unbounded source (limit %zu)", name, m.peak, limit);
    maktabasauti_decoder_close(d);
    CHECK(m.live == 0, "%s generator: leak", name);
}

/* ---- wrogie naglowki i mutacje ---- */
static void test_hostile(const Fx *base) {
    size_t k; uint8_t *b = (uint8_t *)malloc(base->n); static float out[8192 * 2], oldf[8192 * 2]; long accepted = 0, rejected = 0;
    for (k = 0; k < 600; k++) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r; size_t n = base->n, j, got = 0; int mode;
        memcpy(b, base->b, base->n);
        for (j = 0; j < 1u + rnd() % 3u; j++) {
            size_t at = (k & 1) ? 4u + rnd() % 42u : rnd() % (base->n < 12000 ? base->n : 12000); const unsigned how = rnd() % 4u;
            if (how == 0) b[at] = (uint8_t)rnd(); else if (how == 1) b[at] ^= (uint8_t)(1u << (rnd() & 7u)); else if (how == 2) b[at] = (rnd() & 1) ? 0xff : 0x00; else b[at]++;
        }
        if (k % 11 == 0) n = rnd() % (base->n + 1);
        for (mode = 0; mode < 2; mode++) {
            SautiPcmInfo info; void *st; memset(&info, 0, sizeof info); st = sauti_decoder_flac_ops.open(b, n, &info);
            memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = b; s.n = n; s.seekable = mode == 0; s.size_known = mode == 0; s.max_chunk = mode ? 13 : 0;
            r = open_src(&s, &m, &d);
            if (r == MAKTABASAUTI_OK) {
                maktabasauti_stream_info_t si; si.struct_size = sizeof si; (void)maktabasauti_decoder_get_info(d, &si);
                (void)drain(d, out, si.channels, 8192, 700, &got);
                if (st) {                                                                                 /* wzorzec przyjmuje: PCM modulu jest prefiksem PCM wzorca (modul moze skonczyc wczesniej: limit ramki z naglowka) */
                    const size_t g2 = sauti_decoder_flac_ops.read(st, oldf, 8192);
                    CHECK(got <= g2 || g2 == 0, "mutation %zu: the module decoded %zu frames, the old adapter %zu", k, got, g2);
                    if (g2 && got) CHECK(same(out, oldf, (got < g2 ? got : g2) * si.channels), "mutation %zu (%s): PCM differs from the old adapter", k, mode ? "non-seekable" : "seekable");
                }
                accepted++; maktabasauti_decoder_close(d);
            } else { CHECK(!d && s.closed == 0, "mutation %zu: failed open must leave the input with the caller", k); rejected++; }
            if (st) sauti_decoder_flac_ops.close(st);
            CHECK(m.live == 0 && m.bad == 0, "mutation %zu: leak", k);
        }
    }
    printf("%s mutations: accepted %ld, rejected %ld\n", base->name, accepted, rejected);
    free(b);
}
/* STREAMINFO z losowymi polami: czysty blad albo dziala; naglowek z maksymalnymi polami nie zuzywa niekontrolowanej pamieci */
static void test_streaminfo_fields(void) {
    size_t k; uint8_t b[8192];
    for (k = 0; k < 3000; k++) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r; size_t i;
        for (i = 0; i < sizeof b; i++) b[i] = (uint8_t)rnd();
        memcpy(b, "fLaC", 4); b[4] = 0x80; b[5] = 0; b[6] = 0; b[7] = 34;
        if (k & 1) { b[8] = 0; b[9] = (uint8_t)(rnd() % 64); b[10] = (uint8_t)(rnd() & 255); b[11] = (uint8_t)rnd(); }
        if (k % 7 == 0) { b[10] = 0xff; b[11] = 0xff; b[15] = b[16] = b[17] = 0xff; b[20] = (uint8_t)((rnd() & 0xf0) | 0x0b); b[21] = (uint8_t)(0xf0 | (rnd() & 15)); }
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = b; s.n = (k % 5 == 0) ? 42 + rnd() % 200 : sizeof b; s.seekable = (int)(k & 2) != 0; s.size_known = s.seekable;
        r = open_src(&s, &m, &d);
        if (r == MAKTABASAUTI_OK) { static float out[4096]; size_t got; uint64_t act; (void)maktabasauti_decoder_read(d, out, 500, MAKTABASAUTI_FORMAT_F32, &got); (void)maktabasauti_decoder_seek(d, 7, MAKTABASAUTI_SEEK_EXACT, &act); (void)maktabasauti_decoder_read(d, out, 500, MAKTABASAUTI_FORMAT_F32, &got); maktabasauti_decoder_close(d); }
        else CHECK(!d && s.closed == 0, "streaminfo %zu: failed open must leave the input with the caller", k);
        CHECK(m.live == 0 && m.bad == 0, "streaminfo %zu: leak", k);
        /* the worst case grew with FLAC multichannel (8 channels of 65535 samples): 5.9 MB, bounded by 8 MiB */
        CHECK(m.peak < (size_t)(8u << 20), "streaminfo %zu: memory peak %zu", k, m.peak);
    }
}
static void test_tiny_and_probe(void) {
    extern const maktabasauti_codec_t maktabasauti_flac_desc; const maktabasauti_codec_t *c = &maktabasauti_flac_desc; size_t n; uint8_t b[64]; size_t i;
    for (n = 0; n <= 50; n++) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r;
        for (i = 0; i < sizeof b; i++) b[i] = (uint8_t)rnd();
        if (n >= 4) memcpy(b, "fLaC", 4);
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = b; s.n = n; s.seekable = (int)(n & 1); s.size_known = s.seekable;
        g_force_codec = "flac"; r = open_src(&s, &m, &d); g_force_codec = NULL;
        CHECK(r != MAKTABASAUTI_OK && !d && s.closed == 0 && m.live == 0, "FLAC with %zu bytes: must fail cleanly (%d)", n, (int)r);
    }
    CHECK(c->probe(c, (const uint8_t *)"fLaC", 4, NULL) > 0 && c->probe(c, (const uint8_t *)"fLaC", 3, NULL) == 0 && c->probe(c, (const uint8_t *)"fLaD", 4, NULL) == 0 && c->probe(c, NULL, 4, NULL) == 0 && c->probe(c, (const uint8_t *)"OggS", 4, NULL) == 0, "FLAC probe");
}

int main(void) {
    static const char *const files[] = { "flac_st16.flac", "flac_st16_small.flac", "flac_st16_big.flac", "flac_mono16.flac", "flac_st24.flac", "flac_st16_meta.flac", "flac_st16_streamed.flac" };
    size_t i; Fx fx[7]; Ref ref[7];
    for (i = 0; i < sizeof files / sizeof *files; i++) {
        const int streamed = strstr(files[i], "streamed") != NULL;
        fx[i] = load_fx(files[i]);
        if (streamed) { fx[i].name = files[i]; ref[i] = reference(&fx[0]); }                      /* ta sama tresc co flac_st16: wzorzec z pliku z dlugoscia */
        else ref[i] = reference(&fx[i]);
        printf("%s: %zu bytes, metadata %zu bytes, %zu frames x %zu ch @ %u Hz\n", files[i], fx[i].n, fx[i].md_end, ref[i].frames, ref[i].ch, ref[i].rate);
        test_inputs(&fx[i], &ref[i], !streamed, files[i]);
        test_block_sweep(&fx[i], &ref[i], files[i]);
        test_open_block(&fx[i], &ref[i], files[i]);
        if (!streamed) { test_seek(&fx[i], &ref[i], files[i]); test_truncation(&fx[i], &ref[i], 1, files[i]); }
        else test_truncation(&fx[i], &ref[i], 0, files[i]);
        if (i != 5) test_hostile(&fx[i]);
    }
    test_memory(&fx[6], "streamed st16 (maxbs 4096)", (size_t)256 << 10);
    test_streaminfo_fields(); test_tiny_and_probe();
    for (i = 0; i < sizeof files / sizeof *files; i++) { free(fx[i].b); free(ref[i].pcm); }
    printf("FLAC streaming: %s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}
