/* MaktabaSauti: STRUMIENIOWY dekoder VOC (modul maktabasauti_voc, src/decoders/retro/pc/maktabasauti_voc.c).
 *
 * Wzorzec PCM to dawny adapter z calym buforem (sauti_decoder_voc_ops) na tych samych bajtach.  Pliki sa generowane (bloki 1/2/3/4/5/8/9: 8 bit mono, stereo przez blok 8, 8/16 bit przez blok 9, cisza,
 * znaczniki, z blokiem konca i bez), wiec test nie zalezy od plikow uzytkownika.  Wlasnosci:
 *   1. PCM bit w bit jak wzorzec: wejscie przewijalne (dlugosc znana, indeks czesci) i nieprzewijalne (tryb strumieniowy, dlugosc nieznana), odczyty po 1/7 bajtow, losowe E_WOULD_BLOCK;
 *   2. E_WOULD_BLOCK w kazdym numerze wywolania wejscia po otwarciu oraz przy otwarciu (wejscie przewijalne);
 *   3. seek dokladny (losowe cele, granice czesci, do tylu i do przodu), reset, poza koniec = E_OUT_OF_RANGE bez zmiany stanu;
 *   4. urwane dane na KAZDEJ dlugosci: ramki do miejsca ucięcia dokladnie jak w niezaleznym modelu, potem EOF (granica bloku) albo E_TRUNCATED;
 *   5. czesci: 16384 czesci (jak dawniej) przyjete, 16385 odrzucone przy wejsciu przewijalnym, a w trybie strumieniowym przyjete; bloki powtorzen / ADPCM / nieznane = czysty blad;
 *   6. pamiec: ~100 MB bez rozmiaru, szczyt < 64 KiB; mutacje naglowkow: wszystko, co przyjmuje dawny adapter, przyjmuje tez modul z tym samym PCM (oba tryby). */
#define _POSIX_C_SOURCE 200809L
#include "maktabasauti/maktabasauti.h"
#include "maktabasauti/maktabasauti_codec.h"
#include "sauti_decoder_voc.h"
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

/* ---- generowanie ---- */
static uint32_t g_rng = 2463534242u;
static uint32_t rnd(void) { return xr(&g_rng); }
typedef struct { uint8_t *b; size_t n, cap; } Buf;
static void bput(Buf *x, const void *p, size_t n) {
    if (x->n + n > x->cap) { x->cap = (x->n + n) * 2 + 64; x->b = (uint8_t *)realloc(x->b, x->cap); }
    memcpy(x->b + x->n, p, n); x->n += n;
}
static void b8(Buf *x, unsigned v) { uint8_t c = (uint8_t)v; bput(x, &c, 1); }
static void l16(Buf *x, unsigned v) { b8(x, v & 255u); b8(x, (v >> 8) & 255u); }
static void l32(Buf *x, uint32_t v) { l16(x, v & 0xffffu); l16(x, v >> 16); }
static void rbytes(Buf *x, size_t n) { size_t i; for (i = 0; i < n; i++) b8(x, rnd()); }

typedef struct { float *pcm; size_t frames, ch; uint32_t rate; } Ref;
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

/* ---- generowanie ---- */
typedef struct {
    uint8_t *b; size_t n; unsigned nb; size_t bstart[512], bend[512], dstart[512]; uint64_t cum[513]; uint64_t bframes[512]; unsigned bfb[512]; uint64_t frames; unsigned ch; uint32_t rate; int terminated;
} Voc;
static void voc_begin(Buf *x) { bput(x, "Creative Voice File\x1a", 20); l16(x, 26); l16(x, 0x010a); l16(x, 0x1129); }
static void voc_hdr(Buf *x, unsigned type, uint32_t len) { b8(x, type); b8(x, len & 255u); b8(x, (len >> 8) & 255u); b8(x, (len >> 16) & 255u); }
/* rejestr bloku: granice i liczba ramek (fb = bajtow na ramke; 0 = blok bez ramek) */
static void note(Voc *v, size_t start, size_t end, size_t dstart, uint64_t frames, unsigned fb) {
    v->bstart[v->nb] = start; v->bend[v->nb] = end; v->dstart[v->nb] = dstart; v->bframes[v->nb] = frames; v->bfb[v->nb] = fb; v->cum[v->nb + 1] = v->cum[v->nb] + frames; v->nb++;
}
static Voc make_voc(unsigned mode, int terminated, unsigned nblocks) {
    Buf x = { 0 }; Voc v; unsigned i, ch = 1, width = 1; uint32_t rate; const unsigned tc = 156u;                      /* 1e6 / (256-156) = 10000 Hz */
    memset(&v, 0, sizeof v); voc_begin(&x);
    rate = mode == 0 ? 10000u : mode == 1 ? 11025u : (mode == 2 ? 22050u : 44100u);
    if (mode == 3) { ch = 2; width = 2; } if (mode == 2) { ch = 2; width = 1; } if (mode == 1) { ch = 2; width = 1; }
    if (mode == 0) rate = 1000000u / (256u - tc);
    if (mode == 1) { /* blok 8: rozszerzona czestotliwosc stereo: rate = 256e6 / (65536 - tc16) / 2 */ const uint32_t tc16 = 65536u - 256000000u / (rate * 2u); rate = 256000000u / (65536u - tc16) / 2u; }
    for (i = 0; i < nblocks; i++) {
        const size_t start = x.n; const unsigned kind = i == 0 ? 0u : rnd() % 6u; const uint32_t frames = 5u + rnd() % 3000u; size_t ds;
        if (i == 0 || kind == 0) {                                                                                /* blok z naglowkiem formatu */
            if (mode == 0) { voc_hdr(&x, 1, 2u + frames); b8(&x, tc); b8(&x, 0); ds = x.n; rbytes(&x, frames); note(&v, start, x.n, ds, frames, 1); }
            else if (mode == 1) { const uint32_t tc16 = 65536u - 256000000u / (rate * 2u); voc_hdr(&x, 8, 4); l16(&x, tc16); b8(&x, 0); b8(&x, 1); { const size_t s2 = x.n; (void)s2; }
                                  note(&v, start, x.n, x.n, 0, 0); { const size_t s3 = x.n; voc_hdr(&x, 1, 2u + frames * 2u); b8(&x, 0); b8(&x, 0); ds = x.n; rbytes(&x, (size_t)frames * 2u); note(&v, s3, x.n, ds, frames, 2); } }
            else { voc_hdr(&x, 9, 12u + frames * ch * width); l32(&x, rate); b8(&x, width * 8u); b8(&x, ch); l16(&x, width == 2 ? 4u : 0u); l32(&x, 0); ds = x.n; rbytes(&x, (size_t)frames * ch * width); note(&v, start, x.n, ds, frames, ch * width); }
        } else if (kind == 1 || kind == 2) { voc_hdr(&x, 2, frames * ch * width); ds = x.n; rbytes(&x, (size_t)frames * ch * width); note(&v, start, x.n, ds, frames, ch * width); }   /* kontynuacja */
        else if (kind == 3 && mode == 0) { voc_hdr(&x, 3, 3); l16(&x, (unsigned)(frames - 1u)); b8(&x, tc); note(&v, start, x.n, x.n, frames > 0xffffu ? 0xffffu + 1u : frames, 0); v.bfb[v.nb - 1] = 0; }
        else if (kind == 4) { voc_hdr(&x, 4, 2); l16(&x, 1234); note(&v, start, x.n, x.n, 0, 0); }
        else { const unsigned len = rnd() % 40u; voc_hdr(&x, 5, len); rbytes(&x, len); note(&v, start, x.n, x.n, 0, 0); }
    }
    if (terminated) b8(&x, 0);
    v.b = x.b; v.n = x.n; v.frames = v.cum[v.nb]; v.ch = ch; v.rate = rate; v.terminated = terminated;
    return v;
}
static const SautiDecoderOps *VOC_OPS = &sauti_decoder_voc_ops;
typedef struct { uint8_t *b; size_t n; } Bytes;
static Ref reference_bytes(const uint8_t *b, size_t n) {
    Ref r; SautiPcmInfo info; void *st; size_t got = 0, cap, g;
    memset(&info, 0, sizeof info);
    st = VOC_OPS->open(b, n, &info);
    assert(st); r.ch = info.channels; r.rate = info.sample_rate; cap = (size_t)info.total_frames;
    r.pcm = (float *)malloc(cap * r.ch * sizeof(float) + 16);
    while ((g = VOC_OPS->read(st, r.pcm + got * r.ch, cap - got > 4096 ? 4096 : cap - got)) > 0) got += g;
    assert(got == cap); r.frames = got;
    VOC_OPS->close(st);
    return r;
}

static void test_inputs(const Voc *f, const Ref *ref, const char *name) {
    struct { const char *what; int seekable, size_known; size_t chunk; int block_mode; } cfg[] = {
        { "seekable, size known", 1, 1, 0, 0 }, { "seekable, 1-byte reads", 1, 1, 1, 0 }, { "non-seekable, unknown size", 0, 0, 0, 0 }, { "non-seekable, 1-byte reads", 0, 0, 1, 0 },
        { "non-seekable, 7-byte reads", 0, 0, 7, 0 }, { "non-seekable, random WOULD_BLOCK", 0, 0, 5, 3 }, { "seekable, random WOULD_BLOCK", 1, 1, 3, 3 }, { "non-seekable, size known", 0, 1, 0, 0 },
        { "seekable, size unknown", 1, 0, 0, 0 },
    };
    size_t i;
    for (i = 0; i < sizeof cfg / sizeof *cfg; i++) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info; float *out; size_t got = 0; maktabasauti_result_t r, rr; uint64_t act;
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
        s.data = f->b; s.n = f->n; s.seekable = cfg[i].seekable; s.size_known = cfg[i].size_known; s.max_chunk = cfg[i].chunk; s.block_mode = cfg[i].block_mode; s.rng = 777u + (uint32_t)i;
        r = open_src(&s, &m, &d);
        CHECK(r == MAKTABASAUTI_OK, "%s %s: open failed (%d)", name, cfg[i].what, (int)r);
        if (r != MAKTABASAUTI_OK) continue;
        s.armed = 1;
        info.struct_size = sizeof info; CHECK(maktabasauti_decoder_get_info(d, &info) == MAKTABASAUTI_OK, "info");
        CHECK(info.sample_rate == ref->rate && info.channels == ref->ch, "%s %s: stream parameters differ (%u Hz %u ch vs %u Hz %zu ch)", name, cfg[i].what, info.sample_rate, info.channels, ref->rate, ref->ch);
        if (cfg[i].seekable) CHECK(info.total_frames == ref->frames, "%s %s: length %llu vs %zu", name, cfg[i].what, (unsigned long long)info.total_frames, ref->frames);
        else CHECK(info.total_frames == MAKTABASAUTI_UNKNOWN_LENGTH, "%s %s: streaming mode has an unknown length (%llu)", name, cfg[i].what, (unsigned long long)info.total_frames);
        CHECK((info.flags & MAKTABASAUTI_STREAM_LOSSLESS) != 0 && strcmp(info.codec_name, "pcm") == 0 && strcmp(info.container_name, "voc") == 0, "%s %s: names/flags", name, cfg[i].what);
        if (!cfg[i].seekable) {
            CHECK(!(info.flags & MAKTABASAUTI_STREAM_SEEKABLE), "%s %s: must not claim to be seekable", name, cfg[i].what);
            CHECK(maktabasauti_decoder_seek(d, 0, MAKTABASAUTI_SEEK_EXACT, &act) == MAKTABASAUTI_E_NOT_SEEKABLE, "seek on non-seekable");
            CHECK(maktabasauti_decoder_reset(d) == MAKTABASAUTI_E_NOT_SEEKABLE, "reset on non-seekable");
        }
        out = (float *)malloc((ref->frames + 4096) * ref->ch * sizeof(float));
        rr = drain(d, out, ref->ch, ref->frames + 4096, 333 + i * 41, &got);
        CHECK(rr == MAKTABASAUTI_EOF && got == ref->frames && same(out, ref->pcm, got * ref->ch), "%s %s: end %d, %zu/%zu frames or PCM differs", name, cfg[i].what, (int)rr, got, ref->frames);
        { size_t g2 = 7; maktabasauti_result_t r2 = maktabasauti_decoder_read(d, out, 10, MAKTABASAUTI_FORMAT_F32, &g2); CHECK(r2 == MAKTABASAUTI_EOF && g2 == 0, "%s %s: a read after the end must stay EOF (%d, %zu)", name, cfg[i].what, (int)r2, g2); }
        free(out); maktabasauti_decoder_close(d);
        CHECK(s.closed == 1 && m.live == 0 && m.bad == 0, "%s %s: closed=%d live=%zu bad=%ld", name, cfg[i].what, s.closed, m.live, m.bad);
    }
}
static void test_block_sweep(const Voc *f, const Ref *ref, const char *name) {
    long at;
    for (at = 1; at <= 40; at++) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; float *out; size_t got = 0; maktabasauti_result_t r, rr;
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
        s.data = f->b; s.n = f->n; s.seekable = (int)(at & 1); s.size_known = s.seekable; s.max_chunk = (at % 3 == 0) ? 11 : 0; s.block_mode = 1; s.block_at = at;
        r = open_src(&s, &m, &d);
        CHECK(r == MAKTABASAUTI_OK, "%s block_at=%ld: open (%d)", name, at, (int)r);
        if (r != MAKTABASAUTI_OK) continue;
        s.armed = 1; s.calls = 0;
        out = (float *)malloc((ref->frames + 4096) * ref->ch * sizeof(float));
        rr = drain(d, out, ref->ch, ref->frames + 4096, 97, &got);
        CHECK(rr == MAKTABASAUTI_EOF && got == ref->frames && same(out, ref->pcm, got * ref->ch), "%s block_at=%ld: end %d, %zu frames or PCM differs", name, at, (int)rr, got);
        free(out); maktabasauti_decoder_close(d);
        CHECK(m.live == 0, "%s block_at=%ld: leak", name, at);
    }
}
static void test_open_block(const Voc *f, const Ref *ref, const char *name) {
    long at;
    for (at = 1; at <= 8; at++) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; float *out; size_t got = 0; maktabasauti_result_t r, rr; int tries = 0;
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
        s.data = f->b; s.n = f->n; s.seekable = 1; s.size_known = 1; s.max_chunk = (at & 1) ? 0 : 7; s.block_mode = 1; s.block_at = at; s.armed = 1;
        do { r = open_src(&s, &m, &d); } while (r == MAKTABASAUTI_E_WOULD_BLOCK && ++tries < 5);
        CHECK(r == MAKTABASAUTI_OK && tries <= 1, "%s open block_at=%ld: open (%d) after %d tries", name, at, (int)r, tries);
        if (r != MAKTABASAUTI_OK) continue;
        s.armed = 0;
        out = (float *)malloc((ref->frames + 4096) * ref->ch * sizeof(float));
        rr = drain(d, out, ref->ch, ref->frames + 4096, 97, &got);
        CHECK(rr == MAKTABASAUTI_EOF && got == ref->frames && same(out, ref->pcm, got * ref->ch), "%s open block_at=%ld: end %d, %zu frames or PCM differs", name, at, (int)rr, got);
        free(out); maktabasauti_decoder_close(d);
        CHECK(m.live == 0, "%s open block_at=%ld: leak", name, at);
    }
}
static void test_seek(const Voc *f, const Ref *ref, const char *name) {
    Src s; Mem m; maktabasauti_decoder_t *d = NULL; float *buf = (float *)malloc(2000 * ref->ch * sizeof(float)); size_t i; uint64_t act; maktabasauti_result_t r;
    memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
    s.data = f->b; s.n = f->n; s.seekable = 1; s.size_known = 1;
    r = open_src(&s, &m, &d); CHECK(r == MAKTABASAUTI_OK, "%s seek: open", name); if (r != MAKTABASAUTI_OK) { free(buf); return; }
    for (i = 0; i < 250; i++) {
        uint64_t target = i == 0 ? 0 : i == 1 ? ref->frames : i == 2 ? ref->frames - 1 : (rnd() % (ref->frames + 1)); size_t want = 1500, got = 0, avail;
        if (i > 3 && (i & 3) == 0) { const unsigned bi = rnd() % f->nb; target = f->cum[bi] + ((rnd() & 1) ? 0u : (f->bframes[bi] ? f->bframes[bi] - 1u : 0u)); if (target > ref->frames) target = ref->frames; }
        r = maktabasauti_decoder_seek(d, target, MAKTABASAUTI_SEEK_EXACT, &act);
        CHECK(r == MAKTABASAUTI_OK && act == target, "%s seek %llu: %d", name, (unsigned long long)target, (int)r);
        if (r != MAKTABASAUTI_OK) break;
        avail = (size_t)(ref->frames - target); if (want > avail) want = avail;
        if (want) {
            r = maktabasauti_decoder_read(d, buf, want, MAKTABASAUTI_FORMAT_F32, &got);
            CHECK((r == MAKTABASAUTI_OK || r == MAKTABASAUTI_EOF) && got >= 1 && same(buf, ref->pcm + target * ref->ch, got * ref->ch), "%s after seek %llu: %d, %zu frames or PCM differs", name, (unsigned long long)target, (int)r, got);
        } else { r = maktabasauti_decoder_read(d, buf, 10, MAKTABASAUTI_FORMAT_F32, &got); CHECK(r == MAKTABASAUTI_EOF && got == 0, "%s read at the end: %d", name, (int)r); }
    }
    { uint64_t before = 0, after = 1; (void)maktabasauti_decoder_tell(d, &before);
      CHECK(maktabasauti_decoder_seek(d, ref->frames + 1, MAKTABASAUTI_SEEK_EXACT, &act) == MAKTABASAUTI_E_OUT_OF_RANGE, "%s seek past the end", name);
      (void)maktabasauti_decoder_tell(d, &after); CHECK(before == after, "%s: a failed seek must not change the position", name); }
    CHECK(maktabasauti_decoder_reset(d) == MAKTABASAUTI_OK, "%s reset", name);
    { size_t got = 0; r = maktabasauti_decoder_read(d, buf, 100 < ref->frames ? 100 : ref->frames, MAKTABASAUTI_FORMAT_F32, &got); CHECK((r == MAKTABASAUTI_OK) && same(buf, ref->pcm, got * ref->ch), "%s after reset", name); }
    maktabasauti_decoder_close(d); free(buf);
    CHECK(m.live == 0, "%s seek: leak", name);
}
/* ucięcie na kazdej dlugosci: model niezalezny od dekodera (granice blokow zapisane przy generowaniu) */
static void test_truncation(const Voc *f, const Ref *ref, const char *name) {
    size_t cut; float *out = (float *)malloc((ref->frames + 4096) * ref->ch * sizeof(float));
    for (cut = 0; cut <= f->n; cut += (cut < f->bstart[0] + 20 || cut + 60 > f->n) ? 1u : 1u + rnd() % 17u) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; size_t got = 0; uint64_t expect = 0; unsigned j; int boundary = 0, any_audio_before = 0, short_audio = 0; maktabasauti_result_t r, rr;
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = f->b; s.n = cut; s.seekable = (int)(cut & 1); s.size_known = s.seekable && ((cut % 4) == 1); s.max_chunk = (cut % 3) ? 0 : 5;
        for (j = 0; j < f->nb; j++) {
            if (cut >= f->bend[j]) { expect = f->cum[j + 1]; if (cut == f->bend[j]) boundary = 1; if (f->bframes[j]) any_audio_before = 1; continue; }
            if (cut > f->bstart[j]) boundary = 0;
            if (f->bfb[j] && cut >= f->dstart[j]) short_audio = 1;                                                /* dane bloku audio urwane (naglowek bloku kompletny) */
            if (f->bfb[j] && cut > f->dstart[j]) { uint64_t fr = (cut - f->dstart[j]) / f->bfb[j]; if (fr > f->bframes[j]) fr = f->bframes[j]; expect = f->cum[j] + fr; }
            else expect = f->cum[j];
            if (f->bframes[j] && f->bfb[j] == 0 && cut > f->bstart[j]) expect = f->cum[j];                    /* cisza: naglowek ucięty = brak ramek (blok jest wazny dopiero w calosci) */
            if (f->bframes[j] && cut > f->bstart[j]) any_audio_before = any_audio_before || f->bfb[j] != 0;
            break;
        }
        if (cut == f->bstart[0]) boundary = 1;
        r = open_src(&s, &m, &d);
        if (cut < f->bstart[0] || (!any_audio_before && f->bfb[0] != 0 && cut <= f->dstart[0] && 0)) { CHECK(r != MAKTABASAUTI_OK && !d && s.closed == 0 && m.live == 0, "%s cut %zu: a cut header must fail cleanly (%d)", name, cut, (int)r); continue; }
        if (r != MAKTABASAUTI_OK) {                                                                           /* otwarcie moze zawiesc tylko wtedy, gdy nie ma jeszcze zadnego bloku audio z naglowkiem formatu */
            unsigned first_audio = 0; while (first_audio < f->nb && f->bfb[first_audio] == 0 && f->bframes[first_audio] == 0) first_audio++;
            CHECK(cut < f->dstart[first_audio < f->nb ? first_audio : 0] + (s.seekable ? 0u : 0u) + 1u + 3u, "%s cut %zu: open failed (%d) although the first audio block header is complete", name, cut, (int)r);
            CHECK(!d && s.closed == 0 && m.live == 0, "%s cut %zu: failed open must leave the input", name, cut);
            continue;
        }
        rr = drain(d, out, ref->ch, ref->frames + 4096, 211, &got);
        CHECK(got == expect && (rr == MAKTABASAUTI_EOF || rr == MAKTABASAUTI_E_TRUNCATED) && same(out, ref->pcm, got * ref->ch), "%s cut %zu: end %d, %zu/%llu frames (boundary %d)", name, cut, (int)rr, got, (unsigned long long)expect, boundary);
        if (boundary || cut == f->n) CHECK(rr == MAKTABASAUTI_EOF, "%s cut %zu: a cut on a block boundary is a clean end (%d)", name, cut, (int)rr);
        else if (!s.seekable) CHECK(rr == MAKTABASAUTI_E_TRUNCATED, "%s cut %zu: streaming mode: a cut inside a block must be E_TRUNCATED (%d)", name, cut, (int)rr);
        else CHECK(rr == (short_audio ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_EOF), "%s cut %zu: indexed mode: E_TRUNCATED only when audio data is missing (%d, short=%d)", name, cut, (int)rr, short_audio);
        if (rr == MAKTABASAUTI_E_TRUNCATED) {
            size_t g2 = 0; maktabasauti_result_t r2 = maktabasauti_decoder_read(d, out, 10, MAKTABASAUTI_FORMAT_F32, &g2);
            CHECK(r2 == MAKTABASAUTI_E_TRUNCATED && g2 == 0, "%s cut %zu: E_TRUNCATED must be sticky (%d)", name, cut, (int)r2);
            if (s.seekable && expect >= 2) {                                                                      /* seek czysci blad lepki */
                uint64_t act; size_t g3 = 0; maktabasauti_result_t r3 = maktabasauti_decoder_seek(d, 0, MAKTABASAUTI_SEEK_EXACT, &act);
                CHECK(r3 == MAKTABASAUTI_OK, "%s cut %zu: seek after E_TRUNCATED (%d)", name, cut, (int)r3);
                r3 = maktabasauti_decoder_read(d, out, 2, MAKTABASAUTI_FORMAT_F32, &g3);
                CHECK(r3 == MAKTABASAUTI_OK && g3 == 2 && same(out, ref->pcm, 2 * ref->ch), "%s cut %zu: read after a seek past a sticky E_TRUNCATED (%d, %zu)", name, cut, (int)r3, g3);
            }
        }
        maktabasauti_decoder_close(d);
        CHECK(m.live == 0 && s.closed == 1, "%s cut %zu: leak or close count %d", name, cut, s.closed);
    }
    free(out);
}

/* ---- czesci, nieobslugiwane bloki, pamiec ---- */
static void try_open_decode(const uint8_t *b, size_t n, int seekable, maktabasauti_result_t *r_out, size_t *frames_out, float *out, size_t cap) {
    Src s; Mem m; maktabasauti_decoder_t *d = NULL; size_t got = 0;
    memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = b; s.n = n; s.seekable = seekable; s.size_known = seekable; s.max_chunk = seekable ? 0 : 13;
    *r_out = open_src(&s, &m, &d); *frames_out = 0;
    if (*r_out == MAKTABASAUTI_OK) { (void)drain(d, out, 1, cap, 500, &got); *frames_out = got; maktabasauti_decoder_close(d); }
    else CHECK(!d && s.closed == 0, "failed open must leave the input with the caller");
    CHECK(m.live == 0 && m.bad == 0, "leak");
}
static void test_parts(void) {
    unsigned cnt; static float out[40000];
    for (cnt = 16384; cnt <= 16385; cnt++) {
        Buf x = { 0 }; unsigned i; maktabasauti_result_t r; size_t fr; voc_begin(&x);
        voc_hdr(&x, 1, 3); b8(&x, 156); b8(&x, 0); b8(&x, 77);
        for (i = 1; i < cnt; i++) { voc_hdr(&x, 2, 1); b8(&x, (unsigned)i); }
        b8(&x, 0);
        { SautiPcmInfo info; void *st; memset(&info, 0, sizeof info); st = VOC_OPS->open(x.b, x.n, &info); CHECK((st != NULL) == (cnt == 16384), "%u parts: the old adapter accepts exactly 16384 (%d)", cnt, st != NULL); if (st) VOC_OPS->close(st); }
        try_open_decode(x.b, x.n, 1, &r, &fr, out, 40000);
        CHECK((r == MAKTABASAUTI_OK) == (cnt == 16384), "%u parts, seekable: expected %s (%d)", cnt, cnt == 16384 ? "accepted" : "rejected", (int)r);
        if (cnt == 16384) CHECK(fr == cnt, "%u parts: %zu frames", cnt, fr);
        try_open_decode(x.b, x.n, 0, &r, &fr, out, 40000);
        CHECK(r == MAKTABASAUTI_OK && fr == cnt, "%u parts, non-seekable: the streaming mode has no part limit (%d, %zu frames)", cnt, (int)r, fr);
        free(x.b);
    }
}
/* limit blokow przy skanowaniu (2^20 naglowkow): 1048574 znacznikow + blok audio + koniec jest jeszcze przyjete, o jeden wiecej nie (dawny adapter nie mial limitu; to ochrona przed plikami z milionami pustych blokow) */
static void test_block_limit(void) {
    unsigned n; static float out[64];
    for (n = 1048574u; n <= 1048575u; n++) {
        Buf x = { 0 }; unsigned i; maktabasauti_result_t r; size_t fr;
        voc_begin(&x);
        for (i = 0; i < n; i++) voc_hdr(&x, 5, 0);
        voc_hdr(&x, 1, 6); b8(&x, 156); b8(&x, 0); rbytes(&x, 4); b8(&x, 0);
        try_open_decode(x.b, x.n, 1, &r, &fr, out, 64);
        if (n == 1048574u) CHECK(r == MAKTABASAUTI_OK && fr == 4, "%u marker blocks: accepted (%d, %zu frames)", n, (int)r, fr);
        else CHECK(r == MAKTABASAUTI_E_UNSUPPORTED_FEATURE, "%u marker blocks: the scan limit must reject it with E_UNSUPPORTED_FEATURE (%d)", n, (int)r);
        free(x.b);
    }
}
static void test_unsupported_blocks(void) {
    static const unsigned types[] = { 6, 7, 10, 11, 100, 255 }; size_t k; static float out[4096];
    for (k = 0; k < sizeof types / sizeof *types; k++) {
        unsigned where; for (where = 0; where < 2; where++) {
            Buf x = { 0 }; maktabasauti_result_t r; size_t fr; voc_begin(&x);
            if (where == 0) { voc_hdr(&x, types[k], 2); l16(&x, 1); }
            voc_hdr(&x, 1, 6); b8(&x, 156); b8(&x, 0); rbytes(&x, 4);
            if (where == 1) { voc_hdr(&x, types[k], 2); l16(&x, 1); }
            b8(&x, 0);
            try_open_decode(x.b, x.n, 1, &r, &fr, out, 4096);
            CHECK(r != MAKTABASAUTI_OK, "block type %u (%u): the seekable scan must reject it at open", types[k], where);
            try_open_decode(x.b, x.n, 0, &r, &fr, out, 4096);
            if (where == 0) CHECK(r != MAKTABASAUTI_OK, "block type %u before the audio: non-seekable open must fail", types[k]);
            else CHECK(r == MAKTABASAUTI_OK && fr == 4, "block type %u after the audio: the 4 frames before it are delivered (%d, %zu)", types[k], (int)r, fr);
            free(x.b);
        }
    }
    {   /* ADPCM (kodek != 0) */
        Buf x = { 0 }; maktabasauti_result_t r; size_t fr; voc_begin(&x); voc_hdr(&x, 1, 6); b8(&x, 156); b8(&x, 1); rbytes(&x, 4); b8(&x, 0);
        try_open_decode(x.b, x.n, 1, &r, &fr, out, 4096); CHECK(r == MAKTABASAUTI_E_UNSUPPORTED_FEATURE, "ADPCM must be E_UNSUPPORTED_FEATURE (%d)", (int)r);
        try_open_decode(x.b, x.n, 0, &r, &fr, out, 4096); CHECK(r == MAKTABASAUTI_E_UNSUPPORTED_FEATURE, "ADPCM (non-seekable) must be E_UNSUPPORTED_FEATURE (%d)", (int)r);
        free(x.b);
    }
    {   /* inna czestotliwosc w srodku */
        Buf x = { 0 }; maktabasauti_result_t r; size_t fr; voc_begin(&x); voc_hdr(&x, 1, 6); b8(&x, 156); b8(&x, 0); rbytes(&x, 4); voc_hdr(&x, 1, 6); b8(&x, 200); b8(&x, 0); rbytes(&x, 4); b8(&x, 0);
        try_open_decode(x.b, x.n, 1, &r, &fr, out, 4096); CHECK(r != MAKTABASAUTI_OK, "a rate change must be rejected at open (seekable)");
        try_open_decode(x.b, x.n, 0, &r, &fr, out, 4096); CHECK(r == MAKTABASAUTI_OK && fr == 4, "a rate change mid-stream: the frames before it are delivered (%d, %zu)", (int)r, fr);
        free(x.b);
    }
}
static void test_memory(void) {
    Voc v = make_voc(3, 0, 8); Buf cyc = { 0 }; Src s; Mem m; maktabasauti_decoder_t *d = NULL; static float out[4096 * 2]; size_t got, total = 0; maktabasauti_result_t r;
    bput(&cyc, v.b, v.n); free(v.b);
    memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
    s.gen = cyc.b; s.gen_len = cyc.n; s.gen_hdr = 26; s.gen_total = (uint64_t)100 << 20; s.max_chunk = 4096; s.n = cyc.n;
    r = open_src(&s, &m, &d); CHECK(r == MAKTABASAUTI_OK, "generator: open (%d)", (int)r);
    if (r == MAKTABASAUTI_OK) {
        for (;;) { r = maktabasauti_decoder_read(d, out, 4096, MAKTABASAUTI_FORMAT_F32, &got); if (r != MAKTABASAUTI_OK) { CHECK(r == MAKTABASAUTI_EOF || r == MAKTABASAUTI_E_TRUNCATED, "generator: code %d", (int)r); break; } total += got; }
        CHECK(total > 10000000u, "generator: decoded only %zu frames", total);
        CHECK(m.peak < 65536u, "generator: memory peak %zu bytes for an unbounded source", m.peak);
        maktabasauti_decoder_close(d);
    }
    CHECK(m.live == 0, "generator: leak");
    free(cyc.b);
}


/* ---- macierz blokow: dla plikow o cechach granicznych modul (wejscie przewijalne) przyjmuje dokladnie to, co dawny adapter, z tym samym PCM, takze w trybie strumieniowym ---- */
static int old_accepts(const uint8_t *b, size_t n) { SautiPcmInfo info; void *st; memset(&info, 0, sizeof info); st = VOC_OPS->open(b, n, &info); if (st) VOC_OPS->close(st); return st != NULL; }
static long g_agree;
static void agree_voc(const uint8_t *b, size_t n, const char *what) {
    const int o1 = old_accepts(b, n); static float oldf[65536 * 2], newf[65536 * 2]; maktabasauti_result_t r; size_t got;
    r = MAKTABASAUTI_OK; got = 0;
    try_open_decode(b, n, 1, &r, &got, newf, 65536 * 2);
    CHECK((r == MAKTABASAUTI_OK) == (o1 != 0), "%s: old %d, module (seekable) %d", what, o1, (int)r);
    if (o1) {
        SautiPcmInfo info; void *st; size_t g2, g3 = 0; maktabasauti_result_t r2;
        memset(&info, 0, sizeof info); st = VOC_OPS->open(b, n, &info);
        g2 = VOC_OPS->read(st, oldf, 65536 * 2 / info.channels > info.total_frames ? (size_t)info.total_frames : 65536 * 2 / info.channels);
        VOC_OPS->close(st);
        {   Src s; Mem m; maktabasauti_decoder_t *d = NULL; int mode;
            for (mode = 0; mode < 2; mode++) {
                memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = b; s.n = n; s.seekable = mode == 0; s.size_known = mode == 0; s.max_chunk = mode ? 7 : 0;
                r2 = open_src(&s, &m, &d);
                CHECK(r2 == MAKTABASAUTI_OK, "%s: the old adapter accepts, the module (%s) rejects (%d)", what, mode ? "non-seekable" : "seekable", (int)r2);
                if (r2 == MAKTABASAUTI_OK) { (void)drain(d, newf, info.channels, g2, 300, &g3); CHECK(g3 == g2 && same(newf, oldf, g2 * info.channels), "%s (%s): PCM differs (%zu vs %zu frames)", what, mode ? "non-seekable" : "seekable", g3, g2);
                    { size_t g4 = 9; maktabasauti_result_t r4 = maktabasauti_decoder_read(d, newf, 10, MAKTABASAUTI_FORMAT_F32, &g4); CHECK(r4 == MAKTABASAUTI_EOF && g4 == 0, "%s (%s): after the last frame the stream must end (%d, %zu frames)", what, mode ? "non-seekable" : "seekable", (int)r4, g4); }
                    maktabasauti_decoder_close(d); }
                CHECK(m.live == 0, "%s: leak", what);
            }
        }
        g_agree++;
    }
}
static void vblk(Buf *x, unsigned type, uint32_t len, const void *body, size_t nbody) { voc_hdr(x, type, len); if (nbody) bput(x, body, nbody); }
static void vt1(Buf *x, unsigned tc, unsigned codec, size_t nbytes) { voc_hdr(x, 1, (uint32_t)(2u + nbytes)); b8(x, tc); b8(x, codec); rbytes(x, nbytes); }
static void vt2(Buf *x, size_t nbytes) { voc_hdr(x, 2, (uint32_t)nbytes); rbytes(x, nbytes); }
static void vt3(Buf *x, uint32_t len, unsigned count, unsigned tc) { uint32_t k; voc_hdr(x, 3, len); if (len >= 2) l16(x, count); if (len >= 3) b8(x, tc); for (k = 3; k < len; k++) b8(x, 0); }
static void vt4(Buf *x, uint32_t len) { voc_hdr(x, 4, len); { uint32_t k; for (k = 0; k < len; k++) b8(x, 1); } }
static void vt5(Buf *x, uint32_t len) { voc_hdr(x, 5, len); rbytes(x, len); }
static void vt8(Buf *x, uint32_t len, unsigned tc16, unsigned b2, unsigned b3) { uint32_t k; voc_hdr(x, 8, len); if (len >= 2) l16(x, tc16); if (len >= 3) b8(x, b2); if (len >= 4) b8(x, b3); for (k = 4; k < len; k++) b8(x, 0); }
static void vt9(Buf *x, uint32_t len, uint32_t rate, unsigned bits, unsigned ch, unsigned codec, size_t ndata) {
    size_t k; voc_hdr(x, 9, len); if (len >= 4) l32(x, rate); if (len >= 5) b8(x, bits); if (len >= 6) b8(x, ch); if (len >= 8) l16(x, codec); for (k = 8; k < 12 && k < len; k++) b8(x, 0); if (len > 12) rbytes(x, ndata);
}
static void test_block_matrix(void) {
    unsigned id; static const uint32_t r9[] = { 999, 1000, 1001, 191999, 192000, 192001, 0, 22050 };
    for (id = 0; id < 91; id++) {
        Buf x = { 0 }; char what[40]; int term = 1; voc_begin(&x);
        switch (id) {
        case 0: vt1(&x, 156, 0, 20); break;
        case 1: vt1(&x, 156, 0, 0); vt1(&x, 156, 0, 8); break;  /* blok o dlugosci 2 przed poprawnym */
        case 2: vt1(&x, 156, 0, 1); break;
        case 3: vt1(&x, 156, 1, 10); break;                     /* ADPCM */
        case 4: vt1(&x, 250, 0, 10); break;
        case 5: vt1(&x, 251, 0, 10); break;
        case 6: vt1(&x, 0, 0, 10); break;
        case 7: case 8: case 9: case 10: case 11: case 12: case 13: case 14: vt9(&x, 13 + 3, r9[id - 7], 8, 1, 0, 4); break;
        case 15: vt9(&x, 17, 22050, 8, 0, 0, 5); break;        /* kanaly 0 */
        case 16: vt9(&x, 17, 22050, 8, 1, 0, 5); break;
        case 17: vt9(&x, 17, 22050, 8, 2, 0, 5); break;
        case 18: vt9(&x, 18, 22050, 8, 3, 0, 6); break;
        case 19: vt9(&x, 16, 22050, 8, 1, 0, 4); break;
        case 20: vt9(&x, 16, 22050, 8, 1, 4, 4); break;         /* 8 bit z kodekiem 4 */
        case 21: vt9(&x, 16, 22050, 16, 1, 4, 4); break;
        case 22: vt9(&x, 16, 22050, 16, 1, 0, 4); break;
        case 23: vt9(&x, 16, 22050, 12, 1, 0, 4); break;
        case 24: vt9(&x, 16, 22050, 16, 1, 3, 4); break;
        case 25: vt1(&x, 156, 0, 8); vt9(&x, 11, 10000, 8, 1, 0, 0); break;         /* dlugosc 11 po poprawnym bloku */
        case 26: vt1(&x, 156, 0, 8); vt9(&x, 12, 10000, 8, 1, 0, 0); break;         /* dlugosc 12: brak danych, ale dawny adapter odrzuca (< 13) */
        case 27: vt9(&x, 13, 22050, 8, 1, 0, 1); break;
        case 28: vt9(&x, 14, 22050, 16, 2, 4, 2); break;        /* (14-12) % 4 != 0 */
        case 29: vt9(&x, 18, 22050, 16, 2, 4, 6); break;
        case 30: vt9(&x, 20, 22050, 16, 2, 4, 8); break;
        case 31: vt1(&x, 156, 0, 8); vt1(&x, 156, 0, 8); break;
        case 32: vt1(&x, 156, 0, 8); vt1(&x, 200, 0, 8); break;
        case 33: vt9(&x, 16, 10000, 8, 1, 0, 4); vt1(&x, 156, 0, 8); break;
        case 34: vt9(&x, 20, 10000, 8, 2, 0, 8); vt1(&x, 156, 0, 8); break;     /* stereo, potem mono ta sama czestotliwosc */
        case 35: vt9(&x, 16, 10000, 8, 1, 0, 4); vt9(&x, 20, 10000, 8, 2, 0, 8); break;
        case 36: vt9(&x, 20, 10000, 8, 2, 0, 8); vt9(&x, 16, 10000, 8, 1, 0, 4); break;
        case 37: vt9(&x, 20, 10000, 8, 2, 0, 8); vt9(&x, 20, 10000, 8, 2, 0, 8); break;
        case 38: vt9(&x, 16, 10000, 8, 1, 0, 4); vt9(&x, 16, 10001, 8, 1, 0, 4); break;
        case 39: vt8(&x, 4, 62536, 0, 1); vt1(&x, 156, 0, 8); break;               /* stereo przez blok 8 */
        case 40: vt8(&x, 4, 62536, 0, 1); vt1(&x, 156, 0, 7); break;               /* nieparzysta liczba bajtow */
        case 41: vt8(&x, 4, 62536, 0, 0); vt1(&x, 156, 0, 7); break;               /* mono przez blok 8 */
        case 42: vt8(&x, 4, 62536, 1, 0); vt1(&x, 156, 0, 8); break;
        case 43: vt8(&x, 4, 62536, 0, 2); vt1(&x, 156, 0, 8); break;
        case 44: vt8(&x, 3, 62536, 0, 0); vt1(&x, 156, 0, 8); break;
        case 45: vt8(&x, 5, 62536, 0, 1); vt1(&x, 156, 0, 8); break;
        case 46: vt8(&x, 4, 62536, 0, 1); vt1(&x, 156, 0, 8); vt1(&x, 156, 0, 8); break;     /* drugi blok 1 po stereo: mono -> niezgodnosc */
        case 47: vt8(&x, 4, 62536, 0, 1); vt1(&x, 156, 0, 8); vt2(&x, 8); break;
        case 48: vt2(&x, 8); break;                                                   /* kontynuacja bez formatu */
        case 49: vt3(&x, 3, 5, 156); break;                                           /* sama cisza */
        case 50: vt3(&x, 3, 5, 156); vt2(&x, 8); break;                               /* kontynuacja po ciszy: brak last_width */
        case 51: vt3(&x, 2, 5, 156); break;
        case 52: vt3(&x, 4, 5, 156); break;
        case 53: vt1(&x, 156, 0, 8); vt3(&x, 3, 5, 150); break;                       /* inna czestotliwosc ciszy */
        case 54: vt1(&x, 156, 0, 8); vt3(&x, 3, 0, 156); break;                       /* cisza o 1 ramce */
        case 55: vt1(&x, 156, 0, 8); vt3(&x, 3, 65535, 156); break;                   /* 65536 ramek ciszy */
        case 56: vt9(&x, 20, 10000, 16, 2, 4, 8); vt2(&x, 6); break;                  /* kontynuacja: dlugosc niepodzielna przez 4 */
        case 57: vt9(&x, 20, 10000, 16, 2, 4, 8); vt2(&x, 8); break;
        case 58: vt1(&x, 156, 0, 8); vt2(&x, 0); break;
        case 59: vt1(&x, 156, 0, 8); vt4(&x, 1); break;
        case 60: vt1(&x, 156, 0, 8); vt4(&x, 2); break;
        case 61: vt1(&x, 156, 0, 8); vt4(&x, 3); break;
        case 62: vt1(&x, 156, 0, 8); vt5(&x, 0); break;
        case 63: vt1(&x, 156, 0, 8); vt5(&x, 7); vt2(&x, 4); break;
        case 64: vt5(&x, 3); vt1(&x, 156, 0, 8); break;
        case 65: vt4(&x, 2); vt1(&x, 156, 0, 8); break;
        case 66: vt1(&x, 156, 0, 8); b8(&x, 0); b8(&x, 77); b8(&x, 1); b8(&x, 0); b8(&x, 0); b8(&x, 5); term = 0; break;      /* koniec, potem smieci (ignorowane) */
        case 67: b8(&x, 0); term = 0; break;                                          /* sam koniec: brak audio */
        case 68: term = 0; break;                                                     /* brak blokow */
        case 69: vt5(&x, 9); break;                                                   /* same znaczniki */
        case 70: vt1(&x, 156, 0, 8); vt5(&x, 9); term = 0; break;                     /* bez konca */
        case 71: vt1(&x, 156, 0, 8); vblk(&x, 6, 2, "\x01\x00", 2); break;            /* powtorzenie */
        case 72: vt1(&x, 156, 0, 8); vblk(&x, 7, 0, NULL, 0); break;
        case 73: vt1(&x, 156, 0, 8); vblk(&x, 10, 2, "\x01\x00", 2); break;
        case 74: vt9(&x, 20, 44100, 16, 2, 4, 8); vt8(&x, 4, 62536, 0, 1); vt1(&x, 156, 0, 8); break;       /* blok 8 po audio, czestotliwosc inna */
        case 75: vt1(&x, 156, 0, 8); vt8(&x, 4, 62536, 0, 0); vt1(&x, 156, 0, 8); break;                      /* blok 8 mono o innej czestotliwosci */
        case 76: vt1(&x, 156, 0, 8); vt8(&x, 4, 62536, 0, 0); vt5(&x, 3); vt1(&x, 156, 0, 8); break;
        case 77: vt1(&x, 156, 0, 8); voc_hdr(&x, 1, 1); b8(&x, 5); term = 0; break;                            /* blok 1 o dlugosci 1 na samym koncu pliku */
        case 78: vt1(&x, 156, 0, 8); voc_hdr(&x, 9, 5); rbytes(&x, 5); term = 0; break;                       /* blok 9 skrocony */
        default: {
            /* naglowek pliku: rozmiar 25 / 26 / 27 / 30 (dopelnienie), suma kontrolna +-1 */
            const unsigned hv = id - 79; Buf y = { 0 }; size_t k;
            bput(&y, "Creative Voice File\x1a", 20);
            l16(&y, hv == 0 ? 25u : hv == 1 ? 26u : hv == 2 ? 27u : hv == 3 ? 30u : 26u); l16(&y, 0x010a); l16(&y, hv == 4 ? 0x1128u : hv == 5 ? 0x112au : 0x1129u);
            if (hv == 2) b8(&y, 0);
            if (hv == 3) { for (k = 0; k < 4; k++) b8(&y, 0); }
            { Buf blocks = { 0 }; vt1(&blocks, 156, 0, 8); bput(&y, blocks.b, blocks.n); free(blocks.b); }
            b8(&y, 0); free(x.b); x = y; term = 0;
        }
        }
        if (term) b8(&x, 0);
        snprintf(what, sizeof what, "VOC block case %u", id);
        agree_voc(x.b, x.n, what);
        free(x.b);
    }
    {   /* dlugosc gornego limitu czesci z blokami o 0 ramek (nie licza sie jako czesci) */
        Buf x = { 0 }; unsigned i; maktabasauti_result_t r; size_t fr; static float out[40000];
        voc_begin(&x); voc_hdr(&x, 1, 3); b8(&x, 156); b8(&x, 0); b8(&x, 77);
        for (i = 1; i < 16384; i++) { voc_hdr(&x, 2, 1); b8(&x, (unsigned)i); if (i % 100 == 0) { voc_hdr(&x, 2, 0); } }
        b8(&x, 0);
        { SautiPcmInfo info; void *st; memset(&info, 0, sizeof info); st = VOC_OPS->open(x.b, x.n, &info); CHECK(st != NULL, "16384 parts with empty continuation blocks: the old adapter accepts"); if (st) VOC_OPS->close(st); }
        try_open_decode(x.b, x.n, 1, &r, &fr, out, 40000); CHECK(r == MAKTABASAUTI_OK && fr == 16384, "empty continuation blocks do not count as parts (%d, %zu frames)", (int)r, fr);
        free(x.b);
    }
    {   /* sam naglowek pliku: probe */
        extern const maktabasauti_codec_t maktabasauti_voc_desc; const maktabasauti_codec_t *c = &maktabasauti_voc_desc; uint8_t h[26]; Buf y = { 0 };
        bput(&y, "Creative Voice File\x1a", 20); l16(&y, 26); l16(&y, 0x010a); l16(&y, 0x1129); memcpy(h, y.b, 26);
        CHECK(c->probe(c, h, 26, NULL) > 0 && c->probe(c, h, 25, NULL) == 0 && c->probe(c, NULL, 26, NULL) == 0, "VOC probe: length");
        h[20] = 25; CHECK(c->probe(c, h, 26, NULL) == 0, "VOC probe: header size 25"); h[20] = 26; h[24] ^= 1; CHECK(c->probe(c, h, 26, NULL) == 0, "VOC probe: checksum"); h[24] ^= 1; h[0] = 'X'; CHECK(c->probe(c, h, 26, NULL) == 0, "VOC probe: signature");
        free(y.b);
    }
    {   /* ujemny przypadek: prefiks krotszy niz 26 bajtow z kodekiem wymuszonym po nazwie */
        size_t n; uint8_t b[40]; size_t i;
        for (n = 0; n <= 30; n++) {
            Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r;
            for (i = 0; i < sizeof b; i++) b[i] = (uint8_t)rnd();
            if (n >= 20) memcpy(b, "Creative Voice File\x1a", 20);
            if (n >= 26) { b[20] = 26; b[21] = 0; b[22] = 0x0a; b[23] = 0x01; b[24] = 0x29; b[25] = 0x11; }
            memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = b; s.n = n; s.seekable = (int)(n & 1); s.size_known = s.seekable;
            g_force_codec = "creative-voc-pcm"; r = open_src(&s, &m, &d); g_force_codec = NULL;
            CHECK(r != MAKTABASAUTI_OK && !d && s.closed == 0 && m.live == 0, "VOC with %zu bytes: must fail cleanly (%d)", n, (int)r);
        }
    }
    printf("block matrix: old and new agree everywhere (%ld accepted files)\n", g_agree);
}

/* ---- mutacje naglowkow: wszystko, co przyjmuje dawny adapter, przyjmuje modul z tym samym PCM (oba tryby) ---- */
static void test_mutations(const Voc *base, const char *name, int rounds) {
    long both = 0, only_new = 0, rej = 0; int i; uint8_t *b = (uint8_t *)malloc(base->n); static float oldf[16384], newf[16384];
    for (i = 0; i < rounds; i++) {
        size_t k, n = base->n; int mode;
        memcpy(b, base->b, base->n);
        for (k = 0; k < 1u + rnd() % 3u; k++) {
            const size_t at = (rnd() % 3 == 0) ? rnd() % (base->n < 200 ? base->n : 200) : (base->bstart[rnd() % base->nb] + rnd() % 8u); const unsigned how = rnd() % 4u;
            if (at >= base->n) continue;
            if (how == 0) b[at] = (uint8_t)rnd(); else if (how == 1) b[at] ^= (uint8_t)(1u << (rnd() & 7u)); else if (how == 2) b[at] = (rnd() & 1) ? 0xff : 0x00; else b[at]++;
        }
        if (i % 9 == 0) n = rnd() % (base->n + 1);
        for (mode = 0; mode < 2; mode++) {
            Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r; SautiPcmInfo info; void *st; size_t got = 0;
            memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = b; s.n = n; s.seekable = mode == 0; s.size_known = mode == 0; s.max_chunk = mode ? 9 : 0;
            r = open_src(&s, &m, &d);
            memset(&info, 0, sizeof info); st = VOC_OPS->open(b, n, &info);
            if (st) {
                CHECK(r == MAKTABASAUTI_OK, "%s mutation (%s): the old adapter accepts, the module rejects (%d)", name, mode ? "non-seekable" : "seekable", (int)r);
                if (r == MAKTABASAUTI_OK) {
                    const size_t want = info.total_frames < 16384u / info.channels ? (size_t)info.total_frames : 16384u / info.channels; size_t g2;
                    maktabasauti_stream_info_t si; si.struct_size = sizeof si; (void)maktabasauti_decoder_get_info(d, &si);
                    CHECK(si.sample_rate == info.sample_rate && si.channels == info.channels, "%s mutation: parameters differ", name);
                    (void)drain(d, newf, info.channels, want, 300, &got); g2 = VOC_OPS->read(st, oldf, want);
                    CHECK(got == g2 && same(newf, oldf, g2 * info.channels), "%s mutation (%s): PCM differs (%zu vs %zu frames)", name, mode ? "non-seekable" : "seekable", got, g2);
                    both++;
                }
            } else if (r == MAKTABASAUTI_OK) only_new++; else rej++;
            if (st) VOC_OPS->close(st);
            if (r == MAKTABASAUTI_OK) { uint64_t act; (void)drain(d, newf, 1, 4096, 123, &got); (void)maktabasauti_decoder_seek(d, 3, MAKTABASAUTI_SEEK_EXACT, &act); (void)drain(d, newf, 1, 1000, 77, &got); maktabasauti_decoder_close(d); }
            else CHECK(!d && s.closed == 0, "%s mutation: failed open must leave the input with the caller", name);
            CHECK(m.live == 0 && m.bad == 0, "%s mutation: leak", name);
        }
    }
    printf("%s mutations: %d rounds, both accept %ld, only the module accepts %ld, rejected %ld\n", name, rounds, both, only_new, rej);
    free(b);
}
static void test_hostile_random(void) {
    size_t k; uint8_t b[300];
    for (k = 0; k < 4000; k++) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r; size_t i;
        for (i = 0; i < sizeof b; i++) b[i] = (uint8_t)rnd();
        memcpy(b, "Creative Voice File\x1a", 20); b[20] = 26; b[21] = 0; b[22] = 0x0a; b[23] = 0x01; b[24] = 0x29; b[25] = 0x11;
        if (k & 1) { b[26] = (uint8_t)(rnd() % 12); b[27] = (uint8_t)(rnd() % 40); b[28] = 0; b[29] = 0; }
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = b; s.n = (k % 5 == 0) ? rnd() % sizeof b : sizeof b; s.seekable = (int)(k & 2) != 0; s.size_known = s.seekable;
        r = open_src(&s, &m, &d);
        if (r == MAKTABASAUTI_OK) { static float out[4096]; size_t got; uint64_t act; (void)maktabasauti_decoder_read(d, out, 500, MAKTABASAUTI_FORMAT_F32, &got); (void)maktabasauti_decoder_seek(d, 7, MAKTABASAUTI_SEEK_EXACT, &act); (void)maktabasauti_decoder_read(d, out, 500, MAKTABASAUTI_FORMAT_F32, &got); maktabasauti_decoder_close(d); }
        else CHECK(!d && s.closed == 0, "hostile %zu: failed open must leave the input with the caller", k);
        CHECK(m.live == 0 && m.bad == 0, "hostile %zu: leak", k);
    }
}

int main(void) {
    static Voc cases[24]; static char names[24][48]; size_t nc = 0, i; unsigned mode, term, k;
    for (mode = 0; mode < 4; mode++) for (term = 0; term < 2; term++) for (k = 0; k < 3; k++) {
        const unsigned nblocks = k == 0 ? 1u : (k == 1 ? 4u : 12u);
        cases[nc] = make_voc(mode, (int)term, nblocks);
        snprintf(names[nc], sizeof names[nc], "voc mode%u %s %u block%s", mode, term ? "terminated" : "unterminated", nblocks, nblocks == 1 ? "" : "s");
        nc++;
    }
    for (i = 0; i < nc; i++) {
        Voc *c = &cases[i]; Ref ref = reference_bytes(c->b, c->n);
        printf("%s: %zu bytes, %zu frames x %zu ch @ %u Hz\n", names[i], c->n, ref.frames, ref.ch, ref.rate);
        CHECK(ref.frames == c->frames && ref.ch == c->ch, "%s: the model disagrees with the old adapter (%zu vs %llu frames)", names[i], ref.frames, (unsigned long long)c->frames);
        test_inputs(c, &ref, names[i]);
        test_block_sweep(c, &ref, names[i]);
        test_open_block(c, &ref, names[i]);
        test_seek(c, &ref, names[i]);
        test_truncation(c, &ref, names[i]);
        test_mutations(c, names[i], 80);
        free(ref.pcm);
    }
    test_block_matrix(); test_block_limit(); test_parts(); test_unsupported_blocks(); test_memory(); test_hostile_random();
    for (i = 0; i < nc; i++) free(cases[i].b);
    printf("VOC streaming: %s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}
