/* MaktabaSauti: odpornosc rozpoznawania i otwierania na NIEPOPRAWNE wejscie (io), ktore nie przestrzega kontraktu, oraz na skrajne wywolania uslug hosta przez modul.
 *
 * Rdzen jest pierwsza linia obrony: wejscie to kod uzytkownika (siec, plik, pamiec, kod osadzony), ktory moze miec bledy.  Ten test sprawdza, ze rdzen
 *   - nie ufa wartosci zwracanej z io.read (wiecej niz zazadano, wartosci ujemne, skrajne) ani z io.size (ujemny, ogromny, rozny od rzeczywistego),
 *   - nie zajmuje pamieci proporcjonalnej do DEKLAROWANEGO rozmiaru bez pokrycia w danych (kazde zadanie alokacji jest sledzone i ograniczone),
 *   - poprawnie obsluguje odczyty po jednym bajcie i bledy io w kazdym punkcie (kod bledu, brak wycieku, io.close NIE wolane po bledzie open),
 *   - nie wpada w niezdefiniowane zachowanie przy skrajnych argumentach uslug hosta (seek INT64_MIN/MAX z kazdym whence, read/peek SIZE_MAX, peek wiekszy niz bufor).
 * Uruchamiany z ASan+UBSan (-fno-sanitize-recover) i bez sanitizerow.  usage: test_maktabasauti_hostile_io [FILE]   (FILE: opcjonalny plik audio do porownania odczytu 1 bajt/wywolanie) */
#include "maktabasauti/maktabasauti.h"
#include "maktabasauti/maktabasauti_codec.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); g_failures++; } } while (0)

/* ---- alokator sledzacy ---- */
typedef struct { long live, bad; size_t max_request; size_t limit; } Track;
static void *MAKTABASAUTI_CALL t_alloc(void *u, size_t n) { Track *t = (Track *)u; void *p; if (n > t->max_request) t->max_request = n; if (t->limit && n > t->limit) return NULL; p = malloc(n); if (p) t->live++; return p; }
static void MAKTABASAUTI_CALL t_free(void *u, void *p) { Track *t = (Track *)u; if (!p) { t->bad++; return; } t->live--; free(p); }
static maktabasauti_allocator_t make_alloc(Track *t) { maktabasauti_allocator_t a; memset(&a, 0, sizeof a); a.struct_size = sizeof a; a.user = t; a.alloc = t_alloc; a.realloc = NULL; a.free = t_free; return a; }

/* ---- wejscie o konfigurowalnej wrogosci ---- */
enum { H_OK, H_OVERRUN_BY_1, H_OVERRUN_HUGE, H_NEG_HUGE, H_ONE_BYTE, H_FAIL_AT, H_EOF_EARLY };
typedef struct {
    const uint8_t *data; size_t n, pos;
    int mode; int calls, fail_at;
    int64_t claimed_size;               /* wartosc zwracana przez size() */
    int has_size, has_seek, closed, seek_lies;
} Hostile;

static int64_t MAKTABASAUTI_CALL h_read(void *u, void *buf, size_t size) {
    Hostile *h = (Hostile *)u; size_t left = h->n - h->pos, give;
    h->calls++;
    if (h->mode == H_FAIL_AT && h->calls == h->fail_at) return MAKTABASAUTI_E_IO;
    if (h->mode == H_NEG_HUGE) return INT64_MIN;
    if (h->mode == H_OVERRUN_HUGE) return INT64_MAX;
    give = size < left ? size : left;
    if (h->mode == H_ONE_BYTE && give > 1) give = 1;
    if (h->mode == H_EOF_EARLY && h->calls > 3) return 0;
    if (give) memcpy(buf, h->data + h->pos, give);
    h->pos += give;
    if (h->mode == H_OVERRUN_BY_1 && h->calls == 1) return (int64_t)size + 1;   /* kłamie: oddaje wiecej, niz zazadano (naruszenie kontraktu) */
    return (int64_t)give;
}
static int64_t MAKTABASAUTI_CALL h_seek(void *u, int64_t off, int wh) {
    Hostile *h = (Hostile *)u; int64_t base = wh == 0 ? 0 : wh == 1 ? (int64_t)h->pos : (int64_t)h->n;
    if (wh < 0 || wh > 2) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if (off > 0 && base > INT64_MAX - off) return MAKTABASAUTI_E_OUT_OF_RANGE;
    if (base + off < 0 || (uint64_t)(base + off) > (uint64_t)h->n) return MAKTABASAUTI_E_OUT_OF_RANGE;
    h->pos = (size_t)(base + off);
    return h->seek_lies ? (int64_t)h->pos + 12345 : (int64_t)h->pos;    /* seek_lies: zwraca inna pozycje niz ustawil */
}
static int64_t MAKTABASAUTI_CALL h_size(void *u) { return ((Hostile *)u)->claimed_size; }
static void MAKTABASAUTI_CALL h_close(void *u) { ((Hostile *)u)->closed++; }

static maktabasauti_io_t hostile_io(Hostile *h) {
    maktabasauti_io_t io; memset(&io, 0, sizeof io);
    io.struct_size = sizeof io; io.user = h; io.read = h_read; io.close = h_close;
    io.seek = h->has_seek ? h_seek : NULL; io.size = h->has_size ? h_size : NULL;
    return io;
}

static maktabasauti_result_t open_hostile(Hostile *h, Track *t, maktabasauti_decoder_t **out, const maktabasauti_registry_t *reg) {
    maktabasauti_allocator_t a = make_alloc(t); maktabasauti_open_params_t p;
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION; p.io = hostile_io(h); p.allocator = &a; p.registry = reg;
    return maktabasauti_decoder_open(&p, out);
}

/* maly poprawny WAV do testow */
static size_t make_wav(uint8_t *b, size_t cap) {
    size_t n = 0, i; const unsigned frames = 3000;
    #define PUT8(v) do { if (n < cap) b[n] = (uint8_t)(v); n++; } while (0)
    #define PUT16(v) do { PUT8((v) & 255); PUT8(((v) >> 8) & 255); } while (0)
    #define PUT32(v) do { PUT16((v) & 0xffff); PUT16(((v) >> 16) & 0xffff); } while (0)
    #define PUTS(s) do { size_t k_; for (k_ = 0; (s)[k_]; k_++) PUT8((s)[k_]); } while (0)
    PUTS("RIFF"); PUT32(36 + frames * 2); PUTS("WAVE"); PUTS("fmt "); PUT32(16); PUT16(1); PUT16(1); PUT32(8000); PUT32(16000); PUT16(2); PUT16(16); PUTS("data"); PUT32(frames * 2);
    for (i = 0; i < frames; i++) PUT16((unsigned)((i * 37u) & 0xffffu));
    return n;
}

static uint64_t fnv(uint64_t h, const void *p, size_t n) { const uint8_t *b = (const uint8_t *)p; size_t i; for (i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } return h; }
static uint64_t decode_all(maktabasauti_decoder_t *d, uint64_t *frames) {
    static unsigned char buf[4096 * 4 * 8]; maktabasauti_stream_info_t info; uint64_t h = 1469598103934665603ull; size_t got; *frames = 0;
    info.struct_size = sizeof info;
    if (maktabasauti_decoder_get_info(d, &info) != MAKTABASAUTI_OK) return 0;
    for (;;) {
        maktabasauti_result_t r = maktabasauti_decoder_read(d, buf, 4096, MAKTABASAUTI_FORMAT_S16, &got);
        if (r != MAKTABASAUTI_OK) break;
        h = fnv(h, buf, got * info.channels * 2); *frames += got;
    }
    return h;
}

static void test_contract_violations(const uint8_t *data, size_t n) {
    static const struct { int mode; const char *name; } modes[] = { { H_OVERRUN_BY_1, "read returns size+1" }, { H_OVERRUN_HUGE, "read returns INT64_MAX" }, { H_NEG_HUGE, "read returns INT64_MIN" } };
    size_t m; int has_size, has_seek;
    for (m = 0; m < sizeof modes / sizeof *modes; m++)
        for (has_size = 0; has_size < 2; has_size++)
            for (has_seek = 0; has_seek < 2; has_seek++) {
                Hostile h; Track t; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r;
                memset(&h, 0, sizeof h); memset(&t, 0, sizeof t);
                h.data = data; h.n = n; h.mode = modes[m].mode; h.has_size = has_size; h.has_seek = has_seek; h.claimed_size = (int64_t)n;
                r = open_hostile(&h, &t, &d, NULL);
                CHECK(r != MAKTABASAUTI_OK, "%s (size=%d seek=%d): open must fail, got OK", modes[m].name, has_size, has_seek);
                if (r == MAKTABASAUTI_OK && d) maktabasauti_decoder_close(d);
                else { CHECK(h.closed == 0, "%s: io.close called after a failed open (ownership)", modes[m].name); CHECK(!d, "decoder returned with an error"); }
                CHECK(t.live == 0 && t.bad == 0, "%s: allocation imbalance live=%ld bad=%ld", modes[m].name, t.live, t.bad);
            }
}

static void test_io_errors_everywhere(const uint8_t *data, size_t n) {
    int k;
    for (k = 1; k <= 12; k++) {
        Hostile h; Track t; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r;
        memset(&h, 0, sizeof h); memset(&t, 0, sizeof t);
        h.data = data; h.n = n; h.mode = H_FAIL_AT; h.fail_at = k; h.has_size = 1; h.has_seek = 1; h.claimed_size = (int64_t)n;
        r = open_hostile(&h, &t, &d, NULL);
        if (r == MAKTABASAUTI_OK) { uint64_t fr; (void)decode_all(d, &fr); maktabasauti_decoder_close(d); CHECK(h.closed == 1, "close must close the input exactly once (k=%d, closed=%d)", k, h.closed); }
        else CHECK(h.closed == 0, "io.close called after failed open (k=%d)", k);
        CHECK(t.live == 0 && t.bad == 0, "fail_at=%d: allocation imbalance live=%ld bad=%ld", k, t.live, t.bad);
    }
    {   /* wejscie konczy sie nagle po kilku odczytach */
        Hostile h; Track t; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r;
        memset(&h, 0, sizeof h); memset(&t, 0, sizeof t);
        h.data = data; h.n = n; h.mode = H_EOF_EARLY; h.has_size = 1; h.claimed_size = (int64_t)n;
        r = open_hostile(&h, &t, &d, NULL);
        if (r == MAKTABASAUTI_OK) maktabasauti_decoder_close(d);
        CHECK(t.live == 0 && t.bad == 0, "early EOF: allocation imbalance");
    }
}

static void test_one_byte_reads(const uint8_t *data, size_t n, const char *what) {
    Hostile h; Track t; maktabasauti_decoder_t *d = NULL, *ref = NULL; maktabasauti_io_t mio; maktabasauti_open_params_t p; uint64_t fa = 0, fb = 0, ha, hb; maktabasauti_result_t r;
    memset(&h, 0, sizeof h); memset(&t, 0, sizeof t);
    h.data = data; h.n = n; h.mode = H_ONE_BYTE; h.has_size = 1; h.has_seek = 1; h.claimed_size = (int64_t)n;
    r = open_hostile(&h, &t, &d, NULL);
    CHECK(r == MAKTABASAUTI_OK, "%s: open with 1-byte reads failed (%d)", what, (int)r);
    if (r != MAKTABASAUTI_OK) return;
    ha = decode_all(d, &fa);
    maktabasauti_decoder_close(d);
    CHECK(t.live == 0 && t.bad == 0, "%s: allocation imbalance", what);
    if (maktabasauti_io_from_memory(data, n, NULL, &mio) != MAKTABASAUTI_OK) { CHECK(0, "memory io"); return; }
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION; p.io = mio;
    CHECK(maktabasauti_decoder_open(&p, &ref) == MAKTABASAUTI_OK, "%s: reference open failed", what);
    if (ref) { hb = decode_all(ref, &fb); maktabasauti_decoder_close(ref); CHECK(ha == hb && fa == fb, "%s: PCM differs between 1-byte reads and whole reads", what); }
    else mio.close(mio.user);
}

static void test_claimed_sizes(const uint8_t *data, size_t n) {
    static const int64_t sizes[] = { -1, -5, 0, 1, 4096, INT64_MAX, (int64_t)1 << 40, (int64_t)3 << 30, (int64_t)1 << 30, ((int64_t)1 << 30) + 1 };
    size_t i;
    for (i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        Hostile h; Track t; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r; uint64_t fr;
        memset(&h, 0, sizeof h); memset(&t, 0, sizeof t);
        t.limit = (size_t)1 << 31;                                   /* alokator odmawia zadan > 2 GiB; nic nie powinno ich nawet probowac */
        h.data = data; h.n = n; h.has_size = 1; h.has_seek = 1; h.claimed_size = sizes[i];
        r = open_hostile(&h, &t, &d, NULL);
        if (r == MAKTABASAUTI_OK) { (void)decode_all(d, &fr); maktabasauti_decoder_close(d); }
        CHECK(t.live == 0 && t.bad == 0, "claimed size %lld: allocation imbalance live=%ld", (long long)sizes[i], t.live);
        /* nie wolno zajac pamieci proporcjonalnej do deklarowanego rozmiaru, gdy dane go nie pokrywaja: dane maja kilka KiB */
        CHECK(t.max_request <= ((size_t)64 << 20) + 4096, "claimed size %lld for a %zu-byte input: single allocation request of %zu bytes", (long long)sizes[i], n, t.max_request);
        if (sizes[i] < 0 || sizes[i] == 0 || sizes[i] == 4096 || sizes[i] == 1) CHECK(r == MAKTABASAUTI_OK || r == MAKTABASAUTI_E_UNSUPPORTED_FORMAT || r == MAKTABASAUTI_E_NO_MEMORY, "claimed %lld: result %d", (long long)sizes[i], (int)r);
    }
}

static void test_seek_lies(const uint8_t *data, size_t n) {
    Hostile h; Track t; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r; uint64_t fr;
    memset(&h, 0, sizeof h); memset(&t, 0, sizeof t);
    h.data = data; h.n = n; h.has_size = 1; h.has_seek = 1; h.seek_lies = 1; h.claimed_size = (int64_t)n;
    r = open_hostile(&h, &t, &d, NULL);
    if (r == MAKTABASAUTI_OK) { (void)decode_all(d, &fr); (void)maktabasauti_decoder_seek(d, 10, MAKTABASAUTI_SEEK_EXACT, NULL); (void)decode_all(d, &fr); maktabasauti_decoder_close(d); }
    CHECK(t.live == 0 && t.bad == 0, "seek that reports a different position: allocation imbalance");
}

/* ---- modul, ktory wola uslugi hosta ze skrajnymi argumentami ---- */
static int g_hostcalls;
static int32_t MAKTABASAUTI_CALL x_probe(const maktabasauti_codec_t *s, const uint8_t *p, size_t n, const char *h) { (void)s; (void)p; (void)n; (void)h; return 100; }
static maktabasauti_result_t MAKTABASAUTI_CALL x_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *host, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    static const int64_t offs[] = { 0, 1, -1, INT64_MAX, INT64_MIN, INT64_MAX - 1, INT64_MIN + 1, 4096, (int64_t)1 << 40 };
    static const size_t sizes[] = { 0, 1, 4096, 16384, 16385, 65536, (size_t)-1, (size_t)-1 / 2 };
    unsigned char buf[16384]; size_t i, k; int w;
    (void)self; (void)info; *state = NULL;
    for (w = -1; w <= 3; w++) for (i = 0; i < sizeof offs / sizeof *offs; i++) { (void)host->seek(host->user, rd, offs[i], w); g_hostcalls++; }
    (void)host->seek(host->user, rd, 0, 0);
    for (k = 0; k < sizeof sizes / sizeof *sizes; k++) {
        size_t n = sizes[k] > sizeof buf ? sizeof buf : sizes[k];                      /* wlasny bufor wywolujacego ma 16 KiB: dluzsze zadania nie sa mozliwe do spelnienia */
        (void)host->peek(host->user, rd, buf, sizes[k] <= 16384 ? sizes[k] : 16385);     /* peek wiekszy niz bufor czytnika: kod bledu, bez zapisu */
        (void)host->read(host->user, rd, buf, n); g_hostcalls++;
    }
    (void)host->tell(host->user, rd); (void)host->size(host->user, rd);
    (void)host->peek(host->user, NULL, buf, 4);                                       /* zly uchwyt czytnika */
    (void)host->seek(host->user, NULL, 0, 0);
    return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
}
static maktabasauti_result_t MAKTABASAUTI_CALL x_read(void *s, void *d, size_t n, size_t *f) { (void)s; (void)d; (void)n; *f = 0; return MAKTABASAUTI_EOF; }
static void MAKTABASAUTI_CALL x_close(void *s) { (void)s; }
static const maktabasauti_codec_t x_codec = { sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "hostx", "hostile host calls", "test", MAKTABASAUTI_CODEC_FLAG_STREAMING, 0u, NULL,
                                              x_probe, x_open, x_read, NULL, NULL, NULL, NULL, NULL, NULL, x_close, { NULL }, { NULL } };

static void test_host_extremes(const uint8_t *data, size_t n) {
    int mode;
    for (mode = 0; mode < 3; mode++) {                                   /* 0: z pamieci (mem), 1: wejscie bez seek/size, 2: wejscie z klamiacym seek */
        Hostile h; Track t; maktabasauti_decoder_t *d = NULL; maktabasauti_registry_t *reg = NULL;
        memset(&h, 0, sizeof h); memset(&t, 0, sizeof t);
        h.data = data; h.n = n; h.has_size = mode != 1; h.has_seek = mode != 1; h.claimed_size = (int64_t)n; h.seek_lies = mode == 2;
        CHECK(maktabasauti_registry_create(NULL, &reg) == MAKTABASAUTI_OK, "registry");
        CHECK(maktabasauti_registry_add(reg, &x_codec) == MAKTABASAUTI_OK, "registry add");
        (void)open_hostile(&h, &t, &d, reg);
        CHECK(!d, "hostile module must not produce a decoder");
        CHECK(t.live == 0 && t.bad == 0, "host extremes mode %d: allocation imbalance", mode);
        CHECK(h.closed == 0, "io.close called after failed open (mode %d)", mode);
        maktabasauti_registry_destroy(reg);
    }
    CHECK(g_hostcalls > 0, "hostile module was not called");
}


/* ---- sondy modulow o skrajnych wynikach: obciecie do 1..100, kolejnosc przy remisie, przejscie do kolejnego kandydata po bledzie open, kody bledow ---- */
typedef struct { int32_t score; } ScoreCfg;
static char g_open_order[16]; static int g_open_count;
static int32_t MAKTABASAUTI_CALL s_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *h) { (void)p; (void)n; (void)h; return ((const ScoreCfg *)self->user_data)->score; }
static maktabasauti_result_t MAKTABASAUTI_CALL s_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *host, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    (void)host; (void)rd; (void)info; *state = NULL;
    if (g_open_count < 15) g_open_order[g_open_count++] = self->name[0];
    return MAKTABASAUTI_E_CORRUPT_DATA;
}
#define SCORE_CODEC(var, nm, sc) static const ScoreCfg var##_cfg = { sc }; \
    static const maktabasauti_codec_t var = { sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, nm, "score test", "test", MAKTABASAUTI_CODEC_FLAG_STREAMING, 0u, &var##_cfg, \
                                              s_probe, s_open, x_read, NULL, NULL, NULL, NULL, NULL, NULL, x_close, { NULL }, { NULL } }
SCORE_CODEC(sc_a, "a", 1000);
SCORE_CODEC(sc_b, "b", 100);
SCORE_CODEC(sc_c, "c", INT32_MIN);
SCORE_CODEC(sc_d, "d", -1);
SCORE_CODEC(sc_e, "e", 100);
SCORE_CODEC(sc_f, "f", INT32_MAX);
SCORE_CODEC(sc_g, "g", 7);

static void test_probe_scores(const uint8_t *data, size_t n) {
    Hostile h; Track t; maktabasauti_decoder_t *d = NULL; maktabasauti_registry_t *reg = NULL; maktabasauti_result_t r;
    memset(&h, 0, sizeof h); memset(&t, 0, sizeof t);
    h.data = data; h.n = n; h.has_size = 1; h.has_seek = 1; h.claimed_size = (int64_t)n;
    CHECK(maktabasauti_registry_create(NULL, &reg) == MAKTABASAUTI_OK, "registry");
    CHECK(maktabasauti_registry_add(reg, &sc_a) == MAKTABASAUTI_OK && maktabasauti_registry_add(reg, &sc_b) == MAKTABASAUTI_OK && maktabasauti_registry_add(reg, &sc_c) == MAKTABASAUTI_OK &&
          maktabasauti_registry_add(reg, &sc_d) == MAKTABASAUTI_OK && maktabasauti_registry_add(reg, &sc_e) == MAKTABASAUTI_OK && maktabasauti_registry_add(reg, &sc_f) == MAKTABASAUTI_OK &&
          maktabasauti_registry_add(reg, &sc_g) == MAKTABASAUTI_OK, "registry add");
    g_open_count = 0; memset(g_open_order, 0, sizeof g_open_order);
    r = open_hostile(&h, &t, &d, reg);
    CHECK(r == MAKTABASAUTI_E_CORRUPT_DATA, "all candidates fail with CORRUPT_DATA: expected that code, got %d", (int)r);
    /* wyniki obciete do 100: a(1000), b, e, f(INT32_MAX) rowne 100 -> kolejnosc rejestru a,b,e,f; potem g(7); c i d (<=0) nigdy */
    CHECK(strcmp(g_open_order, "abefg") == 0, "candidate order: expected abefg, got %s", g_open_order);
    CHECK(h.closed == 0 && !d, "failed open must not close the input or return a decoder");
    CHECK(t.live == 0 && t.bad == 0, "allocation imbalance");
    maktabasauti_registry_destroy(reg);
}

int main(int argc, char **argv) {
    uint8_t wav[8192]; size_t wn = make_wav(wav, sizeof wav);
    CHECK(wn > 44 && wn <= sizeof wav, "wav builder");
    test_contract_violations(wav, wn);
    test_io_errors_everywhere(wav, wn);
    test_one_byte_reads(wav, wn, "synthetic WAV");
    test_claimed_sizes(wav, wn);
    test_seek_lies(wav, wn);
    test_host_extremes(wav, wn);
    test_probe_scores(wav, wn);
    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb"); long sz; uint8_t *b;
        if (!f) { perror(argv[1]); return 2; }
        fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
        b = (uint8_t *)malloc((size_t)sz);
        if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) return 2;
        fclose(f);
        test_one_byte_reads(b, (size_t)sz, argv[1]);
        test_contract_violations(b, (size_t)sz);
        test_claimed_sizes(b, (size_t)sz);
        free(b);
    }
    printf("hostile io: %s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}
