/* MaktabaSauti: STRUMIENIOWE dekodery WAV i AIFF (modul maktabasauti_pcm, src/decoders/modern/maktabasauti_pcm.c).
 *
 * Wzorzec PCM to dawne adaptery z calym buforem (sauti_decoder_wav_ops / sauti_decoder_aiff_ops) na tych samych bajtach.  Pliki sa generowane (WAV 8/16/24/32 bit, 1..8 kanalow, WAVE_FORMAT_EXTENSIBLE,
 * chunki LIST i o nieparzystym rozmiarze przed danymi, fmt za danymi; AIFF 8/16/24/32 bit, mono/stereo, AIFF-C NONE/sowt, COMM za SSND, przesuniecie SSND), wiec test nie zalezy od plikow uzytkownika.
 *   1. PCM bit w bit jak wzorzec: wejscie przewijalne ze znanym rozmiarem, nieprzewijalne bez rozmiaru, odczyty po 1/7 bajtow, losowe E_WOULD_BLOCK; dlugosc z naglowka, takze bez rozmiaru wejscia;
 *   2. E_WOULD_BLOCK w KAZDYM numerze wywolania wejscia (takze przy otwarciu): nic nie ginie;
 *   3. seek dokladny (losowe cele, 0, koniec, do tylu i do przodu), reset, seek poza koniec = E_OUT_OF_RANGE bez zmiany stanu;
 *   4. urwane dane na KAZDEJ dlugosci: ramki sprzed ucięcia identyczne z wzorcem, potem E_TRUNCATED; urwany naglowek = czysty blad;
 *   5. fmt/COMM za danymi: wejscie nieprzewijalne = E_NOT_SEEKABLE, bez zuzycia wejscia; duzy chunk przed danymi dziala bez seek;
 *   6. WAV zapisany strumieniowo (rozmiary 0 / 0xFFFFFFFF), WAV float 32/64 bit (NaN/Inf -> 0), pamiec: ~100 MB bez rozmiaru, szczyt < 64 KiB;
 *   7. mutacje naglowka: wszystko, co przyjmuje dawny adapter, przyjmuje tez modul i daje ten sam PCM; wrogie naglowki = czysty blad, bez wycieku. */
#define _POSIX_C_SOURCE 200809L
#include "maktabasauti/maktabasauti.h"
#include "maktabasauti/maktabasauti_codec.h"
#include "sauti_decoder_aiff.h"
#include "sauti_decoder_wav.h"
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
    const uint8_t *gen; size_t gen_len, gen_hdr; uint64_t gen_total, gen_done; long seeks, fail_seek_at;
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
    s->seeks++; if (s->fail_seek_at && s->seeks == s->fail_seek_at) return MAKTABASAUTI_E_IO;
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
static void bb16(Buf *x, unsigned v) { b8(x, (v >> 8) & 255u); b8(x, v & 255u); }
static void bb32(Buf *x, uint32_t v) { bb16(x, v >> 16); bb16(x, v & 0xffffu); }
static void tag(Buf *x, const char *t) { bput(x, t, 4); }
static void rbytes(Buf *x, size_t n) { size_t i; for (i = 0; i < n; i++) b8(x, rnd()); }
static void patch32le(Buf *x, size_t at, uint32_t v) { x->b[at] = (uint8_t)v; x->b[at + 1] = (uint8_t)(v >> 8); x->b[at + 2] = (uint8_t)(v >> 16); x->b[at + 3] = (uint8_t)(v >> 24); }
static void patch32be(Buf *x, size_t at, uint32_t v) { x->b[at] = (uint8_t)(v >> 24); x->b[at + 1] = (uint8_t)(v >> 16); x->b[at + 2] = (uint8_t)(v >> 8); x->b[at + 3] = (uint8_t)v; }

typedef struct { uint8_t *b; size_t n, data_off; unsigned ch, fb; uint64_t frames; int needs_seek; int aiff; const SautiDecoderOps *ops; } File;
typedef struct { unsigned ch, bits; uint32_t rate; uint32_t frames; size_t list_bytes; int odd_chunk, fmt_after, ext, trailing; } WavOpt;
static File make_wav(const WavOpt *o) {
    Buf x = { 0 }; File f; const unsigned fb = o->ch * (o->bits / 8u); const size_t data = (size_t)o->frames * fb;
    tag(&x, "RIFF"); l32(&x, 0); tag(&x, "WAVE");
    if (!o->fmt_after) {
        tag(&x, "fmt "); l32(&x, o->ext ? 40u : 16u); l16(&x, o->ext ? 0xfffeu : 1u); l16(&x, o->ch); l32(&x, o->rate); l32(&x, o->rate * fb); l16(&x, fb); l16(&x, o->bits);
        if (o->ext) { static const uint8_t g[12] = { 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71 }; l16(&x, 22); l16(&x, o->bits); l32(&x, 3); l16(&x, 1); l16(&x, 0); bput(&x, g, 12); }
    }
    if (o->list_bytes) { tag(&x, "LIST"); l32(&x, (uint32_t)o->list_bytes); rbytes(&x, o->list_bytes); if (o->list_bytes & 1u) b8(&x, 0); }
    if (o->odd_chunk) { tag(&x, "junk"); l32(&x, 5); rbytes(&x, 5); b8(&x, 0); }
    tag(&x, "data"); l32(&x, (uint32_t)data); f.data_off = x.n; rbytes(&x, data);
    if (data & 1u) b8(&x, 0);
    if (o->fmt_after) { tag(&x, "fmt "); l32(&x, 16); l16(&x, 1); l16(&x, o->ch); l32(&x, o->rate); l32(&x, o->rate * fb); l16(&x, fb); l16(&x, o->bits); }
    if (o->trailing) { tag(&x, "LIST"); l32(&x, 11); rbytes(&x, 11); b8(&x, 0); }
    patch32le(&x, 4, (uint32_t)(x.n - 8u));
    f.b = x.b; f.n = x.n; f.ch = o->ch; f.fb = fb; f.frames = o->frames; f.needs_seek = o->fmt_after; f.aiff = 0; f.ops = &sauti_decoder_wav_ops;
    return f;
}
static void put_ext80(Buf *x, uint32_t rate) {
    unsigned e = 31u; uint64_t mant;
    while (!((rate >> e) & 1u)) e--;
    mant = (uint64_t)rate << (63u - e);
    bb16(x, 16383u + e); bb32(x, (uint32_t)(mant >> 32)); bb32(x, (uint32_t)mant);
}
typedef struct { unsigned ch, bits; uint32_t rate, frames; int aifc, sowt, comm_after, offset, trailing; } AiffOpt;
static File make_aiff(const AiffOpt *o) {
    Buf x = { 0 }; File f; const unsigned w = o->bits / 8u, fb = o->ch * w; const size_t data = (size_t)o->frames * fb; const size_t comm = o->aifc ? 24u : 18u, ssnd = 8u + (size_t)o->offset + data;
    size_t k;
    tag(&x, "FORM"); bb32(&x, 0); tag(&x, o->aifc ? "AIFC" : "AIFF");
    for (k = 0; k < 2; k++) {
        const int do_comm = o->comm_after ? (k == 1) : (k == 0);
        if (do_comm) {
            tag(&x, "COMM"); bb32(&x, (uint32_t)comm); bb16(&x, o->ch); bb32(&x, o->frames); bb16(&x, o->bits); put_ext80(&x, o->rate);
            if (o->aifc) { tag(&x, o->sowt ? "sowt" : "NONE"); b8(&x, 0); b8(&x, 0); }
        } else {
            tag(&x, "SSND"); bb32(&x, (uint32_t)ssnd); bb32(&x, (uint32_t)o->offset); bb32(&x, 0); rbytes(&x, (size_t)o->offset); f.data_off = x.n; rbytes(&x, data);
            if (ssnd & 1u) b8(&x, 0);
        }
    }
    if (o->trailing) { tag(&x, "ANNO"); bb32(&x, 7); rbytes(&x, 7); b8(&x, 0); }
    patch32be(&x, 4, (uint32_t)(x.n - 8u));
    f.b = x.b; f.n = x.n; f.ch = o->ch; f.fb = fb; f.frames = o->frames; f.needs_seek = o->comm_after; f.aiff = 1; f.ops = &sauti_decoder_aiff_ops;
    return f;
}

typedef struct { float *pcm; size_t frames, ch; uint32_t rate; } Ref;
static Ref reference(const File *f) {
    Ref r; SautiPcmInfo info; void *st; size_t got = 0, cap, g;
    memset(&info, 0, sizeof info);
    st = f->ops->open(f->b, f->n, &info);
    assert(st); r.ch = info.channels; r.rate = info.sample_rate; cap = (size_t)info.total_frames;
    r.pcm = (float *)malloc(cap * r.ch * sizeof(float) + 16);
    while ((g = f->ops->read(st, r.pcm + got * r.ch, cap - got > 4096 ? 4096 : cap - got)) > 0) got += g;
    assert(got == cap); r.frames = got;
    f->ops->close(st);
    return r;
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

static void test_inputs(const File *f, const Ref *ref, const char *name) {
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
        if (f->needs_seek && !cfg[i].seekable) {                                       /* fmt / COMM za danymi: nie da sie bez cofniecia */
            CHECK(r == MAKTABASAUTI_E_NOT_SEEKABLE && !d && s.closed == 0 && m.live == 0, "%s %s: header after the data on a non-seekable input must be E_NOT_SEEKABLE (%d)", name, cfg[i].what, (int)r);
            if (f->n > 30000) CHECK(s.pos < f->n / 2, "%s %s: the failed open consumed %zu of %zu bytes (must stop at the data chunk)", name, cfg[i].what, s.pos, f->n);
            continue;
        }
        CHECK(r == MAKTABASAUTI_OK, "%s %s: open failed (%d)", name, cfg[i].what, (int)r);
        if (r != MAKTABASAUTI_OK) continue;
        s.armed = 1;
        info.struct_size = sizeof info; CHECK(maktabasauti_decoder_get_info(d, &info) == MAKTABASAUTI_OK, "info");
        CHECK(info.sample_rate == ref->rate && info.channels == ref->ch, "%s %s: stream parameters differ", name, cfg[i].what);
        CHECK(info.total_frames == (ref->frames ? ref->frames : MAKTABASAUTI_UNKNOWN_LENGTH), "%s %s: length %llu vs %zu", name, cfg[i].what, (unsigned long long)info.total_frames, ref->frames);
        CHECK((info.flags & MAKTABASAUTI_STREAM_LOSSLESS) != 0 && strcmp(info.codec_name, "pcm") == 0 && strcmp(info.container_name, f->aiff ? "aiff" : "wav") == 0, "%s %s: names/flags", name, cfg[i].what);
        if (!cfg[i].seekable) {
            CHECK(!(info.flags & MAKTABASAUTI_STREAM_SEEKABLE), "%s %s: must not claim to be seekable", name, cfg[i].what);
            CHECK(maktabasauti_decoder_seek(d, 0, MAKTABASAUTI_SEEK_EXACT, &act) == MAKTABASAUTI_E_NOT_SEEKABLE, "seek on non-seekable");
            CHECK(maktabasauti_decoder_reset(d) == MAKTABASAUTI_E_NOT_SEEKABLE, "reset on non-seekable");
        }
        out = (float *)malloc((ref->frames + 4096) * ref->ch * sizeof(float));
        rr = drain(d, out, ref->ch, ref->frames + 4096, 333 + i * 41, &got);
        CHECK(rr == MAKTABASAUTI_EOF && got == ref->frames && same(out, ref->pcm, got * ref->ch), "%s %s: end %d, %zu/%zu frames or PCM differs", name, cfg[i].what, (int)rr, got, ref->frames);
        free(out); maktabasauti_decoder_close(d);
        CHECK(s.closed == 1 && m.live == 0 && m.bad == 0, "%s %s: closed=%d live=%zu bad=%ld", name, cfg[i].what, s.closed, m.live, m.bad);
    }
}
/* E_WOULD_BLOCK w kazdym numerze wywolania wejscia po otwarciu: nic nie ginie (otwarcie przy wejsciu nieprzewijalnym nie jest wznawialne: patrz test_open_block) */
static void test_block_sweep(const File *f, const Ref *ref, const char *name) {
    long at;
    if (f->needs_seek) return;
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
/* E_WOULD_BLOCK podczas otwarcia (wejscie przewijalne: host cofa wejscie, ponowienie dziala; naglowek jest czytany z prefiksu przez peek) */
static void test_open_block(const File *f, const Ref *ref, const char *name) {
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
static void test_seek(const File *f, const Ref *ref, const char *name) {
    Src s; Mem m; maktabasauti_decoder_t *d = NULL; float *buf = (float *)malloc(2000 * ref->ch * sizeof(float)); size_t i; uint64_t act; maktabasauti_result_t r;
    memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
    if (!ref->frames) { free(buf); return; }
    s.data = f->b; s.n = f->n; s.seekable = 1; s.size_known = 1;
    r = open_src(&s, &m, &d); CHECK(r == MAKTABASAUTI_OK, "%s seek: open", name); if (r != MAKTABASAUTI_OK) { free(buf); return; }
    for (i = 0; i < 200; i++) {
        uint64_t target = i == 0 ? 0 : i == 1 ? ref->frames : i == 2 ? ref->frames - 1 : (rnd() % (ref->frames + 1)); size_t want = 1500, got = 0, avail;
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
      (void)maktabasauti_decoder_tell(d, &after); CHECK(before == after, "%s: a failed seek must not change the position (%llu -> %llu)", name, (unsigned long long)before, (unsigned long long)after); }
    CHECK(maktabasauti_decoder_reset(d) == MAKTABASAUTI_OK, "%s reset", name);
    { size_t got = 0; r = maktabasauti_decoder_read(d, buf, 100 < ref->frames ? 100 : ref->frames, MAKTABASAUTI_FORMAT_F32, &got); CHECK((r == MAKTABASAUTI_OK) && same(buf, ref->pcm, got * ref->ch), "%s after reset", name); }
    maktabasauti_decoder_close(d); free(buf);
    CHECK(m.live == 0, "%s seek: leak", name);
}
/* ucięcie na kazdej dlugosci (tylko pliki z fmt/COMM przed danymi): naglowek ucięty = czysty blad; potem ramki sprzed ucięcia i E_TRUNCATED, gdy brakuje danych */
static void test_truncation(const File *f, const Ref *ref, const char *name) {
    size_t cut; float *out; if (f->needs_seek) return;
    out = (float *)malloc((ref->frames + 4096) * ref->ch * sizeof(float));
    for (cut = 0; cut <= f->n; cut += (cut < f->data_off + 40 || cut + 80 > f->n) ? 1u : 1u + rnd() % 97u) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; size_t got = 0, expect; maktabasauti_result_t r, rr;
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = f->b; s.n = cut; s.seekable = (int)(cut & 1); s.size_known = (cut % 4) == 0 || (cut % 4) == 1; s.max_chunk = (cut % 3) ? 0 : 5;
        r = open_src(&s, &m, &d);
        if (cut < f->data_off) { CHECK(r == MAKTABASAUTI_E_UNSUPPORTED_FORMAT && !d && s.closed == 0 && m.live == 0, "%s cut %zu: a cut header must fail cleanly with E_UNSUPPORTED_FORMAT (%d)", name, cut, (int)r); continue; }
        CHECK(r == MAKTABASAUTI_OK, "%s cut %zu: open (%d)", name, cut, (int)r);
        if (r != MAKTABASAUTI_OK) continue;
        expect = (cut - f->data_off) / f->fb; if (expect > ref->frames) expect = (size_t)ref->frames;
        rr = drain(d, out, ref->ch, ref->frames + 4096, 211, &got);
        CHECK(got == expect && rr == (expect == ref->frames ? MAKTABASAUTI_EOF : MAKTABASAUTI_E_TRUNCATED) && same(out, ref->pcm, got * ref->ch), "%s cut %zu: end %d, %zu/%zu frames", name, cut, (int)rr, got, expect);
        if (rr == MAKTABASAUTI_E_TRUNCATED) {
            size_t g2 = 0; maktabasauti_result_t r2 = maktabasauti_decoder_read(d, out, 10, MAKTABASAUTI_FORMAT_F32, &g2);
            CHECK(r2 == MAKTABASAUTI_E_TRUNCATED && g2 == 0, "%s cut %zu: E_TRUNCATED must be sticky (%d)", name, cut, (int)r2);
            if (s.seekable && ref->frames >= 3) {                                                                 /* seek czysci blad lepki: ramki sprzed ucięcia znowu czytelne */
                uint64_t act; size_t g3 = 0; maktabasauti_result_t r3 = maktabasauti_decoder_seek(d, 0, MAKTABASAUTI_SEEK_EXACT, &act);
                CHECK(r3 == MAKTABASAUTI_OK, "%s cut %zu: seek after E_TRUNCATED (%d)", name, cut, (int)r3);
                if (expect >= 2) { r3 = maktabasauti_decoder_read(d, out, 2, MAKTABASAUTI_FORMAT_F32, &g3); CHECK(r3 == MAKTABASAUTI_OK && g3 == 2 && same(out, ref->pcm, 2 * ref->ch), "%s cut %zu: read after seek past a sticky E_TRUNCATED (%d, %zu)", name, cut, (int)r3, g3); }
            }
        }
        maktabasauti_decoder_close(d);
        CHECK(m.live == 0 && s.closed == 1, "%s cut %zu: leak or close count %d", name, cut, s.closed);
    }
    free(out);
}

/* ---- WAV zapisany strumieniowo i float ---- */
static File wav_with_sizes(const File *src, uint32_t riff, uint32_t data) { File f = *src; f.b = (uint8_t *)malloc(src->n + 1); memcpy(f.b, src->b, src->n); patch32le(&(Buf){ f.b, f.n, f.n }, 4, riff); patch32le(&(Buf){ f.b, f.n, f.n }, 40, data); return f; }
static void test_placeholder(void) {
    const WavOpt o = { 2, 16, 22050, 5000, 0, 0, 0, 0, 0 }; size_t variant;
    for (variant = 0; variant < 5; variant++) {
        /* 0: oba 0xFFFFFFFF, 1: oba 0, 2: RIFF prawdziwy + data 0xFFFFFFFF (niespojne: odrzucone jak dawniej), 3: RIFF 0xFFFFFFFF + data prawdziwy, 4: jak 0, ale z jednym zbednym bajtem na koncu */
        File g = make_wav(&o), f; Ref ref = reference(&g); const uint32_t real_riff = (uint32_t)(g.n - 8u), real_data = 5000u * 4u; size_t pass; const int bounded = variant == 3;
        f = wav_with_sizes(&g, variant == 1 || variant == 4 ? 0u : variant == 2 ? real_riff : 0xffffffffu, variant == 1 || variant == 4 ? 0u : variant == 3 ? real_data : 0xffffffffu);
        if (variant == 4) { f.b = (uint8_t *)realloc(f.b, f.n + 1); f.b[f.n++] = 0x55; }
        for (pass = 0; pass < 4; pass++) {                                                           /* 0: nieprzewijalne bez rozmiaru, 1: z rozmiarem, 2: przewijalne z rozmiarem, 3: przewijalne bez rozmiaru */
            Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info; float *out; size_t got = 0; maktabasauti_result_t r, rr; uint64_t act;
            memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
            s.data = f.b; s.n = f.n; s.seekable = pass >= 2; s.size_known = pass == 1 || pass == 2; s.max_chunk = 17;
            r = open_src(&s, &m, &d);
            if (variant == 2) { CHECK(r == MAKTABASAUTI_E_UNSUPPORTED_FORMAT && !d && s.closed == 0 && m.live == 0, "placeholder 2/%zu: an inconsistent data size must be rejected (%d)", pass, (int)r); continue; }
            CHECK(r == MAKTABASAUTI_OK, "placeholder %zu/%zu: open (%d)", variant, pass, (int)r);
            if (r != MAKTABASAUTI_OK) continue;
            info.struct_size = sizeof info; (void)maktabasauti_decoder_get_info(d, &info);
            CHECK(info.total_frames == (((pass == 1 || pass == 2) || bounded) ? ref.frames : MAKTABASAUTI_UNKNOWN_LENGTH), "placeholder %zu/%zu: length %llu", variant, pass, (unsigned long long)info.total_frames);
            out = (float *)malloc((ref.frames + 4096) * ref.ch * sizeof(float));
            rr = drain(d, out, ref.ch, ref.frames + 4096, 401, &got);
            CHECK(rr == MAKTABASAUTI_EOF && got == ref.frames && same(out, ref.pcm, got * ref.ch), "placeholder %zu/%zu: end %d, %zu/%zu frames", variant, pass, (int)rr, got, ref.frames);
            if (pass >= 2) { r = maktabasauti_decoder_seek(d, 1234, MAKTABASAUTI_SEEK_EXACT, &act); CHECK(r == MAKTABASAUTI_OK && act == 1234, "placeholder seek"); got = 0; (void)maktabasauti_decoder_read(d, out, 50, MAKTABASAUTI_FORMAT_F32, &got); CHECK(got == 50 && same(out, ref.pcm + 1234 * ref.ch, 50 * ref.ch), "placeholder PCM after seek"); }
            free(out); maktabasauti_decoder_close(d);
            CHECK(m.live == 0, "placeholder: leak");
        }
        free(ref.pcm); free(f.b); free(g.b);
    }
}
static File make_float_wav(unsigned bits, unsigned ch, unsigned frames, int ext, float *expect) {
    Buf x = { 0 }; File f; const unsigned w = bits / 8u, fb = ch * w; size_t i;
    tag(&x, "RIFF"); l32(&x, 0); tag(&x, "WAVE"); tag(&x, "fmt "); l32(&x, ext ? 40u : 16u); l16(&x, ext ? 0xfffeu : 3u); l16(&x, ch); l32(&x, 48000); l32(&x, 48000 * fb); l16(&x, fb); l16(&x, bits);
    if (ext) { static const uint8_t g[12] = { 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71 }; l16(&x, 22); l16(&x, bits); l32(&x, 3); l16(&x, 3); l16(&x, 0); bput(&x, g, 12); }
    tag(&x, "data"); l32(&x, frames * fb); f.data_off = x.n;
    for (i = 0; i < (size_t)frames * ch; i++) {
        float v = (float)((int)(rnd() % 20001u) - 10000) / 7000.0f;
        if (i % 11 == 3) v = NAN;
        else if (i % 13 == 5) v = INFINITY;
        else if (i % 17 == 7) v = -INFINITY;
        expect[i] = isfinite(v) ? v : 0.0f;
        if (bits == 32) { uint32_t u; memcpy(&u, &v, 4); l32(&x, u); } else { double dv = v; uint64_t u; memcpy(&u, &dv, 8); l32(&x, (uint32_t)u); l32(&x, (uint32_t)(u >> 32)); }
    }
    patch32le(&x, 4, (uint32_t)(x.n - 8u));
    f.b = x.b; f.n = x.n; f.ch = ch; f.fb = fb; f.frames = frames; f.needs_seek = 0; f.aiff = 0; f.ops = NULL;
    return f;
}
static void test_float(void) {
    unsigned bits, ch, ext;
    for (bits = 32; bits <= 64; bits += 32) for (ch = 1; ch <= 3; ch += 2) for (ext = 0; ext < 2; ext++) {
        static float expect[3 * 2000], out[3 * 2000 + 64]; File f = make_float_wav(bits, ch, 2000, (int)ext, expect);
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; size_t got = 0; maktabasauti_result_t r, rr; SautiPcmInfo info; void *st;
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = f.b; s.n = f.n; s.max_chunk = 29;
        r = open_src(&s, &m, &d);
        CHECK(r == MAKTABASAUTI_OK, "float %u/%u/%u: open (%d)", bits, ch, ext, (int)r);
        if (r == MAKTABASAUTI_OK) {
            rr = drain(d, out, ch, 2000 + 32, 311, &got);
            CHECK(rr == MAKTABASAUTI_EOF && got == 2000 && same(out, expect, 2000 * ch), "float %u/%u/%u: end %d, %zu frames or PCM differs", bits, ch, ext, (int)rr, got);
            maktabasauti_decoder_close(d);
        }
        CHECK(m.live == 0, "float: leak");
        memset(&info, 0, sizeof info); st = sauti_decoder_wav_ops.open(f.b, f.n, &info);
        CHECK(st == NULL, "the old adapter rejects float WAV (documented difference); if it ever accepts it, this test must compare against it");
        if (st) sauti_decoder_wav_ops.close(st);
        free(f.b);
    }
}

/* ---- duzy chunk przed danymi, wiele chunkow, pamiec ---- */
static void test_big_chunk(void) {
    const WavOpt o = { 2, 16, 44100, 3000, 9001, 1, 0, 0, 1 }, ob = { 2, 16, 44100, 3000, 40001, 1, 0, 0, 1 }; const AiffOpt a = { 2, 16, 44100, 3000, 0, 0, 0, 0, 1 }; File f[3]; size_t k, cfg;
    f[0] = make_wav(&o); f[1] = make_aiff(&a); f[2] = make_wav(&ob);   /* f[2]: LIST dluzszy niz okno czytnika (16 KiB), wiec seek naprawde dociera do wejscia */
    { Buf x = { 0 }; /* WAV: 300 malych chunkow przed danymi */
      const File w = make_wav(&(WavOpt){ 1, 8, 8000, 900, 0, 0, 0, 0, 0 }); size_t i; tag(&x, "RIFF"); l32(&x, 0); tag(&x, "WAVE"); bput(&x, w.b + 12, 24);
      for (i = 0; i < 300; i++) { tag(&x, "junk"); l32(&x, 3); rbytes(&x, 3); b8(&x, 0); }
      tag(&x, "data"); l32(&x, 900); rbytes(&x, 900); patch32le(&x, 4, (uint32_t)(x.n - 8u));
      free(f[0].b); f[0].b = x.b; f[0].n = x.n; f[0].data_off = x.n - 900u; f[0].ch = 1; f[0].fb = 1; f[0].frames = 900; free(w.b); }
    for (k = 0; k < 3; k++) {
        Ref ref = reference(&f[k]);
        for (cfg = 0; cfg < 3; cfg++) {
            Src s; Mem m; maktabasauti_decoder_t *d = NULL; float *out; size_t got = 0; maktabasauti_result_t r, rr;
            memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = f[k].b; s.n = f[k].n; s.seekable = cfg == 2; s.size_known = cfg != 0; s.max_chunk = cfg == 1 ? 13 : 0;
            if (cfg == 2) {                                                                                      /* blad seek w czasie otwarcia (seek poza oknem czytnika dociera do wejscia) */
                long at;
                for (at = 1; at <= 3; at++) {
                    Src s2; Mem m2; maktabasauti_decoder_t *d2 = NULL; maktabasauti_result_t r2;
                    memset(&s2, 0, sizeof s2); memset(&m2, 0, sizeof m2); s2.data = f[k].b; s2.n = f[k].n; s2.seekable = 1; s2.size_known = 1; s2.fail_seek_at = at;
                    r2 = open_src(&s2, &m2, &d2);
                    if (r2 == MAKTABASAUTI_OK) { maktabasauti_decoder_close(d2); CHECK(s2.seeks < at, "big chunk %zu: seek #%ld failed but the open succeeded", k, at); }
                    else CHECK(!d2 && s2.closed == 0 && m2.live == 0, "big chunk %zu: seek failure #%ld must leave the input with the caller", k, at);
                    if (at == 1 && k == 2) CHECK(s2.seeks >= 1, "big chunk %zu: the file is expected to need at least one seek during the open (%ld)", k, s2.seeks);
                }
            }
            r = open_src(&s, &m, &d); CHECK(r == MAKTABASAUTI_OK, "big chunk %zu/%zu: open (%d)", k, cfg, (int)r); if (r != MAKTABASAUTI_OK) continue;
            out = (float *)malloc((ref.frames + 4096) * ref.ch * sizeof(float));
            rr = drain(d, out, ref.ch, ref.frames + 4096, 500, &got);
            CHECK(rr == MAKTABASAUTI_EOF && got == ref.frames && same(out, ref.pcm, got * ref.ch), "big chunk %zu/%zu: end %d, %zu/%zu frames or PCM differs", k, cfg, (int)rr, got, ref.frames);
            free(out); maktabasauti_decoder_close(d); CHECK(m.live == 0, "big chunk: leak");
        }
        free(ref.pcm); free(f[k].b);
    }
}
static void test_memory(void) {
    const WavOpt o = { 1, 16, 44100, 0, 0, 0, 0, 0, 0 }; File f = make_wav(&o); Buf cyc = { 0 }; Src s; Mem m; maktabasauti_decoder_t *d = NULL; static float out[4096]; size_t got, total = 0, i; maktabasauti_result_t r;
    patch32le(&(Buf){ f.b, f.n, f.n }, 4, 0xffffffffu); patch32le(&(Buf){ f.b, f.n, f.n }, 40, 0xffffffffu);
    bput(&cyc, f.b, f.n); for (i = 0; i < 1000; i++) b8(&cyc, rnd()); free(f.b);
    memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
    s.gen = cyc.b; s.gen_len = cyc.n; s.gen_hdr = 44; s.gen_total = (uint64_t)100 << 20; s.max_chunk = 4096; s.n = cyc.n;
    r = open_src(&s, &m, &d); CHECK(r == MAKTABASAUTI_OK, "generator: open (%d)", (int)r);
    if (r == MAKTABASAUTI_OK) {
        for (;;) { r = maktabasauti_decoder_read(d, out, 4096, MAKTABASAUTI_FORMAT_F32, &got); if (r != MAKTABASAUTI_OK) { CHECK(r == MAKTABASAUTI_EOF, "generator: code %d", (int)r); break; } total += got; }
        CHECK(total > 50000000u, "generator: decoded only %zu frames", total);
        CHECK(m.peak < 65536u, "generator: memory peak %zu bytes for an unbounded source", m.peak);
        maktabasauti_decoder_close(d);
    }
    CHECK(m.live == 0, "generator: leak");
    free(cyc.b);
}


/* ---- macierz naglowkow: dla plikow o cechach granicznych modul przyjmuje dokladnie to, co dawny adapter (przewijalne wejscie ze znanym rozmiarem) ---- */
static int old_accepts(const File *f) { SautiPcmInfo info; void *st; memset(&info, 0, sizeof info); st = f->ops->open(f->b, f->n, &info); if (st) f->ops->close(st); return st != NULL; }
static int new_accepts(const File *f) {
    Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r;
    memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = f->b; s.n = f->n; s.seekable = 1; s.size_known = 1;
    r = open_src(&s, &m, &d); if (r == MAKTABASAUTI_OK) maktabasauti_decoder_close(d);
    CHECK(m.live == 0 && (r == MAKTABASAUTI_OK || (!d && s.closed == 0)), "matrix: leak or input taken after a failed open");
    return r == MAKTABASAUTI_OK;
}
static void put_ext80_raw(Buf *x, unsigned exp, uint64_t mant) { bb16(x, exp); bb32(x, (uint32_t)(mant >> 32)); bb32(x, (uint32_t)mant); }
static void put_ext80_double(Buf *x, double v) { int e; const double m = frexp(v, &e); put_ext80_raw(x, (unsigned)(e - 1 + 16383), (uint64_t)ldexp(m, 64)); }
/* wzorzec = dawny adapter: oba przyjmuja albo oba odrzucaja; gdy oba przyjmuja i `compare`, PCM tez jest ten sam */
static void agree_on(const File *f, const char *what, int compare, long *agree) {
    const int o1 = old_accepts(f), n1 = new_accepts(f);
    CHECK(o1 == n1, "%s: old %d, module %d", what, o1, n1);
    if (o1 && n1 && compare) {
        Ref ref = reference(f); Src s; Mem m; maktabasauti_decoder_t *d = NULL; size_t got = 0; float *out = (float *)malloc((ref.frames + 64) * ref.ch * sizeof(float));
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = f->b; s.n = f->n; s.seekable = 1; s.size_known = 1;
        if (open_src(&s, &m, &d) == MAKTABASAUTI_OK) { (void)drain(d, out, ref.ch, ref.frames + 64, 100, &got); CHECK(got == ref.frames && same(out, ref.pcm, got * ref.ch), "%s: PCM differs", what); maktabasauti_decoder_close(d); }
        free(out); free(ref.pcm);
    }
    if (o1) (*agree)++;
}
static void wav_fmt_chunk(Buf *x, unsigned size, unsigned format, unsigned ch, uint32_t rate, unsigned bits) {
    tag(x, "fmt "); l32(x, size); l16(x, format); l16(x, ch); l32(x, rate); l32(x, rate * ch * (bits / 8u)); l16(x, ch * (bits / 8u)); l16(x, bits);
}
static File wav_variant(unsigned v) {
    Buf x = { 0 }; File f; static const uint8_t g[12] = { 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71 }; const size_t data = 32; unsigned valid = 16, cb = 22, rsv = 0, fsz = 40;
    tag(&x, "RIFF"); l32(&x, 0); tag(&x, "WAVE");
    if (v == 0) wav_fmt_chunk(&x, 16, 1, 2, 44100, 16);                                          /* dwa fmt przed danymi */
    if (v == 1) { tag(&x, "fmt "); l32(&x, 15); l16(&x, 1); l16(&x, 2); l32(&x, 44100); l32(&x, 176400); l16(&x, 4); b8(&x, 16); b8(&x, 0); }
    else if (v >= 2 && v <= 7) {
        if (v == 4) valid = 0;
        else if (v == 5) valid = 17;
        else if (v == 7) cb = 21;
        else if (v == 6) fsz = 16;
        else rsv = v;
        wav_fmt_chunk(&x, fsz, 0xfffeu, 2, 44100, 16);
        if (fsz == 40) { l16(&x, cb); l16(&x, valid); l32(&x, 3); l16(&x, 1); b8(&x, rsv == 2 ? 7 : 0); b8(&x, rsv == 3 ? 7 : 0); bput(&x, g, 12); }
    } else if (v == 8) wav_fmt_chunk(&x, 16, 1, 2, 44100, 16);
    else if (v == 9) { /* data, data, fmt: pierwszy data wygrywa */ tag(&x, "data"); l32(&x, 32); rbytes(&x, 32); tag(&x, "data"); l32(&x, 32); rbytes(&x, 32); wav_fmt_chunk(&x, 16, 1, 2, 44100, 16); }
    else wav_fmt_chunk(&x, 16, 1, 2, 44100, 16);
    if (v != 9) { tag(&x, "data"); l32(&x, (uint32_t)data); rbytes(&x, data); }
    f.data_off = 0; f.b = x.b; f.n = x.n; f.ops = &sauti_decoder_wav_ops;
    patch32le(&x, 4, (uint32_t)(x.n - 8u)); f.b = x.b;
    return f;
}
static File aiff_variant(unsigned v, double rate, unsigned exp_override, uint64_t mant_override, int raw_rate) {
    Buf x = { 0 }; File f; const unsigned frames = v == 6 ? 0u : 10u, fb = 4u; size_t data = (size_t)frames * fb; int aifc = v == 2; size_t comm = aifc ? 24u : 18u, ssnd = 8u + data;
    const uint32_t off = 0;
    if (v == 1) comm = 17u;
    else if (v == 2) comm = 21u;
    else if (v == 3) ssnd = 7u;
    else if (v == 4) ssnd = 8u + data + 1000u;
    tag(&x, "FORM"); bb32(&x, 0); tag(&x, aifc ? "AIFC" : "AIFF");
    if (v == 7) { tag(&x, "APPL"); bb32(&x, 5); rbytes(&x, 5); b8(&x, 0); }
    if (v != 8 && v != 10) {
        const unsigned reps = v == 9 ? 2u : 1u; unsigned k;
        for (k = 0; k < reps; k++) {
            tag(&x, "COMM"); bb32(&x, (uint32_t)comm); bb16(&x, 2); bb32(&x, frames); bb16(&x, 16);
            if (raw_rate == 1) put_ext80_raw(&x, exp_override, mant_override); else put_ext80_double(&x, rate);
            if (comm == 17u) { x.n -= 1; }                                                                   /* ciało o 1 bajt krótsze */
            if (aifc && comm >= 22u) { tag(&x, "NONE"); b8(&x, 0); b8(&x, 0); }
            if (aifc && comm == 21u) bput(&x, "NON", 3);
            if (comm & 1u) b8(&x, 0);
        }
    }
    {   const unsigned reps = v == 10 ? 2u : 1u; unsigned k;
        for (k = 0; k < reps; k++) {
            const size_t actual = v == 4 ? 8u + data : ssnd; size_t written = 4;                           /* v4: rozmiar w naglowku wiekszy niz tresc */
            tag(&x, "SSND"); bb32(&x, (uint32_t)ssnd); bb32(&x, v == 5 ? (uint32_t)(ssnd + 8u) : off);
            if (actual >= 8u) { bb32(&x, 0); written = 8; }
            while (written < actual) { b8(&x, rnd()); written++; }
            if (actual & 1u) b8(&x, 0);
        }
    }
    if (v == 8 || v == 10) { tag(&x, "COMM"); bb32(&x, 18); bb16(&x, 2); bb32(&x, frames); bb16(&x, 16); if (raw_rate == 1) put_ext80_raw(&x, exp_override, mant_override); else put_ext80_double(&x, rate); }
    patch32be(&x, 4, (uint32_t)(x.n - 8u));
    f.b = x.b; f.n = x.n; f.data_off = 0; f.ops = &sauti_decoder_aiff_ops;
    return f;
}
static void test_header_matrix(void) {
    static const unsigned chs[] = { 0, 1, 2, 31, 32, 33 }, bitsv[] = { 0, 7, 8, 12, 16, 24, 32, 40 }; static const uint32_t rates[] = { 0, 1, 44100 };
    static const uint32_t arates[] = { 0, 500, 999, 1000, 1001, 44100, 383999, 384000, 384001, 1000000 }; static const unsigned achs[] = { 0, 1, 2, 3 }, abits[] = { 0, 8, 12, 16, 24, 32, 33 };
    size_t a, b, c, d, e; long agree = 0; unsigned v;
    for (a = 0; a < sizeof chs / sizeof *chs; a++) for (b = 0; b < sizeof bitsv / sizeof *bitsv; b++) for (c = 0; c < 3; c++) for (d = 0; d < 2; d++) for (e = 0; e < 4; e++) {
        const unsigned ch = chs[a], bits = bitsv[b]; File f; Buf x = { 0 }; const unsigned good_align = ch * (bits / 8u), align = good_align + d; const size_t data = (good_align ? good_align : 8u) * 4u + (e == 1 ? 1u : 0u);
        char what[96];
        tag(&x, "RIFF"); l32(&x, 0); tag(&x, "WAVE"); tag(&x, "fmt "); l32(&x, 16); l16(&x, 1); l16(&x, ch); l32(&x, rates[c]); l32(&x, rates[c] * good_align); l16(&x, align); l16(&x, bits);
        tag(&x, "data"); l32(&x, (uint32_t)data); f.data_off = x.n; rbytes(&x, data); patch32le(&x, 4, e == 3 ? 2u : (uint32_t)(x.n - 8u - (e == 2 ? 4u : 0u)));
        f.b = x.b; f.n = x.n; f.ops = &sauti_decoder_wav_ops;
        snprintf(what, sizeof what, "WAV matrix ch=%u bits=%u rate=%u align=%u data=%zu variant=%zu", ch, bits, (unsigned)rates[c], align, data, e);
        agree_on(&f, what, 0, &agree);
        free(x.b);
    }
    for (v = 0; v <= 9; v++) { File f = wav_variant(v); char what[48]; snprintf(what, sizeof what, "WAV variant %u", v); agree_on(&f, what, v == 8 || v == 9, &agree); free(f.b); }
    for (a = 0; a < sizeof achs / sizeof *achs; a++) for (b = 0; b < sizeof abits / sizeof *abits; b++) for (c = 0; c < sizeof arates / sizeof *arates; c++) for (d = 0; d < 4; d++) for (e = 0; e < 3; e++) {
        const unsigned ch = achs[a], bits = abits[b]; File f; Buf x = { 0 }; const int aifc = d & 1; const unsigned fb = ch * (bits / 8u); const uint32_t frames = 10u; char what[96];
        const size_t base = (size_t)frames * fb, data = base - (e == 1 && base ? 1u : 0u) + (e == 2 ? (size_t)fb : 0u); uint32_t off = (d & 2) ? 1u : 0u;
        tag(&x, "FORM"); bb32(&x, 0); tag(&x, aifc ? "AIFC" : "AIFF");
        tag(&x, "COMM"); bb32(&x, aifc ? 24u : 18u); bb16(&x, ch); bb32(&x, frames); bb16(&x, bits);
        if (arates[c]) put_ext80_double(&x, (double)arates[c]); else { size_t z; for (z = 0; z < 10; z++) b8(&x, 0); }
        if (aifc) { tag(&x, (d & 2) ? "fl32" : "NONE"); b8(&x, 0); b8(&x, 0); }
        tag(&x, "SSND"); bb32(&x, (uint32_t)(8u + off + data)); bb32(&x, off); bb32(&x, 0); rbytes(&x, off + data); if ((8u + off + data) & 1u) b8(&x, 0);
        patch32be(&x, 4, (uint32_t)(x.n - 8u));
        f.b = x.b; f.n = x.n; f.ops = &sauti_decoder_aiff_ops;
        snprintf(what, sizeof what, "AIFF matrix ch=%u bits=%u rate=%u kind=%zu data-case=%zu", ch, bits, (unsigned)arates[c], d, e);
        agree_on(&f, what, 0, &agree);
        free(x.b);
    }
    for (v = 0; v <= 10; v++) { File f = aiff_variant(v, 44100.0, 0, 0, 0); char what[48]; snprintf(what, sizeof what, "AIFF variant %u", v); agree_on(&f, what, v == 0 || v == 7 || v == 8, &agree); free(f.b); }
    {   static const double nonint[] = { 44100.5, 44100.005, 44100.02, 44099.996, 8000.25 }; size_t i;
        for (i = 0; i < sizeof nonint / sizeof *nonint; i++) { File f = aiff_variant(0, nonint[i], 0, 0, 0); char what[64]; snprintf(what, sizeof what, "AIFF rate %.3f", nonint[i]); agree_on(&f, what, 1, &agree); free(f.b); }
        {   static const struct { unsigned exp; uint64_t mant; } raw[] = { { 0x400e, UINT64_C(0xac44000000000000) }, { 0xc00e, UINT64_C(0xac44000000000000) }, { 0x400e, UINT64_C(0x2c44000000000000) }, { 0, UINT64_C(0xac44000000000000) }, { 0x7fff, UINT64_C(0xac44000000000000) }, { 0x7ffe, UINT64_C(0xac44000000000000) }, { 0x400e, UINT64_C(0xac44000000000001) } };
            for (i = 0; i < sizeof raw / sizeof *raw; i++) { File f = aiff_variant(0, 0.0, raw[i].exp, raw[i].mant, 1); char what[64]; snprintf(what, sizeof what, "AIFF raw rate %zu", i); agree_on(&f, what, 1, &agree); free(f.b); }
        }
    }
    printf("header matrix: old and new agree everywhere (%ld accepted files)\n", agree);
}

/* wejscia o dlugosci 0..11 bajtow z kodekiem wymuszonym po nazwie (sonda jest pomijana, wiec open dostaje krotki prefiks): czysty blad, bez czytania niezainicjowanych bajtow */
static void test_tiny_and_probe(void) {
    static const char *const codecs[] = { "wav", "aiff" }; size_t c, n; uint8_t b[16];
    extern const maktabasauti_codec_t maktabasauti_pcm_wav_desc, maktabasauti_pcm_aiff_desc;
    for (c = 0; c < 2; c++) for (n = 0; n <= 14; n++) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r; size_t i;
        for (i = 0; i < sizeof b; i++) b[i] = (uint8_t)rnd();
        if (n >= 4) memcpy(b, c == 0 ? "RIFF" : "FORM", 4);
        if (n >= 12) memcpy(b + 8, c == 0 ? "WAVE" : "AIFF", 4);
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = b; s.n = n; s.seekable = (int)(n & 1); s.size_known = s.seekable;
        g_force_codec = codecs[c]; r = open_src(&s, &m, &d); g_force_codec = NULL;
        CHECK(r != MAKTABASAUTI_OK && !d && s.closed == 0 && m.live == 0, "%s with %zu bytes: must fail cleanly (%d)", codecs[c], n, (int)r);
    }
    {   const maktabasauti_codec_t *w = &maktabasauti_pcm_wav_desc, *a = &maktabasauti_pcm_aiff_desc; static const uint8_t riff_avi[13] = "RIFF\0\0\0\0AVI ", riff_wave[13] = "RIFF\0\0\0\0WAVE", form_8svx[13] = "FORM\0\0\0\0" "8SVX", form_aiff[13] = "FORM\0\0\0\0" "AIFF", form_aifc[13] = "FORM\0\0\0\0" "AIFC";
        CHECK(w->probe(w, riff_wave, 12, NULL) > 0 && w->probe(w, riff_wave, 11, NULL) == 0 && w->probe(w, riff_avi, 12, NULL) == 0 && w->probe(w, form_aiff, 12, NULL) == 0 && w->probe(w, NULL, 12, NULL) == 0, "WAV probe");
        CHECK(a->probe(a, form_aiff, 12, NULL) > 0 && a->probe(a, form_aifc, 12, NULL) > 0 && a->probe(a, form_aiff, 11, NULL) == 0 && a->probe(a, form_8svx, 12, NULL) == 0 && a->probe(a, riff_wave, 12, NULL) == 0, "AIFF probe");
    }
}
/* blad seek w czasie otwarcia: czysty blad, wejscie zostaje u wywolujacego */
static void test_open_seek_failure(const File *f, const char *name) {
    long at;
    for (at = 1; at <= 4; at++) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r;
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = f->b; s.n = f->n; s.seekable = 1; s.size_known = 1; s.fail_seek_at = at;
        r = open_src(&s, &m, &d);
        if (r == MAKTABASAUTI_OK) { maktabasauti_decoder_close(d); CHECK(s.seeks < at, "%s: seek #%ld failed but the open succeeded", name, at); }
        else CHECK(!d && s.closed == 0 && m.live == 0, "%s: seek failure #%ld must leave the input with the caller", name, at);
    }
}

/* ---- mutacje i wrogie naglowki ---- */
static void try_open(const File *f, const uint8_t *bytes, size_t n, int seekable, long *acc_both, long *acc_new_only, long *rej) {
    Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r; SautiPcmInfo info; void *st; static float out[8192], oldf[8192]; size_t got = 0;
    memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = bytes; s.n = n; s.seekable = seekable; s.size_known = seekable; s.max_chunk = seekable ? 0 : 9;
    r = open_src(&s, &m, &d);
    memset(&info, 0, sizeof info); st = f->ops->open(bytes, n, &info);
    if (seekable && st) {                                                                          /* wszystko, co przyjmuje dawny adapter, przyjmuje modul z tym samym PCM */
        CHECK(r == MAKTABASAUTI_OK, "mutation: the old adapter accepts, the module rejects (%d)", (int)r);
        if (r == MAKTABASAUTI_OK) {
            const size_t want = info.channels && info.total_frames ? (size_t)(8192 / info.channels) : 0; size_t g2;
            maktabasauti_stream_info_t si; si.struct_size = sizeof si; (void)maktabasauti_decoder_get_info(d, &si);
            CHECK(si.sample_rate == info.sample_rate && si.channels == info.channels, "mutation: parameters differ");
            if (want) { (void)drain(d, out, info.channels, want, 300, &got); g2 = f->ops->read(st, oldf, want); CHECK(got >= g2 && same(out, oldf, g2 * info.channels), "mutation: PCM differs"); }
            (*acc_both)++;
        }
    } else if (r == MAKTABASAUTI_OK) (*acc_new_only)++; else (*rej)++;
    if (st) f->ops->close(st);
    if (r == MAKTABASAUTI_OK) {
        uint64_t act; (void)drain(d, out, 1, 4096, 123, &got); (void)maktabasauti_decoder_seek(d, 3, MAKTABASAUTI_SEEK_EXACT, &act); (void)drain(d, out, 1, 1000, 77, &got); maktabasauti_decoder_close(d);
    } else CHECK(!d && s.closed == 0, "mutation: failed open must leave the input with the caller");
    CHECK(m.live == 0 && m.bad == 0, "mutation: leak");
}
static void test_mutations(const File *base, size_t hdr_len, const char *name, int rounds) {
    long both = 0, only_new = 0, rej = 0; int i; uint8_t *b = (uint8_t *)malloc(base->n);
    for (i = 0; i < rounds; i++) {
        size_t k, n = base->n;
        memcpy(b, base->b, base->n);
        for (k = 0; k < 1u + rnd() % 3u; k++) {
            const size_t at = rnd() % hdr_len; const unsigned how = rnd() % 4u;
            if (how == 0) b[at] = (uint8_t)rnd(); else if (how == 1) b[at] ^= (uint8_t)(1u << (rnd() & 7u)); else if (how == 2) b[at] = (rnd() & 1) ? 0xff : 0x00; else b[at]++;
        }
        if (i % 7 == 0) n = rnd() % (base->n + 1);
        try_open(base, b, n, (i & 1), &both, &only_new, &rej);
    }
    printf("%s mutations: %d rounds, both accept %ld, only the module accepts %ld, rejected %ld\n", name, rounds, both, only_new, rej);
    free(b);
}
static void test_hostile_random(void) {
    size_t k; uint8_t b[300];
    for (k = 0; k < 4000; k++) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r; size_t i;
        for (i = 0; i < sizeof b; i++) b[i] = (uint8_t)rnd();
        if (k & 1) { memcpy(b, "RIFF", 4); memcpy(b + 8, "WAVEfmt ", 8); b[16] = 16 + (uint8_t)(rnd() % 30) * (rnd() & 1); b[17] = b[18] = b[19] = 0; b[20] = (uint8_t)(rnd() % 4); b[21] = 0; b[22] = (uint8_t)(rnd() % 10); b[23] = 0; if (rnd() & 1) memcpy(b + 36, "data", 4); }
        else { memcpy(b, "FORM", 4); memcpy(b + 8, (rnd() & 1) ? "AIFF" : "AIFC", 4); memcpy(b + 12, "COMM", 4); b[16] = b[17] = b[18] = 0; b[19] = (uint8_t)(18 + rnd() % 8); b[20] = 0; b[21] = (uint8_t)(rnd() % 4); }
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = b; s.n = (k % 5 == 0) ? rnd() % sizeof b : sizeof b; s.seekable = (int)(k & 2) != 0; s.size_known = s.seekable;
        r = open_src(&s, &m, &d);
        if (r == MAKTABASAUTI_OK) { static float out[4096]; size_t got; uint64_t act; (void)maktabasauti_decoder_read(d, out, 500, MAKTABASAUTI_FORMAT_F32, &got); (void)maktabasauti_decoder_seek(d, 7, MAKTABASAUTI_SEEK_EXACT, &act); (void)maktabasauti_decoder_read(d, out, 500, MAKTABASAUTI_FORMAT_F32, &got); maktabasauti_decoder_close(d); }
        else CHECK(!d && s.closed == 0, "hostile %zu: failed open must leave the input with the caller", k);
        CHECK(m.live == 0 && m.bad == 0, "hostile %zu: leak", k);
    }
}

typedef struct { char name[64]; File f; size_t hdr; } Case;
int main(void) {
    static Case cases[128]; size_t nc = 0, i; unsigned bits, ch, k;
    for (bits = 8; bits <= 32; bits += 8) for (ch = 1; ch <= 8; ch += (ch < 2 ? 1 : (ch < 6 ? 4 : 2))) for (k = 0; k < 4; k++) {
        WavOpt o; Case *c = &cases[nc];
        o.ch = ch; o.bits = bits; o.rate = 8000 + rnd() % 80000; o.frames = 300 + rnd() % 1200; o.list_bytes = (k == 1) ? 33 : (k == 2 ? 200 + rnd() % 500 : 0); o.odd_chunk = k >= 1; o.fmt_after = k == 3;
        o.ext = (rnd() % 3 == 0) && ch <= 2; o.trailing = rnd() & 1;
        c->f = make_wav(&o); c->hdr = c->f.data_off;
        snprintf(c->name, sizeof c->name, "wav %ubit %uch%s%s%s%s", bits, ch, o.ext ? " ext" : "", o.list_bytes ? " LIST" : "", o.fmt_after ? " fmt-after-data" : "", o.trailing ? " trailing" : "");
        nc++;
    }
    for (bits = 8; bits <= 32; bits += 8) for (ch = 1; ch <= 2; ch++) for (k = 0; k < 4; k++) {
        AiffOpt o; Case *c = &cases[nc];
        o.ch = ch; o.bits = bits; o.rate = 8000 + rnd() % 80000; o.frames = 300 + rnd() % 1200; o.aifc = k & 1; o.sowt = o.aifc && (rnd() & 1); o.comm_after = k == 3; o.offset = (k == 2) ? (int)(rnd() % 17) : 0; o.trailing = rnd() & 1;
        c->f = make_aiff(&o); c->hdr = c->f.data_off;
        snprintf(c->name, sizeof c->name, "aiff%s%s %ubit %uch%s%s%s", o.aifc ? "-c" : "", o.sowt ? " sowt" : "", bits, ch, o.comm_after ? " COMM-after-SSND" : "", o.offset ? " ssnd-offset" : "", o.trailing ? " trailing" : "");
        nc++;
    }
    {   const WavOpt wz = { 2, 16, 8000, 0, 0, 0, 0, 0, 1 }; Case *c = &cases[nc]; c->f = make_wav(&wz); c->hdr = c->f.data_off; snprintf(c->name, sizeof c->name, "wav zero frames"); nc++; }
    {   const AiffOpt ab = { 2, 16, 44100, 40000, 0, 0, 1, 0, 0 }; Case *c = &cases[nc]; c->f = make_aiff(&ab); c->hdr = c->f.data_off; snprintf(c->name, sizeof c->name, "aiff big COMM-after-SSND"); nc++; }
    for (i = 0; i < nc; i++) {
        Case *c = &cases[i]; Ref ref = reference(&c->f);
        printf("%s: %zu bytes, %zu frames x %zu ch @ %u Hz\n", c->name, c->f.n, ref.frames, ref.ch, ref.rate);
        test_inputs(&c->f, &ref, c->name);
        test_block_sweep(&c->f, &ref, c->name);
        test_open_block(&c->f, &ref, c->name);
        test_seek(&c->f, &ref, c->name);
        test_truncation(&c->f, &ref, c->name);
        test_open_seek_failure(&c->f, c->name);
        test_mutations(&c->f, c->hdr < 80 ? c->hdr : 80, c->name, 60);
        free(ref.pcm);
    }
    test_header_matrix(); test_tiny_and_probe(); test_placeholder(); test_float(); test_big_chunk(); test_memory(); test_hostile_random();
    for (i = 0; i < nc; i++) free(cases[i].f.b);
    printf("PCM streaming (WAV, AIFF): %s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}
