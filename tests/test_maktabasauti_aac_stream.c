/* MaktabaSauti: PRAWDZIWE STRUMIENIOWANIE AAC w ADTS (modul maktabasauti_aac, src/decoders/modern/aac/maktabasauti_aac_adts.c).
 *
 * Wzorzec PCM to odczyt calego pliku z pamieci nowym API (bit w bit rowny staremu API: test_maktabasauti_regression).  Kazdy test sprawdza jedna wlasnosc strumieniowania:
 *   1. wejscie NIEPRZEWIJALNE bez rozmiaru: PCM identyczny, dlugosc nieznana, seek/reset = E_NOT_SEEKABLE;
 *   2. odczyty po 1 bajcie i po losowych, krotkich porcjach (czesciowe odczyty): PCM identyczny;
 *   3. E_WOULD_BLOCK: wejscie "nie ma jeszcze danych" w kazdym punkcie (przemiatanie po numerze wywolania) i losowo co drugie wywolanie; powtarzanie wywolan daje ten sam PCM, nic nie ginie;
 *   4. urwany strumien (obciecie na KAZDEJ dlugosci w poczatku i koncu): ramki sprzed urwania identyczne ze wzorcem, potem EOF (granica ramki) albo E_TRUNCATED;
 *   5. uszkodzony strumien: poprawne ramki, potem E_CORRUPT_DATA; po flush dekodowanie wznawia sie po resynchronizacji (strumien nieprzewijalny, na zywo);
 *   6. pamiec: zrodlo-generator oddajace ~100 MB bez przechowywania, nieznany rozmiar, a szczyt pamieci dekodera < 1 MiB; wejscie przewijalne: szczyt pamieci nie rosnie z dlugoscia
 *      (plik 1x i 150x, roznica < budzet punktow kontrolnych), zadne pojedyncze zadanie alokacji nie jest proporcjonalne do pliku;
 *   7. seek DOKLADNY (wejscie z pamieci i z pliku): losowe cele, na granicach ramek, +-1, koniec, do tylu i do przodu, ten sam wynik co wzorzec; seek poza koniec = E_OUT_OF_RANGE bez zmiany stanu;
 *      po wyczerpaniu puli punktow kontrolnych (dlugi plik) wynik nadal dokladny;
 *   8. opcja adts.scan=off: dlugosc nieznana, seek dziala (do przodu przez dekodowanie), ladowanie za koncem = koniec;
 *   9. dlugosc: wejscie przewijalne podaje dokladna dlugosc; bajty za ostatnia ramka (znacznik ID3v1) nie psuja odczytu.
 * usage: test_maktabasauti_aac_stream DIR   (DIR z tests/data: lc.aac he1.aac he2.aac m51.aac lc_mono.aac he1_mono.aac) */
#define _POSIX_C_SOURCE 200809L
#include "maktabasauti/maktabasauti.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
static int g_mp4;                                        /* 1 = MP4: indeksowany na wejsciu przewijalnym, jednoprzebiegowy "na zywo" na nieprzewijalnym */
static int g_live_ok;                                    /* 1 = MP4 z moov przed danymi (fragmentowany albo faststart): da sie czytac z wejscia nieprzewijalnego */
static uint32_t g_open_flags;                            /* flagi maktabasauti_open_params_t dla open_src */
static int g_frag;                                       /* 1 = MP4 fragmentowany (na zywo: dlugosc nieznana) */
static int g_adif;                                       /* 1 = ADIF: bloki bez dlugosci i bez synchronizacji (brak resynchronizacji po uszkodzeniu) */
static int g_loas;                                       /* 1 = pliki LOAS: elementy (3 B naglowka + dlugosc), 0 = ADTS */
static size_t g_hmin = 7;                                /* najmniejszy naglowek elementu */
static size_t elen(const uint8_t *b, size_t pos, int loas) { return loas ? 3u + (size_t)(((b[pos + 1] & 0x1F) << 8) | b[pos + 2]) : (size_t)(((b[pos + 3] & 3) << 11) | (b[pos + 4] << 3) | (b[pos + 5] >> 5)); }
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #c); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); g_failures++; } } while (0)

/* ---- alokator sledzacy (bez realloc) ---- */
typedef struct { size_t live, peak, max_request; long bad; } Mem;
/* free bez rozmiaru: sledzimy rozmiar nagłowkiem */
typedef struct { size_t n; size_t pad[3]; } Hdr;                        /* 32 B, wyrownanie 16 */
static void *MAKTABASAUTI_CALL h_alloc(void *u, size_t n) { Mem *m = (Mem *)u; Hdr *h = (Hdr *)malloc(sizeof(Hdr) + n); if (!h) return NULL; h->n = n; m->live += n; if (m->live > m->peak) m->peak = m->live; if (n > m->max_request) m->max_request = n; return h + 1; }
static void MAKTABASAUTI_CALL h_free(void *u, void *p) { Mem *m = (Mem *)u; Hdr *h; if (!p) { m->bad++; return; } h = (Hdr *)p - 1; m->live -= h->n; free(h); }
static maktabasauti_allocator_t mem_allocator(Mem *m) { maktabasauti_allocator_t a; memset(&a, 0, sizeof a); a.struct_size = sizeof a; a.user = m; a.alloc = h_alloc; a.realloc = NULL; a.free = h_free; return a; }

/* ---- wejscie testowe ---- */
typedef struct {
    const uint8_t *data; size_t n, pos;
    int seekable, size_known;
    size_t max_chunk;                                    /* 0 = bez limitu */
    int block_mode;                                      /* 0 brak, 1 = raz w wywolaniu numer block_at, 2 = co drugie wywolanie, 3 = losowo */
    long block_at, calls, blocked; uint32_t rng;
    /* generator: oddaje ramki cyklicznie bez przechowywania (tryb gen_n > 0) */
    const uint8_t *gen; size_t gen_len; uint64_t gen_total, gen_done;
    int closed, armed;                                   /* blokowanie wlacza sie dopiero po otwarciu (otwarcie nieprzewijalnego wejscia nie jest powtarzalne) */
} Src;
static uint32_t xr(uint32_t *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }
static int64_t MAKTABASAUTI_CALL s_read(void *u, void *buf, size_t size) {
    Src *s = (Src *)u; size_t give;
    s->calls++;
    if (s->armed && ((s->block_mode == 1 && s->calls == s->block_at) || (s->block_mode == 2 && (s->calls & 1)) || (s->block_mode == 3 && (xr(&s->rng) & 3u) == 0))) { s->blocked++; return MAKTABASAUTI_E_WOULD_BLOCK; }
    if (s->gen) {
        uint8_t *o = (uint8_t *)buf; give = 0;
        if (s->max_chunk && size > s->max_chunk) size = s->max_chunk;
        while (give < size && s->gen_done < s->gen_total) { o[give] = s->gen[s->gen_done % s->gen_len]; give++; s->gen_done++; }
        return (int64_t)give;
    }
    give = s->n - s->pos; if (give > size) give = size;
    if (s->max_chunk && give > s->max_chunk) give = s->max_chunk;
    if (give) memcpy(buf, s->data + s->pos, give);
    s->pos += give;
    return (int64_t)give;
}
static int64_t MAKTABASAUTI_CALL s_seek(void *u, int64_t off, int wh) {
    Src *s = (Src *)u; int64_t base = wh == 0 ? 0 : wh == 1 ? (int64_t)s->pos : (int64_t)s->n;
    if (base + off < 0 || (uint64_t)(base + off) > (uint64_t)s->n) return MAKTABASAUTI_E_OUT_OF_RANGE;
    s->pos = (size_t)(base + off); return base + off;
}
static int64_t MAKTABASAUTI_CALL s_size(void *u) { Src *s = (Src *)u; return s->gen ? (int64_t)s->gen_total : (int64_t)s->n; }
static void MAKTABASAUTI_CALL s_close(void *u) { ((Src *)u)->closed++; }
static maktabasauti_io_t src_io(Src *s) { maktabasauti_io_t io; memset(&io, 0, sizeof io); io.struct_size = sizeof io; io.user = s; io.read = s_read; io.close = s_close; io.seek = s->seekable ? s_seek : NULL; io.size = s->size_known ? s_size : NULL; return io; }

static maktabasauti_result_t open_src(Src *s, Mem *m, maktabasauti_decoder_t **d, const char *scan) {
    maktabasauti_allocator_t a = mem_allocator(m); maktabasauti_open_params_t p; maktabasauti_option_t opt;
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION; p.io = src_io(s); p.allocator = &a; p.flags = g_open_flags;
    if (scan) { opt.key = "adts.scan"; opt.value = scan; p.options = &opt; p.option_count = 1; }
    return maktabasauti_decoder_open(&p, d);
}

static uint8_t *slurp(const char *dir, const char *name, size_t *n) {
    char path[512]; FILE *f; long sz; uint8_t *b;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "rb"); if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    b = (uint8_t *)malloc((size_t)sz); if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) exit(2);
    fclose(f); *n = (size_t)sz; return b;
}

typedef struct { float *pcm; size_t frames, ch; uint32_t rate; } Ref;
static Ref reference(const uint8_t *b, size_t n) {
    Ref r; Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info; size_t cap, got = 0;
    memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = b; s.n = n; s.seekable = 1; s.size_known = 1;
    assert(open_src(&s, &m, &d, NULL) == MAKTABASAUTI_OK);
    info.struct_size = sizeof info; assert(maktabasauti_decoder_get_info(d, &info) == MAKTABASAUTI_OK);
    assert(info.total_frames != MAKTABASAUTI_UNKNOWN_LENGTH && (info.flags & MAKTABASAUTI_STREAM_SEEKABLE));
    r.ch = info.channels; r.rate = info.sample_rate; cap = (size_t)info.total_frames;
    r.pcm = (float *)malloc(cap * r.ch * sizeof(float) + 16); assert(r.pcm);
    for (;;) { size_t g = 0; maktabasauti_result_t rr = maktabasauti_decoder_read(d, r.pcm + got * r.ch, 4096 < cap - got ? 4096 : cap - got, MAKTABASAUTI_FORMAT_F32, &g); if (rr == MAKTABASAUTI_EOF) break; assert(rr == MAKTABASAUTI_OK); got += g; if (got == cap) { size_t z; assert(maktabasauti_decoder_read(d, r.pcm, 1, MAKTABASAUTI_FORMAT_F32, &z) == MAKTABASAUTI_EOF); break; } }
    assert(got == cap); r.frames = got;
    maktabasauti_decoder_close(d);
    return r;
}

/* czyta do EOF/bledu z ponawianiem WOULD_BLOCK; zwraca kod koncowy; *got = ramki; out ma miejsce na `cap` ramek */
static maktabasauti_result_t drain(maktabasauti_decoder_t *d, float *out, size_t ch, size_t cap, size_t chunk, size_t *got, long *retries) {
    size_t n = 0; long blocked_in_row = 0; maktabasauti_result_t rr = MAKTABASAUTI_OK;
    for (;;) {
        size_t g = 0, want = chunk < cap - n ? chunk : cap - n;
        if (want == 0) { rr = MAKTABASAUTI_OK; break; }
        rr = maktabasauti_decoder_read(d, out + n * ch, want, MAKTABASAUTI_FORMAT_F32, &g);
        n += g;
        if (rr == MAKTABASAUTI_E_WOULD_BLOCK) { (*retries)++; if (++blocked_in_row > 1000) { rr = MAKTABASAUTI_E_INTERNAL; break; } continue; }
        blocked_in_row = 0;
        if (rr != MAKTABASAUTI_OK) break;
    }
    *got = n; return rr;
}
static int same(const float *a, const float *b, size_t samples) { return memcmp(a, b, samples * sizeof(float)) == 0; }

static const char *const files[] = { "lc.aac", "lc_mono.aac", "he1.aac", "he1_mono.aac", "he2.aac", "m51.aac", "lc.loas", "lc_mono.loas", "he1.loas", "he2.loas", "lc.adif", "lc_mono.adif", "he1.adif", "lc.m4a", "lc_mono.m4a", "he1.m4a", "he2.m4a", "lc_fast.m4a", "lc_mono_fast.m4a", "he1_fast.m4a", "he2_fast.m4a", "lc_frag.m4a", "ld_st.m4a", "eld_st.m4a", "ld_mono.m4a", "eld_mono.m4a", "ld_hq.m4a", "eld_hq.m4a", "ld_st.loas", "eld_st.loas" };
#define NFILES (sizeof files / sizeof *files)

/* 1+2+3: niestandardowe wejscia, PCM identyczny */
static void test_inputs(const uint8_t *b, size_t n, const Ref *ref, const char *name) {
    struct { const char *what; int seekable, size_known; size_t chunk; int block_mode; long block_at; } cfg[] = {
        { "non-seekable, unknown size", 0, 0, 0, 0, 0 },
        { "non-seekable, 1-byte reads", 0, 0, 1, 0, 0 },
        { "non-seekable, 7-byte reads", 0, 0, 7, 0, 0 },
        { "seekable, 1-byte reads", 1, 1, 1, 0, 0 },
        { "seekable, 300-byte reads", 1, 1, 300, 0, 0 },
        { "non-seekable, WOULD_BLOCK every 2nd call", 0, 0, 0, 2, 0 },
        { "seekable, WOULD_BLOCK random", 1, 1, 0, 3, 0 },
        { "non-seekable, 5-byte reads + random WOULD_BLOCK", 0, 0, 5, 3, 0 },
    };
    size_t i, k;
    for (i = 0; i < sizeof cfg / sizeof *cfg; i++) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info; float *out; size_t got = 0; long retries = 0; maktabasauti_result_t r, rr;
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
        if (g_mp4 && !g_live_ok && !cfg[i].seekable) {                         /* moov za danymi: z wejscia nieprzewijalnego nie da sie tego odczytac - czysty blad, wejscie i pamiec nietkniete */
            s.data = b; s.n = n; s.max_chunk = cfg[i].chunk; r = open_src(&s, &m, &d, NULL);
            CHECK(r == MAKTABASAUTI_E_NOT_SEEKABLE && !d && s.closed == 0 && m.live == 0, "%s %s: non-seekable MP4 with moov after mdat must fail cleanly with E_NOT_SEEKABLE (%d)", name, cfg[i].what, (int)r);
            continue;
        }
        s.data = b; s.n = n; s.seekable = cfg[i].seekable; s.size_known = cfg[i].size_known; s.max_chunk = cfg[i].chunk; s.block_mode = cfg[i].block_mode; s.rng = 12345u + (uint32_t)i;
        for (k = 0;; k++) {                                                    /* otwarcie moze zwrocic WOULD_BLOCK: ponawiamy (wejscie zostaje u nas) */
            r = open_src(&s, &m, &d, NULL);
            if (r != MAKTABASAUTI_E_WOULD_BLOCK) break;
            CHECK(!s.closed && !d, "%s %s: blocked open must leave the input with the caller", name, cfg[i].what);
            if (k > 100000) break;
        }
        CHECK(r == MAKTABASAUTI_OK, "%s %s: open failed (%d)", name, cfg[i].what, (int)r);
        if (r != MAKTABASAUTI_OK) continue;
        s.armed = 1; s.calls = 0;
        info.struct_size = sizeof info; CHECK(maktabasauti_decoder_get_info(d, &info) == MAKTABASAUTI_OK, "info");
        CHECK(info.sample_rate == ref->rate && info.channels == ref->ch, "%s %s: stream parameters differ", name, cfg[i].what);
        if (!cfg[i].seekable) {
            uint64_t act;
            if (g_mp4 && !g_frag) CHECK(info.total_frames == ref->frames && !(info.flags & MAKTABASAUTI_STREAM_SEEKABLE), "%s %s: faststart MP4 knows its length from moov even on live input (%llu)", name, cfg[i].what, (unsigned long long)info.total_frames);
            else if (g_mp4) CHECK(info.total_frames == ref->frames && !(info.flags & MAKTABASAUTI_STREAM_SEEKABLE), "%s %s: live fMP4 reports the length declared by the edit list (%llu)", name, cfg[i].what, (unsigned long long)info.total_frames);
            else CHECK(info.total_frames == MAKTABASAUTI_UNKNOWN_LENGTH && !(info.flags & MAKTABASAUTI_STREAM_SEEKABLE), "%s %s: non-seekable input must report unknown length", name, cfg[i].what);
            CHECK(maktabasauti_decoder_seek(d, 0, MAKTABASAUTI_SEEK_EXACT, &act) == MAKTABASAUTI_E_NOT_SEEKABLE, "seek on non-seekable");
            CHECK(maktabasauti_decoder_reset(d) == MAKTABASAUTI_E_NOT_SEEKABLE, "reset on non-seekable");
        }
        out = (float *)malloc((ref->frames + 4096) * ref->ch * sizeof(float));
        rr = drain(d, out, ref->ch, ref->frames + 4096, 1000 + i * 37, &got, &retries);
        CHECK(rr == MAKTABASAUTI_EOF, "%s %s: stream must end with EOF, got %d after %zu frames", name, cfg[i].what, (int)rr, got);
        CHECK(got == ref->frames && same(out, ref->pcm, got * ref->ch), "%s %s: PCM differs from the reference (%zu vs %zu frames)", name, cfg[i].what, got, ref->frames);
        if (cfg[i].block_mode && n > 40000) CHECK(s.blocked > 0, "%s %s: blocking was injected but never observed", name, cfg[i].what);
        free(out);
        maktabasauti_decoder_close(d);
        CHECK(s.closed == 1 && m.live == 0 && m.bad == 0, "%s %s: input closed %d times, live=%zu bad=%ld", name, cfg[i].what, s.closed, m.live, m.bad);
    }
}

/* 3b: WOULD_BLOCK dokladnie raz, w KAZDYM numerze wywolania wejscia (krotki plik: pierwsze ~14 ramek) */
static void test_block_sweep(const uint8_t *b, size_t n, const char *name) {
    size_t cut = 0, pos = 0, frames_in = 0; long at;
    Ref ref; (void)n;
    while (frames_in < 14 && pos + g_hmin <= n) { size_t fl = elen(b, pos, g_loas); pos += fl; frames_in++; }
    cut = g_mp4 ? n : (g_adif ? n / 3 : pos);
    ref = reference(b, cut);
    for (at = 1; at <= (g_mp4 ? 12 : 60); at++) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; float *out; size_t got = 0; long retries = 0; maktabasauti_result_t r, rr; int tries = 0;
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
        s.data = b; s.n = cut; s.seekable = (g_mp4 && !g_live_ok) ? 1 : (at & 1); s.size_known = s.seekable; s.max_chunk = (at % 3 == 0) ? 11 : 0; s.block_mode = 1; s.block_at = at;
        do { r = open_src(&s, &m, &d, NULL); } while (r == MAKTABASAUTI_E_WOULD_BLOCK && ++tries < 10);
        CHECK(r == MAKTABASAUTI_OK, "%s block_at=%ld: open failed (%d)", name, at, (int)r);
        if (r != MAKTABASAUTI_OK) continue;
        s.armed = 1; s.calls = 0;
        out = (float *)malloc((ref.frames + 4096) * ref.ch * sizeof(float));
        rr = drain(d, out, ref.ch, ref.frames + 4096, 777, &got, &retries);
        CHECK((rr == MAKTABASAUTI_EOF || (g_adif && !g_mp4 && rr == MAKTABASAUTI_E_TRUNCATED)) && got == ref.frames && same(out, ref.pcm, got * ref.ch), "%s block_at=%ld: result %d, %zu/%zu frames or PCM differs", name, at, (int)rr, got, ref.frames);
        free(out); maktabasauti_decoder_close(d);
        CHECK(m.live == 0, "%s block_at=%ld: leak", name, at);
    }
    free(ref.pcm);
}

/* 3c: blokada W TRAKCIE OTWARCIA na wejsciu przewijalnym: open zwraca WOULD_BLOCK, wejscie zostaje u nas i jest cofniete, ponowienie dziala */
static void test_open_block(const uint8_t *b, size_t n, const Ref *ref, const char *name) {
    long at;
    for (at = 1; at <= 6; at++) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r; float *out; size_t got = 0; long retries = 0;
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
        s.data = b; s.n = n; s.seekable = 1; s.size_known = 1; s.block_mode = 1; s.block_at = at; s.armed = 1;
        r = open_src(&s, &m, &d, NULL);
        if (r == MAKTABASAUTI_OK) { maktabasauti_decoder_close(d); d = NULL; continue; }                        /* blokada wypadla poza otwarciem (maly plik: wszystko w prefiksie) */
        CHECK(r == MAKTABASAUTI_E_WOULD_BLOCK && !d && s.closed == 0 && m.live == 0, "%s open block_at=%ld: expected a clean WOULD_BLOCK (%d)", name, at, (int)r);
        r = open_src(&s, &m, &d, NULL);
        CHECK(r == MAKTABASAUTI_OK, "%s open block_at=%ld: retry failed (%d)", name, at, (int)r);
        if (r != MAKTABASAUTI_OK) continue;
        out = (float *)malloc((ref->frames + 4096) * ref->ch * sizeof(float));
        r = drain(d, out, ref->ch, ref->frames + 4096, 1000, &got, &retries);
        CHECK(r == MAKTABASAUTI_EOF && got == ref->frames && same(out, ref->pcm, got * ref->ch), "%s open block_at=%ld: PCM after the retried open differs", name, at);
        free(out); maktabasauti_decoder_close(d);
    }
}

/* 4a: ADIF: granice blokow nie sa widoczne (brak dlugosci), wiec sprawdzamy tylko: brak awarii/wycieku, kod koncowy, prefiks zgodny ze wzorcem */
static void test_truncation_raw(const uint8_t *b, size_t n, const Ref *ref, const char *name) {
    static const size_t starts[] = { 0, 1, 3, 4, 8, 20, 60, 100, 300, 1000 }; size_t k;
    for (k = 0; k < sizeof starts / sizeof *starts + 500; k++) {
        size_t cut = k < sizeof starts / sizeof *starts ? starts[k] : (n > 500 ? n - 500 : 0) + (k - sizeof starts / sizeof *starts), got = 0; Src s; Mem m; maktabasauti_decoder_t *d = NULL; float *out; long retries = 0; maktabasauti_result_t r, rr;
        if (cut > n) continue;
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = b; s.n = cut; s.seekable = (int)(k & 1); s.size_known = s.seekable;
        r = open_src(&s, &m, &d, NULL);
        if (r != MAKTABASAUTI_OK) { CHECK(s.closed == 0 && m.live == 0 && !d, "%s cut=%zu: failed open must leave input and memory alone", name, cut); continue; }
        out = (float *)malloc((ref->frames + 4096) * ref->ch * sizeof(float));
        rr = drain(d, out, ref->ch, ref->frames + 4096, 1024, &got, &retries);
        CHECK(rr == MAKTABASAUTI_EOF || rr == MAKTABASAUTI_E_TRUNCATED || rr == MAKTABASAUTI_E_CORRUPT_DATA, "%s cut=%zu: final code %d", name, cut, (int)rr);
        CHECK(got <= ref->frames, "%s cut=%zu: %zu frames (reference has %zu)", name, cut, got, ref->frames);
        { const size_t cmp = got > 2048 ? got - 2048 : 0; CHECK(same(out, ref->pcm, cmp * ref->ch), "%s cut=%zu: decoded part differs from the reference", name, cut); }
        free(out); maktabasauti_decoder_close(d);
        CHECK(m.live == 0 && m.bad == 0, "%s cut=%zu: leak", name, cut);
    }
}

/* 4: obciecie na kazdej dlugosci (poczatek i koniec) */
static void test_truncation(const uint8_t *b, size_t n, const Ref *ref, const char *name) {
    if (g_adif || g_loas) { test_truncation_raw(b, n, ref, name); return; }      /* LOAS: elementy przed pierwsza konfiguracja nie sa dekodowalne, wiec liczenie pelnych ramek nie ma sensu */
    size_t fl0 = elen(b, 0, g_loas), cut, nframes_total = 0, pos = 0;
    size_t starts[] = { 0, 1, 2, 3, 6, 7, 8, 9, 10, fl0 - 1, fl0, fl0 + 1, fl0 + 6, fl0 + 7, fl0 + 8 }, k;
    size_t ends_from;
    while (pos + g_hmin <= n) { pos += elen(b, pos, g_loas); nframes_total++; }
    ends_from = n > 500 ? n - 500 : 0;
    for (k = 0; k < sizeof starts / sizeof *starts + (n - ends_from); k++) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; float *out; size_t got = 0, full = 0, p2 = 0; long retries = 0; maktabasauti_result_t r, rr; int seekable = (int)(k & 1);
        cut = k < sizeof starts / sizeof *starts ? starts[k] : ends_from + (k - sizeof starts / sizeof *starts);
        if (cut > n) continue;
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
        s.data = b; s.n = cut; s.seekable = seekable; s.size_known = seekable;
        while (p2 + g_hmin <= cut) { size_t fl = elen(b, p2, g_loas); if (p2 + fl > cut) break; p2 += fl; full++; }
        r = open_src(&s, &m, &d, NULL);
        if (full == 0) { CHECK(r != MAKTABASAUTI_OK || cut >= g_hmin, "%s cut=%zu: no complete frame must not open", name, cut); if (r == MAKTABASAUTI_OK) maktabasauti_decoder_close(d); else CHECK(s.closed == 0 && m.live == 0, "ownership/leak after failed open"); continue; }
        CHECK(r == MAKTABASAUTI_OK, "%s cut=%zu: open failed (%d) with %zu complete frames", name, cut, (int)r, full);
        if (r != MAKTABASAUTI_OK) continue;
        out = (float *)malloc((ref->frames + 4096) * ref->ch * sizeof(float));
        rr = drain(d, out, ref->ch, ref->frames + 4096, 1024, &got, &retries);
        {   const int clean = (p2 == cut) || seekable;                      /* przewijalne: dlugosc z naglowkow, bajty za ostatnia pelna ramka ignorowane */
            CHECK(rr == MAKTABASAUTI_EOF || (!clean && rr == MAKTABASAUTI_E_TRUNCATED), "%s cut=%zu (seekable=%d): final code %d", name, cut, seekable, (int)rr); }
        {   const size_t per = ref->frames / nframes_total;                  /* ramek PCM na ramke ADTS */
            CHECK(got <= full * per && got + 2 * per >= full * per, "%s cut=%zu: %zu PCM frames from %zu ADTS frames", name, cut, got, full);
            /* HE-AAC v2: ostatni blok jest renderowany z cisza zamiast wyprzedzenia kolejnej ramki, wiec moze sie roznic od wzorca: porownujemy bez niego */
            const size_t cmp = (per >= 2048 && got > 2048) ? got - 2048 : got;
            CHECK(same(out, ref->pcm, cmp * ref->ch), "%s cut=%zu: decoded part differs from the reference", name, cut); }
        free(out); maktabasauti_decoder_close(d);
        CHECK(m.live == 0 && m.bad == 0, "%s cut=%zu: leak", name, cut);
    }
}

/* 5a: ADIF: uszkodzenie w polowie pliku; PCM sprzed uszkodzenia zgodny ze wzorcem, dalej dowolny koniec bez awarii (brak resynchronizacji w ADIF) */
static void test_corruption_raw(const uint8_t *b, size_t n, const Ref *ref, const char *name) {
    uint8_t *w = (uint8_t *)malloc(n); size_t k, got = 0; Src s; Mem m; maktabasauti_decoder_t *d = NULL; float *out; long retries = 0; maktabasauti_result_t r, rr;
    size_t dpos = n / 2;
    if (g_mp4) {                                                                  /* MP4: uszkadzamy srodek POSLEDNIEJ ZAWARTOSCI mdat (nie tablice w moov) */
        size_t pos = 0, last = 0, last_sz = 0;
        while (pos + 8 <= n) { size_t sz = ((size_t)b[pos] << 24) | ((size_t)b[pos + 1] << 16) | ((size_t)b[pos + 2] << 8) | b[pos + 3]; if (sz < 8 || pos + sz > n) break; if (!memcmp(b + pos + 4, "mdat", 4)) { last = pos; last_sz = sz; } pos += sz; }
        if (last_sz) dpos = last + 8 + (last_sz - 8) * 3 / 4;
    }
    memcpy(w, b, n); for (k = 0; k < 40; k++) w[dpos + k] ^= 0xA5;
    memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = w; s.n = n; s.seekable = g_mp4; s.size_known = g_mp4;
    r = open_src(&s, &m, &d, NULL); CHECK(r == MAKTABASAUTI_OK, "%s corrupt: open", name);
    if (r == MAKTABASAUTI_OK) {
        out = (float *)malloc((ref->frames + 8192) * ref->ch * sizeof(float));
        rr = drain(d, out, ref->ch, ref->frames + 8192, 1000, &got, &retries);
        if (rr == MAKTABASAUTI_E_CORRUPT_DATA || rr == MAKTABASAUTI_E_UNSUPPORTED_FEATURE) { CHECK(maktabasauti_decoder_flush(d) == MAKTABASAUTI_OK, "flush"); (void)drain(d, out + got * ref->ch, ref->ch, 4096, 1000, &k, &retries); }
        { const size_t safe = g_mp4 ? ref->frames / 2u : (size_t)((double)ref->frames * (double)(n / 2 - 3000) / (double)n);   /* MP4: uszkodzone 3/4 mdat, bierzemy polowe */ CHECK(got < safe || same(out, ref->pcm, safe * ref->ch), "%s corrupt: PCM before the damage differs", name); }
        free(out); maktabasauti_decoder_close(d);
        CHECK(m.live == 0 && m.bad == 0, "%s corrupt: leak", name);
    }
    free(w);
}

/* 5: uszkodzenie i flush (strumien nieprzewijalny) */
static void test_corruption(const uint8_t *b, size_t n, const Ref *ref, const char *name) {
    if (g_adif) { test_corruption_raw(b, n, ref, name); return; }
    size_t pos = 0, fl[40], idx = 0, i; uint8_t *w = (uint8_t *)malloc(n);
    while (pos + g_hmin <= n && idx < 40) { fl[idx] = elen(b, pos, g_loas); pos += fl[idx]; idx++; }
    for (i = 0; i < 4; i++) {                                                  /* i: 0 = zepsuty naglowek, 1 = zepsuty ładunek, 2 = zerowe 300 B, 3 = wstawione smieci */
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; float *out; size_t got = 0, off5 = 0, k, total = 0; long retries = 0; maktabasauti_result_t r, rr; size_t wn = n;
        for (k = 0; k < (g_loas ? 20u : 5u); k++) off5 += fl[k];
        memcpy(w, b, n);
        if (i == 0) w[off5] = 0x00;                                           /* synchronizacja ramki 5 */
        else if (i == 1) { for (k = 0; k < 40 && off5 + 20 + k < off5 + fl[5]; k++) w[off5 + 20 + k] ^= 0xA5; }   /* ladunek ramki 5 */
        else if (i == 2) { for (k = 0; k < 300 && off5 + k < n; k++) w[off5 + k] = 0; }
        else { /* wstawienie 123 bajtow smieci przed ramka 5 */
            uint8_t *t = (uint8_t *)malloc(n + 123); memcpy(t, b, off5); for (k = 0; k < 123; k++) t[off5 + k] = (uint8_t)(k * 7 + 3); memcpy(t + off5 + 123, b + off5, n - off5);
            memcpy(w, t, n); free(t); wn = n;                                  /* ta sama dlugosc: ogon obciety, nie szkodzi */
            (void)wn;
        }
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = w; s.n = n; s.seekable = 0; s.size_known = 0;
        r = open_src(&s, &m, &d, NULL); CHECK(r == MAKTABASAUTI_OK, "%s corrupt %zu: open (%d)", name, i, (int)r); if (r != MAKTABASAUTI_OK) continue;
        out = (float *)malloc((ref->frames + 8192) * ref->ch * sizeof(float));
        rr = drain(d, out, ref->ch, ref->frames + 8192, 1000, &got, &retries); total = got;
        CHECK(rr == MAKTABASAUTI_E_CORRUPT_DATA || rr == MAKTABASAUTI_E_UNSUPPORTED_FEATURE || rr == MAKTABASAUTI_EOF || rr == MAKTABASAUTI_E_TRUNCATED, "%s corrupt %zu: expected CORRUPT_DATA, got %d", name, i, (int)rr);
        if (rr == MAKTABASAUTI_E_CORRUPT_DATA || rr == MAKTABASAUTI_E_UNSUPPORTED_FEATURE) {
            const size_t good = got;
            CHECK(good > 0 && same(out, ref->pcm, good * ref->ch), "%s corrupt %zu: frames before the damage must equal the reference (%zu frames)", name, i, good);
            CHECK(maktabasauti_decoder_read(d, out, 100, MAKTABASAUTI_FORMAT_F32, &got) == rr, "error must be sticky until flush");
            CHECK(maktabasauti_decoder_flush(d) == MAKTABASAUTI_OK, "flush");
            rr = drain(d, out + good * ref->ch, ref->ch, ref->frames + 8192 - good, 1000, &got, &retries); total += got;
            CHECK(rr == MAKTABASAUTI_EOF || rr == MAKTABASAUTI_E_CORRUPT_DATA || rr == MAKTABASAUTI_E_TRUNCATED, "%s corrupt %zu: after flush code %d", name, i, (int)rr);
            CHECK(got > 0, "%s corrupt %zu: decoding must resume after flush (resynchronisation)", name, i);
            for (k = 0; k < total * ref->ch; k++) if (!(out[k] == out[k])) { CHECK(0, "NaN in output"); break; }
        }
        free(out); maktabasauti_decoder_close(d);
        CHECK(m.live == 0 && m.bad == 0, "%s corrupt %zu: leak", name, i);
    }
    free(w);
}

/* ---- MP4 NA ZYWO (wejscie nieprzewijalne) ---- */
typedef struct { uint32_t type; size_t off, size; } TBox;
static size_t top_boxes(const uint8_t *b, size_t n, TBox *o, size_t max) {
    size_t pos = 0, k = 0;
    while (pos + 8 <= n && k < max) {
        size_t sz = ((size_t)b[pos] << 24) | ((size_t)b[pos + 1] << 16) | ((size_t)b[pos + 2] << 8) | b[pos + 3];
        o[k].type = ((uint32_t)b[pos + 4] << 24) | ((uint32_t)b[pos + 5] << 16) | ((uint32_t)b[pos + 6] << 8) | b[pos + 7]; o[k].off = pos; o[k].size = sz; k++;
        if (sz < 8) break;
        pos += sz;
    }
    return k;
}
/* czy otwarcie z wejscia nieprzewijalnego (i ewentualnie przewijalnego) zwraca ten sam PCM; zwraca kod koncowy */
static maktabasauti_result_t live_decode(const uint8_t *b, size_t n, int seekable, size_t chunk, float *out, size_t cap, size_t ch, size_t *got, maktabasauti_result_t *open_rc, Mem *m, int *closed) {
    Src s; maktabasauti_decoder_t *d = NULL; long retries = 0; maktabasauti_result_t rr;
    memset(&s, 0, sizeof s); memset(m, 0, sizeof *m);
    s.data = b; s.n = n; s.seekable = seekable; s.size_known = seekable; s.max_chunk = chunk;
    *got = 0; *open_rc = open_src(&s, m, &d, NULL);
    if (*open_rc != MAKTABASAUTI_OK) { *closed = s.closed; return *open_rc; }
    rr = drain(d, out, ch, cap, 1000, got, &retries);
    maktabasauti_decoder_close(d); *closed = s.closed;
    return rr;
}
/* obciecie strumienia w wielu miejscach (naglowek pudelka, srodek moof, srodek probki, granice pudelek): wynik to prefiks PCM, nigdy blad pamieci/zawieszenie */
static void test_live_truncation(const uint8_t *b, size_t n, const Ref *ref, const char *name) {
    TBox bx[512]; const size_t nb = top_boxes(b, n, bx, 512); size_t i, k; size_t cuts[2048], nc = 0;
    float *out = (float *)malloc((ref->frames + 4096) * ref->ch * sizeof(float));
    for (i = 0; i < nb; i++) {
        const size_t o = bx[i].off, e = bx[i].off + bx[i].size;
        const size_t pts[] = { o + 1, o + 4, o + 7, o + 8, o + 9, o + 15, o + 16, o + bx[i].size / 3, o + bx[i].size / 2, e - 1, e };
        for (k = 0; k < sizeof pts / sizeof *pts && nc < 2040; k++) if (pts[k] > 0 && pts[k] <= n) cuts[nc++] = pts[k];
    }
    for (i = 0; i < nc; i++) {
        Mem m; size_t got = 0; maktabasauti_result_t orc, rr; int closed = 0;
        rr = live_decode(b, cuts[i], 0, (i % 3) ? 0 : 13, out, ref->frames + 4096, ref->ch, &got, &orc, &m, &closed);
        CHECK(m.live == 0 && m.bad == 0, "%s cut %zu: leak (live=%zu)", name, cuts[i], m.live);
        if (orc != MAKTABASAUTI_OK) { CHECK(closed == 0, "%s cut %zu: failed open must leave the input with the caller", name, cuts[i]); continue; }
        CHECK(rr == MAKTABASAUTI_EOF || rr == MAKTABASAUTI_E_TRUNCATED, "%s cut %zu: ended with %d", name, cuts[i], (int)rr);
        CHECK(got <= ref->frames && same(out, ref->pcm, got * ref->ch), "%s cut %zu: decoded PCM is not a prefix of the reference (%zu frames)", name, cuts[i], got);
    }
    free(out);
}
/* fMP4 z mdat przed moof: seekable dziala (ten sam PCM), nieprzewijalne = czysty blad E_UNSUPPORTED_FEATURE (bez cofania nie da sie odczytac) */
static void test_live_mdat_first(const char *dir, const Ref *ref) {
    size_t n; uint8_t *b = slurp(dir, "lc_frag_mdatfirst.m4a", &n); Mem m; size_t got = 0; maktabasauti_result_t orc, rr; int closed = 0;
    float *out = (float *)malloc((ref->frames + 4096) * ref->ch * sizeof(float));
    rr = live_decode(b, n, 1, 0, out, ref->frames + 4096, ref->ch, &got, &orc, &m, &closed);
    CHECK(orc == MAKTABASAUTI_OK && rr == MAKTABASAUTI_EOF && got == ref->frames && same(out, ref->pcm, got * ref->ch), "mdat-first, seekable: PCM differs (open %d, end %d, %zu frames)", (int)orc, (int)rr, got);
    rr = live_decode(b, n, 0, 0, out, ref->frames + 4096, ref->ch, &got, &orc, &m, &closed);
    CHECK(orc == MAKTABASAUTI_E_UNSUPPORTED_FEATURE && m.live == 0, "mdat-first, live: expected E_UNSUPPORTED_FEATURE at open (open %d, end %d)", (int)orc, (int)rr);
    free(out); free(b);
}
/* wrogie naglowki (uszkodzone pudelko: drain wznawia po E_CORRUPT_DATA przez flush, a MP4 nie ma synchronizacji, wiec strumien konczy sie EOF; deklaracje rozmiaru: blad): moof deklarujacy 1 GiB (odmowa bez alokacji), moof deklarujacy 10 MiB i urwany po 100 B (pamiec nie idzie za deklaracja), pudelko o rozmiarze < naglowka */
static void test_live_hostile(const uint8_t *b, size_t n, const Ref *ref, const char *name) {
    TBox bx[512]; const size_t nb = top_boxes(b, n, bx, 512); size_t moof = 0, i, k; uint8_t *w = (uint8_t *)malloc(n + 256);
    float *out = (float *)malloc((ref->frames + 4096) * ref->ch * sizeof(float));
    for (i = 0, k = 0; i < nb; i++) if (bx[i].type == 0x6d6f6f66u && ++k == 2) { moof = bx[i].off; break; }          /* po pierwszym kompletnym fragmencie: otwarcie sie udaje, wrogi jest drugi */
    CHECK(moof > 0, "%s: no moof", name);
    for (k = 0; k < 4; k++) {
        Mem m; size_t got = 0; maktabasauti_result_t orc, rr; int closed = 0; size_t len = moof;
        memcpy(w, b, moof);
        if (k == 0) { w[len++] = 0x40; w[len++] = 0; w[len++] = 0; w[len++] = 0; memcpy(w + len, "moof", 4); len += 4; }                 /* 1 GiB */
        if (k == 1) { w[len++] = 0; w[len++] = 0xA0; w[len++] = 0; w[len++] = 0; memcpy(w + len, "moof", 4); len += 4; memset(w + len, 0x55, 100); len += 100; }   /* 10 MiB, 100 B */
        if (k == 2) { w[len++] = 0; w[len++] = 0; w[len++] = 0; w[len++] = 4; memcpy(w + len, "moof", 4); len += 4; }                  /* rozmiar < naglowka */
        if (k == 3) { w[len++] = 0; w[len++] = 0; w[len++] = 0; w[len++] = 1; memcpy(w + len, "moof", 4); len += 4; memset(w + len, 0xff, 8); len += 8; }   /* rozmiar 64-bitowy = 2^64-1 */
        rr = live_decode(w, len, 0, 0, out, ref->frames + 4096, ref->ch, &got, &orc, &m, &closed);
        CHECK(m.live == 0 && m.bad == 0 && m.peak < ((size_t)3 << 20), "%s hostile %zu: leak or memory peak %zu", name, k, m.peak);
        CHECK(orc == MAKTABASAUTI_OK && rr != MAKTABASAUTI_OK && (k >= 2 || rr != MAKTABASAUTI_EOF) && got > 0 && same(out, ref->pcm, got * ref->ch), "%s hostile %zu: open %d, end %d, %zu frames", name, k, (int)orc, (int)rr, got);
    }
    free(w); free(out);
}
/* wejscie bez konca (generator, ~100 MB cyklicznego fMP4): pamiec nie zalezy od dlugosci; bez gapless, bo lista edycji ucielaby wyjscie */
static void test_live_memory(const uint8_t *b, size_t n, const char *name) {
    Src s; Mem m; maktabasauti_decoder_t *d = NULL; static float out[4096 * 8]; size_t got, total = 0;
    memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
    s.gen = b; s.gen_len = n; s.gen_total = (uint64_t)100 << 20; s.max_chunk = 4096;
    g_open_flags = MAKTABASAUTI_OPEN_NO_GAPLESS;
    CHECK(open_src(&s, &m, &d, NULL) == MAKTABASAUTI_OK, "%s live generator: open", name);
    g_open_flags = 0;
    if (!d) return;
    for (;;) { maktabasauti_result_t r = maktabasauti_decoder_read(d, out, 4096, MAKTABASAUTI_FORMAT_F32, &got); if (r != MAKTABASAUTI_OK) { CHECK(r == MAKTABASAUTI_EOF, "%s live generator: code %d", name, (int)r); break; } total += got; if (total / 1024 >= 5000) break; }
    CHECK(total > 1000000, "%s live generator: decoded only %zu frames", name, total);
    CHECK(m.peak < ((size_t)1 << 20) + ((size_t)1 << 19), "%s live generator: memory peak %zu bytes for an unbounded source (must not depend on length)", name, m.peak);
    maktabasauti_decoder_close(d);
    CHECK(m.live == 0, "%s live generator: leak", name);
}
/* OPEN_NO_GAPLESS: bez przyciecia wyjscie jest dluzsze o priming + dopelnienie, PCM nie traci zadnej probki */
static void test_no_gapless(const uint8_t *b, size_t n, const Ref *ref, const char *name) {
    Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info; size_t got = 0; long retries = 0; float *out;
    memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = b; s.n = n; s.seekable = 1; s.size_known = 1;
    g_open_flags = MAKTABASAUTI_OPEN_NO_GAPLESS; CHECK(open_src(&s, &m, &d, NULL) == MAKTABASAUTI_OK, "%s no-gapless: open", name); g_open_flags = 0;
    if (!d) return;
    info.struct_size = sizeof info; CHECK(maktabasauti_decoder_get_info(d, &info) == MAKTABASAUTI_OK && info.total_frames >= ref->frames, "%s no-gapless: length %llu vs trimmed %zu", name, (unsigned long long)info.total_frames, ref->frames);
    out = (float *)malloc((size_t)(info.total_frames + 4096) * ref->ch * sizeof(float));
    CHECK(drain(d, out, ref->ch, (size_t)info.total_frames + 4096, 1000, &got, &retries) == MAKTABASAUTI_EOF && got == info.total_frames, "%s no-gapless: %zu frames", name, got);
    free(out); maktabasauti_decoder_close(d);
}

/* 6: pamiec: generator ~100 MB bez przechowywania */
static void test_memory(const uint8_t *b, size_t n, const char *name) {
    Src s; Mem m; maktabasauti_decoder_t *d = NULL; static float out[4096 * 8]; size_t got, total = 0; uint64_t frames_target = 5000;
    /* granice ramek: generator oddaje caly plik cyklicznie; w miejscu sklejenia dekoder widzi ciagly strumien ADTS (te same naglowki) */
    memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
    s.gen = b; s.gen_len = n; s.gen_total = (uint64_t)100 << 20; s.max_chunk = 4096;
    CHECK(open_src(&s, &m, &d, NULL) == MAKTABASAUTI_OK, "%s generator: open", name);
    if (!d) return;
    for (;;) { maktabasauti_result_t r = maktabasauti_decoder_read(d, out, 4096, MAKTABASAUTI_FORMAT_F32, &got); if (r != MAKTABASAUTI_OK) { CHECK(r == MAKTABASAUTI_EOF || r == MAKTABASAUTI_E_CORRUPT_DATA, "%s generator: code %d", name, (int)r); break; } total += got; if (total / 1024 >= frames_target) break; }
    CHECK(total > 1000000, "%s generator: decoded only %zu frames", name, total);
    CHECK(m.peak < ((size_t)1 << 20) + ((size_t)1 << 19), "%s generator: memory peak %zu bytes for an unbounded source (must not depend on length)", name, m.peak);
    CHECK(s.gen_done < ((uint64_t)100 << 20), "generator not exhausted");
    maktabasauti_decoder_close(d);
    CHECK(m.live == 0, "%s generator: leak", name);
}

/* wejscie przewijalne: szczyt pamieci nie rosnie z dlugoscia pliku */
static void test_memory_seekable(const uint8_t *b, size_t n, const char *name) {
    size_t reps[2] = { 1, 150 }, peaks[2] = { 0, 0 }, maxreq[2] = { 0, 0 }, i, r, j;
    for (i = 0; i < 2; i++) {
        uint8_t *big = (uint8_t *)malloc(n * reps[i]); Src s; Mem m; maktabasauti_decoder_t *d = NULL; static float out[4096 * 8]; size_t got;
        for (r = 0; r < reps[i]; r++) memcpy(big + r * n, b, n);
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = big; s.n = n * reps[i]; s.seekable = 1; s.size_known = 1;
        CHECK(open_src(&s, &m, &d, NULL) == MAKTABASAUTI_OK, "%s x%zu: open", name, reps[i]);
        if (d) {
            for (;;) { maktabasauti_result_t rr = maktabasauti_decoder_read(d, out, 4096, MAKTABASAUTI_FORMAT_F32, &got); if (rr != MAKTABASAUTI_OK) break; }
            for (j = 0; j < 20; j++) { uint64_t act, tot; maktabasauti_stream_info_t info; info.struct_size = sizeof info; maktabasauti_decoder_get_info(d, &info); tot = info.total_frames;
                (void)maktabasauti_decoder_seek(d, tot * (j * 37 % 100) / 100, MAKTABASAUTI_SEEK_EXACT, &act); (void)maktabasauti_decoder_read(d, out, 2000, MAKTABASAUTI_FORMAT_F32, &got); }
            maktabasauti_decoder_close(d);
        }
        peaks[i] = m.peak; maxreq[i] = m.max_request; CHECK(m.live == 0, "%s x%zu: leak", name, reps[i]);
        free(big);
    }
    CHECK(peaks[1] <= peaks[0] + ((size_t)9 << 20), "%s: memory peak grows with length: %zu B (1x) vs %zu B (150x)", name, peaks[0], peaks[1]);
    CHECK(maxreq[1] < ((size_t)4 << 20), "%s: a single allocation of %zu bytes (file is %zu bytes)", name, maxreq[1], n * 150);
}

/* 7: seek dokladny */
static void test_seek(const uint8_t *b, size_t n, const Ref *ref, const char *name, int via_file) {
    Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info; uint32_t rng = 777u + (uint32_t)via_file; size_t i, total = ref->frames, ch = ref->ch;
    static float y[4096 * 8]; char path[] = "/tmp/mks_stream_test_XXXXXX"; int fd = -1;
    memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
    if (via_file) {
        maktabasauti_allocator_t a = mem_allocator(&m); maktabasauti_open_params_t p; FILE *f;
        const char *tmp = getenv("TMPDIR"); char base[512]; snprintf(base, sizeof base, "%s/mks_stream_XXXXXX", tmp && *tmp ? tmp : "/tmp");
        fd = mkstemp(base); if (fd < 0) { CHECK(0, "mkstemp"); return; }
        f = fdopen(fd, "wb"); fwrite(b, 1, n, f); fclose(f); strcpy(path, base);
        memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION; p.allocator = &a;
        assert(maktabasauti_io_from_file(path, &a, &p.io) == MAKTABASAUTI_OK);
        CHECK(maktabasauti_decoder_open(&p, &d) == MAKTABASAUTI_OK, "%s: open from file", name);
    } else { s.data = b; s.n = n; s.seekable = 1; s.size_known = 1; CHECK(open_src(&s, &m, &d, NULL) == MAKTABASAUTI_OK, "%s: open", name); }
    if (!d) { if (via_file) remove(path); return; }
    info.struct_size = sizeof info; maktabasauti_decoder_get_info(d, &info);
    CHECK(info.total_frames == total, "%s: total %llu vs reference %zu", name, (unsigned long long)info.total_frames, total);
    for (i = 0; i < 80; i++) {
        uint64_t pos, act = 0; size_t want, got = 0;
        switch (i % 8) {
        case 0: pos = 0; break;
        case 1: pos = total; break;
        case 2: pos = total - 1; break;
        case 3: pos = (xr(&rng) % (total / 1024 + 1)) * 1024; break;                   /* granica ramki */
        case 4: pos = (xr(&rng) % (total / 1024 + 1)) * 1024 + 1; break;
        case 5: pos = (xr(&rng) % (total / 1024 + 1)) * 1024 + 1023; break;
        default: pos = xr(&rng) % (total + 1); break;
        }
        if (pos > total) pos = total;
        CHECK(maktabasauti_decoder_seek(d, pos, MAKTABASAUTI_SEEK_EXACT, &act) == MAKTABASAUTI_OK && act == pos, "%s: seek %llu", name, (unsigned long long)pos);
        want = 1500 < total - pos ? 1500 : (size_t)(total - pos);
        { maktabasauti_result_t r = maktabasauti_decoder_read(d, y, want ? want : 1, MAKTABASAUTI_FORMAT_F32, &got);
          if (want == 0) CHECK(r == MAKTABASAUTI_EOF, "%s: read at the end must give EOF (%d)", name, (int)r);
          else CHECK(r == MAKTABASAUTI_OK && got > 0 && same(y, ref->pcm + pos * ch, got * ch), "%s: PCM after seek to %llu differs from the reference (code %d, %zu frames)", name, (unsigned long long)pos, (int)r, got); }
    }
    {   uint64_t act, before; maktabasauti_decoder_tell(d, &before);
        CHECK(maktabasauti_decoder_seek(d, total + 1, MAKTABASAUTI_SEEK_EXACT, &act) == MAKTABASAUTI_E_OUT_OF_RANGE, "%s: seek beyond the end", name);
        { uint64_t after; maktabasauti_decoder_tell(d, &after); CHECK(before == after, "%s: failed seek changed the position", name); }
        CHECK(maktabasauti_decoder_reset(d) == MAKTABASAUTI_OK, "reset");
        { size_t g2 = 0; CHECK(maktabasauti_decoder_read(d, y, 2048, MAKTABASAUTI_FORMAT_F32, &g2) == MAKTABASAUTI_OK && same(y, ref->pcm, g2 * ch), "%s: PCM after reset", name); }
    }
    maktabasauti_decoder_close(d);
    CHECK(m.live == 0 && m.bad == 0, "%s: leak after seeks (live %zu)", name, m.live);
    if (via_file) remove(path);
}

/* 7b: dlugi plik: pula punktow kontrolnych sie przepelnia (stride rosnie); seek nadal dokladny */
static void test_seek_long(const uint8_t *b, size_t n, const char *name) {
    const size_t reps = 150; uint8_t *big = (uint8_t *)malloc(n * reps); size_t r, i; Ref ref; Src s; Mem m; maktabasauti_decoder_t *d = NULL; uint32_t rng = 99; static float y[4096 * 8];
    for (r = 0; r < reps; r++) memcpy(big + r * n, b, n);
    ref = reference(big, n * reps);
    memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = big; s.n = n * reps; s.seekable = 1; s.size_known = 1;
    CHECK(open_src(&s, &m, &d, NULL) == MAKTABASAUTI_OK, "%s long: open", name);
    if (d) {
        size_t got; for (;;) { maktabasauti_result_t rr = maktabasauti_decoder_read(d, y, 4096, MAKTABASAUTI_FORMAT_F32, &got); if (rr != MAKTABASAUTI_OK) break; }       /* odczyt do konca zapisuje punkty kontrolne i wypelnia pule */
        for (i = 0; i < 60; i++) {
            uint64_t pos = (uint64_t)(xr(&rng) % (uint32_t)(ref.frames + 1)), act = 0; size_t g = 0, want = 1200 < ref.frames - pos ? 1200 : (size_t)(ref.frames - pos);
            CHECK(maktabasauti_decoder_seek(d, pos, MAKTABASAUTI_SEEK_EXACT, &act) == MAKTABASAUTI_OK && act == pos, "seek");
            if (want) CHECK(maktabasauti_decoder_read(d, y, want, MAKTABASAUTI_FORMAT_F32, &g) == MAKTABASAUTI_OK && same(y, ref.pcm + pos * ref.ch, g * ref.ch), "%s long: PCM after seek %llu differs", name, (unsigned long long)pos);
        }
        maktabasauti_decoder_close(d); CHECK(m.live == 0, "leak");
    }
    free(ref.pcm); free(big);
}

/* 8: adts.scan=off, 9: ID3v1 za ostatnia ramka */
static void test_options_and_tail(const uint8_t *b, size_t n, const Ref *ref, const char *name) {
    Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info; static float y[4096 * 8]; size_t got = 0; uint64_t act = 0; uint8_t *t;
    memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = b; s.n = n; s.seekable = 1; s.size_known = 1;
    CHECK(open_src(&s, &m, &d, "off") == MAKTABASAUTI_OK, "%s scan=off: open", name);
    if (d) {
        info.struct_size = sizeof info; maktabasauti_decoder_get_info(d, &info);
        CHECK(info.total_frames == MAKTABASAUTI_UNKNOWN_LENGTH && (info.flags & MAKTABASAUTI_STREAM_SEEKABLE), "%s scan=off: unknown length but seekable expected", name);
        CHECK(maktabasauti_decoder_seek(d, ref->frames / 2, MAKTABASAUTI_SEEK_EXACT, &act) == MAKTABASAUTI_OK && act == ref->frames / 2, "scan=off seek");
        CHECK(maktabasauti_decoder_read(d, y, 1000, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && same(y, ref->pcm + (ref->frames / 2) * ref->ch, got * ref->ch), "%s scan=off: PCM after seek", name);
        CHECK(maktabasauti_decoder_seek(d, ref->frames * 3, MAKTABASAUTI_SEEK_EXACT, &act) == MAKTABASAUTI_OK && act <= ref->frames, "%s scan=off: seek beyond the end must land at the end", name);
        maktabasauti_decoder_close(d);
    }
    t = (uint8_t *)malloc(n + 128); memcpy(t, b, n); memset(t + n, 0, 128); memcpy(t + n, "TAG", 3);                            /* ID3v1 */
    {   Src s2; Mem m2; maktabasauti_decoder_t *d2 = NULL; float *out; size_t g2 = 0; long retries = 0; maktabasauti_result_t rr;
        memset(&s2, 0, sizeof s2); memset(&m2, 0, sizeof m2); s2.data = t; s2.n = n + 128; s2.seekable = 1; s2.size_known = 1;
        CHECK(open_src(&s2, &m2, &d2, NULL) == MAKTABASAUTI_OK, "%s +ID3v1: open", name);
        if (d2) {
            maktabasauti_stream_info_t i2; i2.struct_size = sizeof i2; maktabasauti_decoder_get_info(d2, &i2);
            CHECK(i2.total_frames == ref->frames, "%s +ID3v1: length %llu vs %zu", name, (unsigned long long)i2.total_frames, ref->frames);
            out = (float *)malloc((ref->frames + 4096) * ref->ch * sizeof(float));
            rr = drain(d2, out, ref->ch, ref->frames + 4096, 1000, &g2, &retries);
            CHECK(rr == MAKTABASAUTI_EOF && g2 == ref->frames && same(out, ref->pcm, g2 * ref->ch), "%s +ID3v1: trailing tag must be ignored (code %d, %zu frames)", name, (int)rr, g2);
            free(out); maktabasauti_decoder_close(d2);
        } }
    free(t);
}

/* 10: ID3v2 przed ADTS (rozpoznanie przez rejestr domyslny, wiec takze kolejnosc wzgledem MP3): maly znacznik, ze stopka, dwa z rzedu, DUZY (> prefiks sond 16 KiB), plus ID3v1 na koncu */
static size_t make_id3(uint8_t *o, size_t body, int footer) {
    size_t n = 0; o[n++] = 'I'; o[n++] = 'D'; o[n++] = '3'; o[n++] = 4; o[n++] = 0; o[n++] = footer ? 0x10 : 0;
    o[n++] = (uint8_t)((body >> 21) & 0x7f); o[n++] = (uint8_t)((body >> 14) & 0x7f); o[n++] = (uint8_t)((body >> 7) & 0x7f); o[n++] = (uint8_t)(body & 0x7f);
    memcpy(o + n, "TIT2", 4); n += 4; memset(o + n, 0x20, body > 4 ? body - 4 : 0); n += body > 4 ? body - 4 : 0;
    if (footer) { memcpy(o + n, "3DI\4\0\x10", 6); memset(o + n + 6, 0, 4); n += 10; }
    return n;
}
static void test_id3v2(const uint8_t *b, size_t n, const Ref *ref, const char *name) {
    static const struct { const char *what; size_t body1; int footer; size_t body2; int tail; } v[] = {
        { "small tag", 40, 0, 0, 0 }, { "tag with footer", 100, 1, 0, 0 }, { "two tags", 60, 0, 30, 0 }, { "tag + ID3v1 tail", 50, 0, 0, 1 }, { "big tag (40 KiB, beyond the probe prefix)", 40000, 0, 0, 0 },
    };
    size_t i, mode;
    for (i = 0; i < sizeof v / sizeof *v; i++) {
        uint8_t *t = (uint8_t *)malloc(n + 90000), *p = t; size_t tn;
        p += make_id3(p, v[i].body1, v[i].footer);
        if (v[i].body2) p += make_id3(p, v[i].body2, 0);
        memcpy(p, b, n); p += n;
        if (v[i].tail) { memset(p, 0, 128); memcpy(p, "TAG", 3); p += 128; }
        tn = (size_t)(p - t);
        for (mode = 0; mode < 2; mode++) {                                         /* 0 = wejscie przewijalne, 1 = nieprzewijalne */
            Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info; float *out; size_t got = 0; long retries = 0; maktabasauti_result_t r, rr;
            memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = t; s.n = tn; s.seekable = (int)(mode == 0); s.size_known = s.seekable;
            r = open_src(&s, &m, &d, NULL);
            if (mode == 1 && v[i].body1 > 16000) { CHECK(r != MAKTABASAUTI_OK && !d && s.closed == 0 && m.live == 0, "%s %s non-seekable: a tag beyond the reader window must fail cleanly (%d)", name, v[i].what, (int)r); continue; }
            CHECK(r == MAKTABASAUTI_OK, "%s %s (%s): open failed (%d)", name, v[i].what, mode ? "non-seekable" : "seekable", (int)r);
            if (r != MAKTABASAUTI_OK) continue;
            info.struct_size = sizeof info; maktabasauti_decoder_get_info(d, &info);
            CHECK(strcmp(info.codec_name, "aac") == 0 && strcmp(info.container_name, "adts") == 0, "%s %s: recognised as %s/%s", name, v[i].what, info.codec_name, info.container_name);
            if (mode == 0) CHECK(info.total_frames == ref->frames, "%s %s: length %llu vs %zu", name, v[i].what, (unsigned long long)info.total_frames, ref->frames);
            out = (float *)malloc((ref->frames + 4096) * ref->ch * sizeof(float));
            rr = drain(d, out, ref->ch, ref->frames + 4096, 1000, &got, &retries);
            CHECK((rr == MAKTABASAUTI_EOF || (mode == 1 && rr == MAKTABASAUTI_E_CORRUPT_DATA && v[i].tail)) && got == ref->frames && same(out, ref->pcm, got * ref->ch), "%s %s (%s): PCM differs (code %d, %zu/%zu frames)", name, v[i].what, mode ? "non-seekable" : "seekable", (int)rr, got, ref->frames);
            if (mode == 0) {                                                       /* seek i reset przez znacznik */
                uint64_t act; float y[2048 * 8]; size_t g = 0;
                CHECK(maktabasauti_decoder_seek(d, ref->frames / 2, MAKTABASAUTI_SEEK_EXACT, &act) == MAKTABASAUTI_OK && maktabasauti_decoder_read(d, y, 1000, MAKTABASAUTI_FORMAT_F32, &g) == MAKTABASAUTI_OK &&
                      same(y, ref->pcm + (ref->frames / 2) * ref->ch, g * ref->ch), "%s %s: seek", name, v[i].what);
                CHECK(maktabasauti_decoder_reset(d) == MAKTABASAUTI_OK && maktabasauti_decoder_read(d, y, 1000, MAKTABASAUTI_FORMAT_F32, &g) == MAKTABASAUTI_OK && same(y, ref->pcm, g * ref->ch), "%s %s: reset (the tag must be skipped again)", name, v[i].what);
            }
            free(out); maktabasauti_decoder_close(d);
            CHECK(m.live == 0 && m.bad == 0, "%s %s: leak", name, v[i].what);
        }
        free(t);
    }
}

/* 11: zmiana parametrow w srodku strumienia (E_FORMAT_CHANGED): dwa pliki sklejone; PCM pierwszego identyczny ze wzorcem, potem E_FORMAT_CHANGED (0 ramek), nowy opis strumienia (czestotliwosc, kanaly,
 * dlugosc nieznana, bez seek), PCM drugiego identyczny ze wzorcem odczytanym osobno (swiezy stan dekodera; HE-AAC v2: zalegly blok pierwszego wydany z cisza jako wyprzedzeniem) */
static void test_format_change(const char *dir, const char *fa, const char *fb) {
    size_t na, nb, mode; uint8_t *a = slurp(dir, fa, &na), *b = slurp(dir, fb, &nb), *ab = (uint8_t *)malloc(na + nb); Ref ra = reference(a, na), rb = reference(b, nb);
    memcpy(ab, a, na); memcpy(ab + na, b, nb);
    for (mode = 0; mode < 2; mode++) {
        Src s; Mem m; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info; maktabasauti_result_t r; size_t got = 0, g = 0, cap = ra.frames + rb.frames + 8192;
        float *outa = (float *)malloc(cap * 8 * sizeof(float)), *outb = (float *)malloc(cap * 8 * sizeof(float)); int changed = 0; uint64_t act;
        memset(&s, 0, sizeof s); memset(&m, 0, sizeof m); s.data = ab; s.n = na + nb; s.seekable = (int)(mode == 0); s.size_known = s.seekable;
        r = open_src(&s, &m, &d, NULL);
        CHECK(r == MAKTABASAUTI_OK, "%s + %s (%s): open failed (%d)", fa, fb, mode ? "non-seekable" : "seekable", (int)r);
        if (r != MAKTABASAUTI_OK) { free(outa); free(outb); continue; }
        info.struct_size = sizeof info; maktabasauti_decoder_get_info(d, &info);
        CHECK(info.sample_rate == ra.rate && info.channels == ra.ch, "%s + %s: the first stream parameters", fa, fb);
        for (;;) {                                                                 /* faza 1: do zmiany */
            r = maktabasauti_decoder_read(d, outa + got * ra.ch, 1000, MAKTABASAUTI_FORMAT_F32, &g);
            got += g;
            if (r == MAKTABASAUTI_E_FORMAT_CHANGED) { changed = 1; break; }
            if (r != MAKTABASAUTI_OK) break;
            if (got > ra.frames + 4096) break;
        }
        CHECK(changed, "%s + %s (%s): E_FORMAT_CHANGED expected, got %d after %zu frames", fa, fb, mode ? "non-seekable" : "seekable", (int)r, got);
        CHECK(got == ra.frames && same(outa, ra.pcm, got * ra.ch), "%s + %s (%s): PCM before the change differs (%zu vs %zu frames)", fa, fb, mode ? "non-seekable" : "seekable", got, ra.frames);
        if (changed) {
            info.struct_size = sizeof info; CHECK(maktabasauti_decoder_get_info(d, &info) == MAKTABASAUTI_OK, "info");
            CHECK(info.sample_rate == rb.rate && info.channels == rb.ch, "%s + %s: stream info after the change (%u Hz x %u vs %u Hz x %zu)", fa, fb, info.sample_rate, info.channels, rb.rate, rb.ch);
            CHECK(info.total_frames == MAKTABASAUTI_UNKNOWN_LENGTH && !(info.flags & MAKTABASAUTI_STREAM_SEEKABLE), "%s + %s: length must be unknown and seeking off after the change", fa, fb);
            CHECK(maktabasauti_decoder_seek(d, 0, MAKTABASAUTI_SEEK_EXACT, &act) == MAKTABASAUTI_E_NOT_SEEKABLE, "seek after the change");
            CHECK(maktabasauti_decoder_reset(d) == MAKTABASAUTI_E_NOT_SEEKABLE, "reset after the change");
            { size_t gb = 0; long retries = 0; r = drain(d, outb, rb.ch, cap, 1000, &gb, &retries);
              CHECK((r == MAKTABASAUTI_EOF) && gb == rb.frames && same(outb, rb.pcm, gb * rb.ch), "%s + %s (%s): PCM after the change differs (code %d, %zu vs %zu frames)", fa, fb, mode ? "non-seekable" : "seekable", (int)r, gb, rb.frames); }
        }
        maktabasauti_decoder_close(d);
        CHECK(m.live == 0 && m.bad == 0, "%s + %s: leak", fa, fb);
        free(outa); free(outb);
    }
    free(ra.pcm); free(rb.pcm); free(a); free(b); free(ab);
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "tests/data"; size_t i;
    for (i = 0; i < NFILES; i++) {
        const char *only = getenv("MKS_STREAM_ONLY");
        if (only && *only && !strstr(files[i], only)) continue;                                   /* przyspieszenie iteracji: tylko pliki z podanym fragmentem nazwy */
        size_t n; uint8_t *b = slurp(dir, files[i], &n); Ref ref = reference(b, n);
        g_loas = strstr(files[i], ".loas") != NULL; g_adif = strstr(files[i], ".adif") != NULL; g_mp4 = strstr(files[i], ".m4a") != NULL; if (g_mp4) g_adif = 1; g_frag = strstr(files[i], "_frag") != NULL; g_live_ok = g_frag || strstr(files[i], "_fast") != NULL;   /* MP4: kontrole jak dla ADIF (granice jednostek niewidoczne w pliku) */ g_hmin = g_loas ? 3u : 7u;
        printf("%s: %zu bytes, %zu frames x %zu ch @ %u Hz\n", files[i], n, ref.frames, ref.ch, ref.rate);
        test_inputs(b, n, &ref, files[i]);
        test_block_sweep(b, n, files[i]);
        test_open_block(b, n, &ref, files[i]);
        test_truncation(b, n, &ref, files[i]);
        test_corruption(b, n, &ref, files[i]);
        if (g_mp4) test_no_gapless(b, n, &ref, files[i]);
        if (g_frag) { test_live_truncation(b, n, &ref, files[i]); test_live_hostile(b, n, &ref, files[i]); test_live_memory(b, n, files[i]); test_live_mdat_first(dir, &ref); }
        if (g_mp4 && !g_frag && g_live_ok) test_live_truncation(b, n, &ref, files[i]);
        if (!g_adif) test_memory(b, n, files[i]);                              /* generator: cykliczne sklejanie pliku jest poprawnym strumieniem tylko dla ADTS/LOAS */
        test_seek(b, n, &ref, files[i], 0);
        test_seek(b, n, &ref, files[i], 1);
        if (!g_mp4) test_options_and_tail(b, n, &ref, files[i]);
        if (!g_adif && !g_loas) test_id3v2(b, n, &ref, files[i]);                       /* tylko ADTS */
        if (!g_mp4 && (i == 0 || i == 2)) { test_memory_seekable(b, n, files[i]); test_seek_long(b, n, files[i]); }          /* LC stereo i HE-AAC (duzy stan) */
        free(ref.pcm); free(b);
    }
    {   static const char *const pairs[][2] = { { "lc.aac", "lc_mono.aac" }, { "lc.aac", "lc44.aac" }, { "lc.aac", "he1.aac" }, { "he2.aac", "lc.aac" }, { "he1.aac", "lc_mono.aac" }, { "lc_mono.aac", "he2.aac" },
            { "m51.aac", "lc.aac" }, { "lc.aac", "m51.aac" }, { "he2.aac", "he1.aac" }, { "lc.loas", "he1.loas" }, { "lc.loas", "lc44.loas" }, { "he2.loas", "lc44.loas" }, { "he1.loas", "he2.loas" } };
        size_t k; const char *only = getenv("MKS_STREAM_ONLY");
        for (k = 0; k < sizeof pairs / sizeof *pairs; k++) if (!only || !*only || !strcmp(only, "pairs") || strstr(pairs[k][0], only) || strstr(pairs[k][1], only)) test_format_change(dir, pairs[k][0], pairs[k][1]);
    }
    printf("AAC streaming: %s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}
