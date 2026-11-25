/* MaktabaSauti: STRUMIENIOWE dekodery DVSM i AVR (modul maktabasauti_dvs, src/decoders/retro/atari/maktabasauti_dvs.c).
 *
 * Wzorzec PCM to dawny adapter z calym buforem (sauti_decoder_dvsm_ops / sauti_decoder_avr_ops) na tych samych bajtach.  Pliki sa generowane losowo (wszystkie tryby: PCM 8/16 bit, Pack-2 delta,
 * Pack-4 Voice; mono i stereo z blokami o roznych rozmiarach; AVR 8/16 bit ze znakiem i bez), wiec test nie zalezy od plikow uzytkownika.  Wlasnosci:
 *   1. PCM bit w bit jak wzorzec: wejscie przewijalne ze znanym rozmiarem, nieprzewijalne bez rozmiaru (dlugosc nieznana, seek/reset = E_NOT_SEEKABLE), odczyty po 1/7 bajtow, losowe E_WOULD_BLOCK;
 *   2. E_WOULD_BLOCK dokladnie raz w KAZDYM numerze wywolania wejscia: nic nie ginie;
 *   3. seek dokladny (losowe cele, 0, koniec, granice blokow, do tylu i do przodu), potem reset; seek poza koniec = E_OUT_OF_RANGE bez zmiany stanu;
 *   4. urwane dane na KAZDEJ dlugosci: ramki z kompletnych jednostek identyczne z wzorcem, potem EOF (granica) albo E_TRUNCATED;
 *   5. pamiec: generator ~100 MB Pack-2 bez rozmiaru, szczyt pamieci dekodera < 64 KiB;
 *   6. wrogie naglowki (pack/mode/blok/czestotliwosc): czysty blad, bez wycieku. */
#define _POSIX_C_SOURCE 200809L
#include "maktabasauti/maktabasauti.h"
#include "sauti_decoder_avr.h"
#include "sauti_decoder_dvsm.h"
#include <assert.h>
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
    const uint8_t *gen; size_t gen_len, gen_hdr; uint64_t gen_total, gen_done;           /* generator: naglowek (gen_hdr bajtow), potem cykliczne dane */
} Src;
static uint32_t xr(uint32_t *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }
static int64_t MAKTABASAUTI_CALL s_read(void *u, void *buf, size_t size) {
    Src *s = (Src *)u; size_t give;
    s->calls++;
    if (s->armed && ((s->block_mode == 1 && s->calls == s->block_at) || (s->block_mode == 3 && (xr(&s->rng) & 3u) == 0))) return MAKTABASAUTI_E_WOULD_BLOCK;
    if (s->max_chunk && size > s->max_chunk) size = s->max_chunk;
    if (s->gen) {                                                                      /* naglowek (gen_len bajtow z poczatku), potem dane petla od gen[hdr_len..] */
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
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION; p.allocator = &a;
    p.io.struct_size = sizeof p.io; p.io.user = s; p.io.read = s_read; p.io.close = s_close; p.io.seek = s->seekable ? s_seek : NULL; p.io.size = s->size_known ? s_size : NULL;
    return maktabasauti_decoder_open(&p, d);
}

/* ---- generowanie plikow ---- */
static uint32_t g_rng = 2463534242u;
static uint32_t rnd(void) { return xr(&g_rng); }
typedef struct { uint8_t *b; size_t n; unsigned ch; const SautiDecoderOps *ops; uint64_t block_frames; } File;
static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
/* DVSM: pack 0/2/4, channels 1/2, bits (pack 0: 8/16), wielkosc danych bytes; blok stereo `block` */
static File make_dvsm(unsigned pack, unsigned ch, unsigned bits, size_t data_bytes, uint32_t block, unsigned hz_code, unsigned extra_off) {
    File f; size_t off = 16u + extra_off, i; uint8_t *b = (uint8_t *)calloc(off + data_bytes + 4, 1);
    memcpy(b, "DVSM\0\0", 6); put16(b + 6, (unsigned)off); put16(b + 8, hz_code); b[10] = (uint8_t)pack; b[11] = (uint8_t)((ch == 1 ? 2u : 0u) | (bits == 16 ? 1u : 0u)); put32(b + 12, block);
    for (i = 0; i < data_bytes; i++) b[off + i] = (uint8_t)rnd();
    for (i = 16; i < off; i++) b[i] = (uint8_t)rnd();
    f.b = b; f.n = off + data_bytes; f.ch = ch; f.ops = &sauti_decoder_dvsm_ops; f.block_frames = 0;
    return f;
}
static File make_avr(unsigned ch, unsigned bits, int is_signed, uint32_t rate, uint32_t units) {
    File f; const size_t bytes = (size_t)units * (bits / 8u); size_t i; uint8_t *b = (uint8_t *)calloc(128 + bytes + 4, 1);
    memcpy(b, "2BIT", 4); put16(b + 12, ch == 1 ? 0 : 0xffff); put16(b + 14, bits); put16(b + 16, is_signed ? 0xffff : 0); put32(b + 22, rate & 0xffffffu); put32(b + 26, units);
    for (i = 0; i < bytes; i++) b[128 + i] = (uint8_t)rnd();
    f.b = b; f.n = 128 + bytes; f.ch = ch; f.ops = &sauti_decoder_avr_ops; f.block_frames = 0;
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
        CHECK(info.sample_rate == ref->rate && info.channels == ref->ch, "%s %s: stream parameters differ", name, cfg[i].what);
        if (cfg[i].size_known) CHECK(info.total_frames == ref->frames, "%s %s: length %llu vs %zu", name, cfg[i].what, (unsigned long long)info.total_frames, ref->frames);
        else CHECK(info.total_frames == MAKTABASAUTI_UNKNOWN_LENGTH || strstr(name, "avr"), "%s %s: unknown size must give unknown length (%llu)", name, cfg[i].what, (unsigned long long)info.total_frames);
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
static void test_block_sweep(const File *f, const Ref *ref, const char *name) {
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
static void test_seek(const File *f, const Ref *ref, const char *name, int pack4_long) {
    Src s; Mem m; maktabasauti_decoder_t *d = NULL; float *buf = (float *)malloc(2000 * ref->ch * sizeof(float)); size_t i; uint64_t act; maktabasauti_result_t r;
    memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
    s.data = f->b; s.n = f->n; s.seekable = 1; s.size_known = 1;
    r = open_src(&s, &m, &d); CHECK(r == MAKTABASAUTI_OK, "%s seek: open", name); if (r != MAKTABASAUTI_OK) { free(buf); return; }
    for (i = 0; i < 300; i++) {
        uint64_t target = i == 0 ? 0 : i == 1 ? ref->frames : i == 2 ? ref->frames - 1 : (rnd() % (ref->frames + 1)); size_t want = 1500, got = 0, avail;
        if (i > 3 && (i & 3) == 0 && f->block_frames) { target = (uint64_t)(rnd() % (ref->frames / f->block_frames + 1)) * f->block_frames + ((rnd() & 1) ? 0 : f->block_frames - 1); if (target > ref->frames) target = ref->frames; }
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
    (void)pack4_long;
    maktabasauti_decoder_close(d); free(buf);
    CHECK(m.live == 0, "%s seek: leak", name);
}

/* ile ramek mieszczą pierwsze `bytes` bajtow danych i ile bajtow ogona jest niekompletne (niezalezna arytmetyka formatu) */
static size_t frames_in(unsigned pack, unsigned ch, unsigned bits, uint32_t block, size_t bytes, int *tail) {
    *tail = 0;
    if (pack == 0) { const size_t fb = (size_t)ch * (bits / 8u); *tail = (int)(bytes % fb != 0); return bytes / fb; }
    if (ch == 1 && block >= 3) {                                                                              /* mono z blokami: kazdy blok ma wlasne ziarno (2 B), potem delty (Pack-4: 2 ramki na bajt) */
        const size_t per = pack == 2 ? 1 : 2, ff = 1 + (block - 2) * per, full = bytes / block, rem = bytes % block; size_t t = full * ff;
        if (rem >= 2) t += 1 + (rem - 2) * per; else *tail = rem != 0;
        return t; }
    if (ch == 1) { if (bytes < 2) { *tail = bytes == 1; return 0; } return pack == 2 ? bytes - 1 : 1 + 2 * (bytes - 2); }
    { const size_t bpf = pack == 2 ? 2 : 1, ff = 1 + (block - 4) / bpf, full = bytes / block, rem = bytes % block; size_t t = full * ff;
      if (rem >= 4) { t += 1 + (rem - 4) / bpf; *tail = ((rem - 4) % bpf) != 0; } else *tail = rem != 0;
      return t; }
}
static void test_truncation(const File *f, const Ref *ref, const char *name, unsigned pack, unsigned bits, uint32_t block, size_t off) {
    size_t cut; const int is_avr = strstr(name, "avr") != NULL; float *out = (float *)malloc((ref->frames + 4096) * ref->ch * sizeof(float));
    for (cut = 0; cut <= f->n; cut += (cut < off + 80 || cut + 80 > f->n) ? 1u : 1u + rnd() % 13u) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; size_t got = 0, expect; int tail = 0; maktabasauti_result_t r, rr;
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = f->b; s.n = cut; s.seekable = (int)(cut & 1); s.size_known = (cut % 4) == 0 || (cut % 4) == 1; s.max_chunk = (cut % 3) ? 0 : 5;
        r = open_src(&s, &m, &d);
        if (cut < off) { CHECK(r != MAKTABASAUTI_OK && !d && s.closed == 0 && m.live == 0, "%s cut %zu: a cut header must fail cleanly (%d)", name, cut, (int)r); continue; }
        if (!is_avr && s.size_known) {                                                          /* znany rozmiar: dlugosc z arytmetyki, wiec pusty / jednobajtowy zapis delty jest odrzucany przy otwarciu (jak w starym adapterze) */
            size_t e0 = frames_in(pack, (unsigned)ref->ch, bits, block, cut - off, &tail);
            if (e0 == 0) { CHECK(r != MAKTABASAUTI_OK && !d && s.closed == 0 && m.live == 0, "%s cut %zu: no complete unit with a known size must be rejected at open (%d)", name, cut, (int)r); continue; }
        }
        CHECK(r == MAKTABASAUTI_OK, "%s cut %zu: open (%d)", name, cut, (int)r);
        if (r != MAKTABASAUTI_OK) continue;
        if (is_avr) { const size_t fb = (size_t)ref->ch * (bits / 8u); expect = (cut - off) / fb; if (expect > ref->frames) expect = ref->frames; }
        else expect = frames_in(pack, (unsigned)ref->ch, bits, block, cut - off, &tail);
        rr = drain(d, out, ref->ch, ref->frames + 4096, 211, &got);
        if (is_avr) CHECK(got == expect && rr == ((expect == ref->frames || s.size_known) ? MAKTABASAUTI_EOF : MAKTABASAUTI_E_TRUNCATED) && same(out, ref->pcm, got * ref->ch), "%s cut %zu: end %d, %zu/%zu frames", name, cut, (int)rr, got, expect);
        else CHECK(got == expect && rr == (tail ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_EOF) && same(out, ref->pcm, got * ref->ch), "%s cut %zu: end %d (tail %d), %zu/%zu frames", name, cut, (int)rr, tail, got, expect);
        if (rr == MAKTABASAUTI_E_TRUNCATED) {                                                   /* blad jest lepki, a flush go czysci (dane nadal sa urwane, wiec wraca) */
            size_t g2 = 0; maktabasauti_result_t r2 = maktabasauti_decoder_read(d, out, 10, MAKTABASAUTI_FORMAT_F32, &g2);
            CHECK(r2 == MAKTABASAUTI_E_TRUNCATED && g2 == 0, "%s cut %zu: E_TRUNCATED must be sticky (%d)", name, cut, (int)r2);
        }
        maktabasauti_decoder_close(d);
        CHECK(m.live == 0 && s.closed == 1, "%s cut %zu: leak or close count %d", name, cut, s.closed);
    }
    free(out);
}
static void test_memory(const File *f, const char *name, size_t hdr) {
    Src s; Mem m; maktabasauti_decoder_t *d = NULL; static float out[4096 * 2]; size_t got, total = 0; maktabasauti_result_t r;
    memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
    s.gen = f->b; s.gen_len = f->n; s.gen_hdr = hdr; s.gen_total = (uint64_t)100 << 20; s.max_chunk = 4096; s.n = f->n;
    r = open_src(&s, &m, &d); CHECK(r == MAKTABASAUTI_OK, "%s generator: open (%d)", name, (int)r); if (r != MAKTABASAUTI_OK) return;
    for (;;) { r = maktabasauti_decoder_read(d, out, 4096, MAKTABASAUTI_FORMAT_F32, &got); if (r != MAKTABASAUTI_OK) { CHECK(r == MAKTABASAUTI_EOF, "%s generator: code %d", name, (int)r); break; } total += got; }
    CHECK(total > 50000000u, "%s generator: decoded only %zu frames", name, total);
    CHECK(m.peak < 65536u, "%s generator: memory peak %zu bytes for an unbounded source", name, m.peak);
    maktabasauti_decoder_close(d);
    CHECK(m.live == 0, "%s generator: leak", name);
}
static void test_hostile(void) {
    size_t k; uint8_t b[400];
    for (k = 0; k < 4000; k++) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r; size_t i;
        for (i = 0; i < sizeof b; i++) b[i] = (uint8_t)rnd();
        if (k & 1) { memcpy(b, "DVSM\0\0", 6); put16(b + 6, (unsigned)(rnd() % 80)); put16(b + 8, rnd() % 400000); b[10] = (uint8_t)(rnd() % 7); b[11] = (uint8_t)(rnd() % 6); put32(b + 12, rnd() % 40); }
        else { memcpy(b, "2BIT", 4); put16(b + 12, (rnd() & 1) ? 0xffff : rnd() & 3); put16(b + 14, (rnd() & 1) ? 16 : rnd() % 40); put16(b + 16, (rnd() & 1) ? 0xffff : 0); put32(b + 22, rnd()); put32(b + 26, rnd() % 100); b[40] = b[41] = (uint8_t)(rnd() % 3 == 0); }
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = b; s.n = (k % 5 == 0) ? rnd() % sizeof b : sizeof b; s.seekable = (int)(k & 2) != 0; s.size_known = s.seekable;
        r = open_src(&s, &m, &d);
        if (r == MAKTABASAUTI_OK) { static float out[2048]; size_t got; uint64_t act; (void)maktabasauti_decoder_read(d, out, 500, MAKTABASAUTI_FORMAT_F32, &got); (void)maktabasauti_decoder_seek(d, 7, MAKTABASAUTI_SEEK_EXACT, &act); (void)maktabasauti_decoder_read(d, out, 500, MAKTABASAUTI_FORMAT_F32, &got); maktabasauti_decoder_close(d); }
        else CHECK(!d && s.closed == 0, "hostile %zu: failed open must leave the input with the caller", k);
        CHECK(m.live == 0 && m.bad == 0, "hostile %zu: leak", k);
    }
}

/* dlugi plik: pula punktow kontrolnych (1024) sie wyczerpuje i jest przerzedzana; seek nadal dokladny */
static void test_seek_long(unsigned pack, unsigned ch, const char *name) {
    uint32_t block = ch == 2 ? (pack == 2 ? 2u * 70000u : 4u + 3000000u) : 0u; File f = make_dvsm(pack, ch, 16, 9000000u, block, 3, 0); Ref ref = reference(&f);
    Src s; Mem m; maktabasauti_decoder_t *d = NULL; float *buf = (float *)malloc(3000 * ref.ch * sizeof(float)); size_t i; uint64_t act; maktabasauti_result_t r;
    memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = f.b; s.n = f.n; s.seekable = 1; s.size_known = 1;
    r = open_src(&s, &m, &d); CHECK(r == MAKTABASAUTI_OK, "%s long: open", name);
    if (r == MAKTABASAUTI_OK) {
        size_t got = 0;
        for (;;) { r = maktabasauti_decoder_read(d, buf, 3000, MAKTABASAUTI_FORMAT_F32, &got); if (r != MAKTABASAUTI_OK) break; }                       /* przejscie po calym pliku zapisuje punkty kontrolne */
        CHECK(r == MAKTABASAUTI_EOF, "%s long: full pass ended with %d", name, (int)r);
        for (i = 0; i < 120; i++) {
            const uint64_t target = i == 0 ? ref.frames : i == 1 ? 0 : (uint64_t)rnd() % (ref.frames + 1u); size_t want = 2500; got = 0;
            r = maktabasauti_decoder_seek(d, target, MAKTABASAUTI_SEEK_EXACT, &act);
            CHECK(r == MAKTABASAUTI_OK && act == target, "%s long: seek %llu (%d)", name, (unsigned long long)target, (int)r); if (r != MAKTABASAUTI_OK) break;
            if (want > ref.frames - target) want = (size_t)(ref.frames - target);
            if (!want) continue;
            r = maktabasauti_decoder_read(d, buf, want, MAKTABASAUTI_FORMAT_F32, &got);
            CHECK(r == MAKTABASAUTI_OK && got >= 1 && same(buf, ref.pcm + target * ref.ch, got * ref.ch), "%s long: PCM after seek %llu differs (%d, %zu frames)", name, (unsigned long long)target, (int)r, got);
        }
        CHECK(m.peak < 200000u, "%s long: memory peak %zu (the checkpoint pool is bounded)", name, m.peak);
        maktabasauti_decoder_close(d);
    }
    CHECK(m.live == 0, "%s long: leak", name);
    free(buf); free(ref.pcm); free(f.b);
}

/* Program 525 (Five to Five) zapisuje przy redukcji do mono w naglowku AVR liczbe probek ZRODLA (2x za duzo): przy znanym rozmiarze dlugosc jest przycinana do danych i plik konczy sie czysto (EOF), bez
 * rozmiaru zostaje E_TRUNCATED; stereo i poprawne naglowki sie nie zmieniaja. */
static void test_avr_overstated(void) {
    static const unsigned bitsv[] = { 8u, 16u }; unsigned v, mode;
    for (v = 0; v < 2; v++) for (mode = 0; mode < 2; mode++) {
        File f = make_avr(1, bitsv[v], 1, 44100, 1000u); Ref ref; Src s; Mem m; maktabasauti_decoder_t *d = NULL; size_t got = 0; maktabasauti_result_t r, rr; maktabasauti_stream_info_t info;
        float *out = (float *)malloc(4096 * sizeof(float));
        ref = reference(&f); put32(f.b + 26, 2000u);                                                              /* naglowek obiecuje 2x wiecej niz jest */
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = f.b; s.n = f.n; s.seekable = (int)mode; s.size_known = (int)mode;
        r = open_src(&s, &m, &d); CHECK(r == MAKTABASAUTI_OK, "avr overstated %u/%u: open (%d)", bitsv[v], mode, (int)r);
        if (r == MAKTABASAUTI_OK) {
            memset(&info, 0, sizeof info); info.struct_size = sizeof info; (void)maktabasauti_decoder_get_info(d, &info);
            rr = drain(d, out, 1, 4096, 97, &got);
            CHECK(got == 1000u && same(out, ref.pcm, 1000u), "avr overstated %u/%u: %zu frames", bitsv[v], mode, got);
            CHECK(rr == (mode ? MAKTABASAUTI_EOF : MAKTABASAUTI_E_TRUNCATED), "avr overstated %u/%u: end %d", bitsv[v], mode, (int)rr);
            if (mode) CHECK(info.total_frames == 1000u, "avr overstated %u: length %llu", bitsv[v], (unsigned long long)info.total_frames);
            maktabasauti_decoder_close(d);
        }
        CHECK(m.live == 0, "avr overstated %u/%u: leak", bitsv[v], mode);
        free(out); free(ref.pcm); free(f.b);
    }
}

typedef struct { const char *name; File f; unsigned pack, bits; uint32_t block; size_t off; } Case;
int main(void) {
    static const unsigned hz[] = { 0, 3, 7, 12345, 44100, 48000 };
    static char names[40][48]; Case cases[40]; size_t nc = 0, i; unsigned pack, ch, bits, k;
    for (pack = 0; pack <= 4; pack += 2) for (ch = 1; ch <= 2; ch++) for (k = 0; k < 2; k++) {
        const unsigned b = pack == 0 ? (k ? 16u : 8u) : 16u; uint32_t block = 0; size_t bytes; Case *c = &cases[nc]; char *nm = names[nc];
        if (pack != 0 && k == 1 && ch == 2) continue;                                                            /* wariant k == 1: mono z blokami */
        if (pack != 0 && ch == 2) block = pack == 2 ? 2u * (2 + rnd() % 3000) : 4u + rnd() % 5000;
        if (pack != 0 && ch == 1 && k == 1) block = 3u + rnd() % 3000u;
        bytes = pack == 0 ? ((size_t)ch * (b / 8) * (3000 + rnd() % 9000)) : (ch == 1 ? (block >= 3 ? (size_t)block * (3 + rnd() % 4) + rnd() % block : 5000 + rnd() % 20000) : (size_t)block * (3 + rnd() % 4) + 4 + (pack == 2 ? 2u * (rnd() % 100) : rnd() % 100));
        snprintf(nm, 48, "dvsm pack%u %s %ubit%s", pack, ch == 1 ? "mono" : "stereo", b, (pack != 0 && ch == 1 && k == 1) ? " blocks" : "");
        c->name = nm; c->pack = pack; c->bits = b; c->block = block; c->off = 16u + (rnd() % 3 == 0 ? rnd() % 40u : 0u);
        c->f = make_dvsm(pack, ch, b, bytes, block, hz[rnd() % 6], (unsigned)(c->off - 16u));
        c->f.block_frames = (pack != 0 && ch == 2) ? 1 + (block - 4) / (pack == 2 ? 2u : 1u) : (pack != 0 && ch == 1 && block >= 3) ? 1 + (block - 2) * (pack == 2 ? 1u : 2u) : 0;
        nc++;
    }
    for (k = 0; k < 2; k++) {                                                                                      /* ciasteczko PACK (tablica 512 B) + INFO w naglowku: Pack-2 mono (bloki) i stereo */
        const unsigned chs = k ? 2u : 1u; const uint32_t block = chs == 2 ? 2u * (2 + rnd() % 3000) : 3u + rnd() % 3000u; size_t j;
        Case *c = &cases[nc]; char *nm = names[nc];
        snprintf(nm, 48, "dvsm pack2 %s 16bit PACK cookie", chs == 1 ? "mono" : "stereo");
        c->name = nm; c->pack = 2; c->bits = 16; c->block = block; c->off = 16u + 6u + 512u + 6u + 10u;
        c->f = make_dvsm(2, chs, 16, (size_t)block * 4 + (chs == 2 ? 4u + 2u * (rnd() % 100) : rnd() % block), block, 3, (unsigned)(c->off - 16u));
        memcpy(c->f.b + 16, "PACK", 4); put16(c->f.b + 20, 6u + 512u);
        for (j = 0; j < 256; j++) { const int v = (int)(rnd() % 4001u) - 2000; put16(c->f.b + 22 + 2 * j, (unsigned)(v & 0xFFFF)); }   /* wlasna tablica: zwykle male delty */
        memcpy(c->f.b + 16 + 6 + 512, "INFO", 4); put16(c->f.b + 16 + 6 + 512 + 4, 6u + 10u);
        c->f.block_frames = chs == 2 ? 1 + (block - 4) / 2u : 1 + (block - 2);
        nc++;
    }
    for (bits = 8; bits <= 16; bits += 8) for (ch = 1; ch <= 2; ch++) for (k = 0; k < 2; k++) {
        Case *c = &cases[nc]; char *nm = names[nc]; const uint32_t units = ch * (1500 + rnd() % 5000);
        snprintf(nm, 48, "avr %ubit %s %s", bits, ch == 1 ? "mono" : "stereo", k ? "signed" : "unsigned");
        c->name = nm; c->pack = 9; c->bits = bits; c->block = 0; c->off = 128; c->f = make_avr(ch, bits, (int)k, 8000 + rnd() % 40000, units); nc++;
    }
    for (i = 0; i < nc; i++) {
        Case *c = &cases[i]; Ref ref = reference(&c->f);
        printf("%s: %zu bytes, %zu frames x %zu ch @ %u Hz\n", c->name, c->f.n, ref.frames, ref.ch, ref.rate);
        test_inputs(&c->f, &ref, c->name);
        test_block_sweep(&c->f, &ref, c->name);
        test_seek(&c->f, &ref, c->name, 0);
        test_truncation(&c->f, &ref, c->name, c->pack, c->bits, c->block, c->off);
        if (c->pack == 2 && ref.ch == 1) test_memory(&c->f, c->name, c->off);
        free(ref.pcm);
    }
    test_avr_overstated();
    test_seek_long(2, 1, "dvsm pack2 mono"); test_seek_long(4, 1, "dvsm pack4 mono"); test_seek_long(2, 2, "dvsm pack2 stereo"); test_seek_long(4, 2, "dvsm pack4 stereo");
    test_hostile();
    for (i = 0; i < nc; i++) free(cases[i].f.b);
    printf("DVS streaming: %s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}
