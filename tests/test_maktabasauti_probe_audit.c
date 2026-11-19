/* MaktabaSauti: audyt bezpieczenstwa sond (probe) i otwarcia kodekow na DOWOLNYCH danych.
 *
 * Kazda sonda rejestru domyslnego dostaje: wszystkie pliki korpusu (syntetyczne wszystkich formatow + obce dane + fixtury z tests/data), puste, jedno- i kilkubajtowe
 * bufory, ucięte naglowki (kazda dlugosc 0..96 i dlugosci graniczne), pojedyncze i czterobajtowe wartosci skrajne w kazdym offsecie naglowka (0x00/0xFF/0x7F/0x80...),
 * dane losowe, "etykieta + losowy ogon", losowe mutacje i obciecia.  KAZDE wywolanie dostaje bufor o DOKLADNYM rozmiarze:
 *   - bufor w osobnym mapowaniu ze stronami PROT_NONE tuz za koncem (odczyt poza koniec = SIGSEGV) albo tuz przed poczatkiem (odczyt przed poczatkiem),
 *   - bufor tylko do odczytu (zapis do wejscia = SIGSEGV; sonda nie moze modyfikowac danych),
 *   - przed kazdym wywolaniem stos jest zasmiecany roznymi wzorcami (wykrywa niezainicjowane zmienne, jak blad minimp3 z b2bdd74).
 * Dziala tak samo bez sanitizerow (strony ochronne) i z ASan/UBSan (dokladne granice).  Faza "open" otwiera te same dane kodekami (bezposrednio, bez probe), bo rdzen
 * wola open() kazdego kodeka, ktorego sonda przepuscila: czyta kilka porcji, przewija, resetuje; licznik alokacji musi wrocic do zera.
 * Zgloszenie bledu zawiera kodek, plik, tryb i dlugosc (handler SIGSEGV/SIGBUS/SIGFPE/SIGILL/SIGABRT).
 *
 * usage: test_maktabasauti_probe_audit [FILE...]   (srodowisko: MKS_AUDIT_ITERS=30000 MKS_AUDIT_OPEN_ITERS=1500 MKS_AUDIT_SEED=1)
 * Zwraca 0 tylko gdy nic sie nie zawiesilo, nie padlo i wszystkie oczekiwania co do rozpoznania sa spelnione. */
#define _GNU_SOURCE
#include "maktabasauti/maktabasauti.h"
#include "maktabasauti/maktabasauti_codec.h"
#include "mks_internal.h"
#include "mks_probe_corpus.h"
#include "sauti_decoder.h"
#include "sauti_decoder_opus.h"
#include "sauti_decoder_ffmpeg.h"

#include <signal.h>
#include <time.h>
#include <unistd.h>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#define MKS_GUARD 1
#else
#define MKS_GUARD 0
#endif

/* ---- kontekst bledu ---- */
static const char *g_codec = "-", *g_item = "-", *g_mode = "-";
static size_t g_len;
static unsigned long g_aux;
static void on_fault(int sig) {
    char msg[512];
    int n = snprintf(msg, sizeof msg, "\nFAULT signal %d: codec=%s item=%s mode=%s len=%zu aux=%lu\n", sig, g_codec, g_item, g_mode, g_len, g_aux);
    if (n > 0) { ssize_t w = write(2, msg, (size_t)n); (void)w; }
    _exit(3);
}
static void install_handlers(void) {
    static unsigned char altstack[65536];
    struct sigaction sa; stack_t ss;
    ss.ss_sp = altstack; ss.ss_size = sizeof altstack; ss.ss_flags = 0;
    sigaltstack(&ss, NULL);
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_fault; sa.sa_flags = SA_ONSTACK; sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL); sigaction(SIGFPE, &sa, NULL); sigaction(SIGILL, &sa, NULL); sigaction(SIGABRT, &sa, NULL);
}

static int g_failures;
#define FAIL(...) do { fprintf(stderr, "FAIL [%s | %s | %s | len=%zu]: ", g_codec, g_item, g_mode, g_len); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); g_failures++; } while (0)

/* ---- generator pseudolosowy ---- */
static uint64_t g_rng = 88172645463325252ull;
static uint64_t rnd(void) { g_rng ^= g_rng >> 12; g_rng ^= g_rng << 25; g_rng ^= g_rng >> 27; return g_rng * 2685821657736338717ull; }
static size_t rnd_below(size_t n) { return n ? (size_t)(rnd() % n) : 0; }

/* ---- bufor ze stronami ochronnymi ---- */
typedef struct { uint8_t *map; size_t map_size, page; uint8_t *fallback; } Guard;
static void guard_init(Guard *g, size_t max_n) {
#if MKS_GUARD
    g->page = (size_t)sysconf(_SC_PAGESIZE);
    g->map_size = (max_n + g->page - 1) / g->page * g->page + 2 * g->page;
    g->map = (uint8_t *)mmap(NULL, g->map_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (g->map == (uint8_t *)MAP_FAILED) { perror("mmap"); exit(2); }
#else
    g->fallback = (uint8_t *)malloc(max_n ? max_n : 1); (void)g;
#endif
}
/* kopiuje `src` (n bajtow) do bufora o dokladnym rozmiarze; tail=1: koniec dokladnie na granicy strony ochronnej, tail=0: poczatek tuz za strona ochronna */
static const uint8_t *guard_set(Guard *g, const uint8_t *src, size_t n, int tail) {
#if MKS_GUARD
    uint8_t *usable = g->map + g->page; size_t usable_size = g->map_size - 2 * g->page;
    uint8_t *start, *end, *first, *last;
    if (n > usable_size) { fprintf(stderr, "guard too small\n"); exit(2); }
    mprotect(usable, usable_size, PROT_NONE);
    if (tail) { end = usable + usable_size; start = end - n; }
    else { start = usable; end = start + n; }
    first = (uint8_t *)((uintptr_t)start & ~(uintptr_t)(g->page - 1));
    last = (uint8_t *)(((uintptr_t)end + g->page - 1) & ~(uintptr_t)(g->page - 1));
    if (last > first) {
        mprotect(first, (size_t)(last - first), PROT_READ | PROT_WRITE);
        if (n) memcpy(start, src, n);
        mprotect(first, (size_t)(last - first), PROT_READ);
    }
    return start;
#else
    (void)tail; if (n) memcpy(g->fallback, src, n); return g->fallback;
#endif
}

#if defined(__GNUC__)
__attribute__((noinline))
#endif
static void poison_stack(unsigned char v) { volatile unsigned char scratch[24576]; memset((void *)scratch, v, sizeof scratch); }

/* ---- rejestr ---- */
static const maktabasauti_codec_t *const *g_codecs; static size_t g_ncodecs;
/* sondy poza rejestrem domyslnym (stare API odtwarzacza): Opus, AAC/ALAC/WebM przez FFmpeg.  WavPack (jedna memcmp) przejrzany recznie: wymaga libwavpack. */
typedef struct { const char *name; const SautiDecoderOps *ops; } Extra;
static const Extra g_extras[] = { { "opus-opusfile", &sauti_decoder_opus_ops }, { "aac-libavcodec", &sauti_decoder_aac_ops }, { "alac-libavcodec", &sauti_decoder_alac_ops }, { "webm-libavcodec", &sauti_decoder_webm_ops } };
#define N_EXTRAS (sizeof g_extras / sizeof g_extras[0])
#define N_ENTRIES (g_ncodecs + N_EXTRAS)
static const char *entry_name(size_t ci) { return ci < g_ncodecs ? g_codecs[ci]->name : g_extras[ci - g_ncodecs].name; }
static unsigned long g_probe_calls, g_open_calls;
static unsigned long g_claims[64], g_open_ok[64];

static int32_t call_probe(size_t ci, const uint8_t *d, size_t n, unsigned variant) {
    static const unsigned char pat[] = { 0x80, 0xFF, 0x00, 0x7F, 0xA5, 0xC0 };
    int32_t s;
    poison_stack(pat[variant % sizeof pat]);
    g_codec = entry_name(ci);
    if (ci < g_ncodecs) s = g_codecs[ci]->probe(g_codecs[ci], d, n, (variant & 1u) ? "name.bin" : NULL);
    else s = g_extras[ci - g_ncodecs].ops->probe(d, n) ? 50 : 0;
    g_probe_calls++;
    if (s < 0 || s > 100) FAIL("probe returned %d (allowed 0..100)", (int)s);
    return s;
}

/* wszystkie sondy na jednym wejsciu, obie orientacje bufora */
static void probe_all(Guard *g, const char *item, const char *mode, const uint8_t *src, size_t n, unsigned variant) {
    size_t ci; int tail;
    g_item = item; g_mode = mode; g_len = n;
    for (tail = 0; tail < 2; tail++) {
        const uint8_t *d = guard_set(g, src, n, tail);
        for (ci = 0; ci < N_ENTRIES; ci++) (void)call_probe(ci, d, n, variant + (unsigned)ci);
    }
}

/* ---- faza 1: dokladny bufor, korpus x sondy x dlugosci ---- */
static void phase_cross_and_sizes(Guard *g, const corpus_t *c) {
    size_t i, ci, k;
    for (i = 0; i < c->count; i++) {
        const corpus_item_t *it = &c->items[i];
        const uint8_t *d = guard_set(g, it->bytes, it->n, 1);
        unsigned claimers = 0;
        g_item = it->name; g_mode = "cross";
        for (ci = 0; ci < N_ENTRIES; ci++) {
            int32_t s = call_probe(ci, d, it->n, (unsigned)ci);
            if (s > 0) { claimers++; g_claims[ci]++; }
            if (it->owner && !strcmp(it->owner, entry_name(ci)) && s <= 0) FAIL("owner codec does not recognise its own file");
            if (it->owner && ci < g_ncodecs && strcmp(it->owner, entry_name(ci)) && s > 0 && strcmp(entry_name(ci), "mp3-minimp3") != 0 &&
                !(strstr(it->owner, "-adts") && strstr(entry_name(ci), "-adts")))   /* rodzina ADTS (LC/HE) nakłada się z założenia: wygrywa kolejność rejestru; adaptery FFmpeg tez */
                FAIL("foreign codec %s claims a %s file", entry_name(ci), it->owner);
        }
        if (!claimers && strncmp(it->name, "foreign", 7) && strncmp(it->name, "one", 3) && strncmp(it->name, "few", 3)) FAIL("no codec recognises the file");
        for (k = 0; k <= 96 && k <= it->n; k++) probe_all(g, it->name, "prefix", it->bytes, k, (unsigned)k);
        {
            static const size_t deltas[] = { 1, 2, 3, 4, 7, 8, 9, 15, 16, 17, 31, 32, 33, 127, 128, 129, 226, 227, 228, 4095, 4096, 4097 };
            for (k = 0; k < sizeof deltas / sizeof *deltas; k++) {
                if (it->n > deltas[k]) probe_all(g, it->name, "cut-tail", it->bytes, it->n - deltas[k], (unsigned)k);
                probe_all(g, it->name, "pad-zero", it->bytes, it->n, (unsigned)k);   /* ten sam rozmiar przy innym ukladzie stosu */
            }
        }
        probe_all(g, it->name, "half", it->bytes, it->n / 2, 1);
    }
}

/* ---- faza 2: skrajne wartosci w kazdym offsecie naglowka ---- */
static void phase_extremes(Guard *g, const corpus_t *c) {
    static const uint8_t bytev[] = { 0x00, 0x01, 0x7F, 0x80, 0xFE, 0xFF };
    static const uint32_t wordv[] = { 0x00000000u, 0x00000001u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 0x01000000u, 0x00FFFFFFu, 0xFFFFFF7Fu, 0x0000FFFFu, 0xFFFF0000u };
    size_t i, off, k;
    uint8_t *w = (uint8_t *)malloc(1 << 17);
    if (!w) abort();
    for (i = 0; i < c->count; i++) {
        const corpus_item_t *it = &c->items[i];
        size_t lim = it->n < 128 ? it->n : 128;
        if (!it->n) continue;
        for (off = 0; off < lim; off++) {
            for (k = 0; k < sizeof bytev; k++) {
                memcpy(w, it->bytes, it->n); w[off] = bytev[k];
                probe_all(g, it->name, "byte", w, it->n, (unsigned)(off + k));
            }
            if (off + 4 <= it->n)
                for (k = 0; k < sizeof wordv / sizeof *wordv; k++) {
                    memcpy(w, it->bytes, it->n);
                    w[off] = (uint8_t)(wordv[k] >> 24); w[off + 1] = (uint8_t)(wordv[k] >> 16); w[off + 2] = (uint8_t)(wordv[k] >> 8); w[off + 3] = (uint8_t)wordv[k];
                    probe_all(g, it->name, "word-be", w, it->n, (unsigned)(off + k));
                    memcpy(w, it->bytes, it->n);
                    w[off + 3] = (uint8_t)(wordv[k] >> 24); w[off + 2] = (uint8_t)(wordv[k] >> 16); w[off + 1] = (uint8_t)(wordv[k] >> 8); w[off] = (uint8_t)wordv[k];
                    probe_all(g, it->name, "word-le", w, it->n, (unsigned)(off + k));
                }
        }
    }
    free(w);
}

/* ---- faza 3: losowe dane, etykieta + losowy ogon, losowe mutacje ---- */
static size_t mutate(const corpus_item_t *it, uint8_t *w, size_t cap) {
    size_t n = it->n, nmut, k;
    if (n > cap) n = cap;
    memcpy(w, it->bytes, n);
    nmut = 1 + rnd_below(6);
    for (k = 0; k < nmut && n; k++) {
        size_t at = rnd() & 1 ? rnd_below(n < 128 ? n : 128) : rnd_below(n);
        switch (rnd_below(6)) {
        case 0: w[at] = (uint8_t)rnd(); break;
        case 1: w[at] ^= (uint8_t)(1u << rnd_below(8)); break;
        case 2: w[at] = (uint8_t[]){ 0x00, 0xFF, 0x7F, 0x80 }[rnd_below(4)]; break;
        case 3: if (at + 4 <= n) { uint32_t v = (uint32_t[]){ 0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u, 0u, 0x01000000u }[rnd_below(5)]; memcpy(w + at, &v, 4); } break;
        case 4: n = at; break;                                     /* obciecie */
        default: if (at + 2 <= n) { w[at] = w[at + 1]; } break;
        }
    }
    return n;
}

static void phase_random(Guard *g, const corpus_t *c, unsigned long iters) {
    unsigned long it_no;
    uint8_t *w = (uint8_t *)malloc(1 << 17);
    if (!w) abort();
    for (it_no = 0; it_no < iters; it_no++) {
        size_t n, k;
        switch (it_no % 4) {
        case 0: {                                                  /* czysto losowe dane, dlugosc 0..5000 */
            n = rnd_below(5001); for (k = 0; k < n; k++) w[k] = (uint8_t)rnd();
            probe_all(g, "random", "random", w, n, (unsigned)it_no); break; }
        case 1: {                                                  /* etykieta z losowego pliku + losowy ogon */
            const corpus_item_t *it = &c->items[rnd_below(c->count)];
            size_t head = it->n < 64 ? it->n : rnd_below(64) + 1;
            if (head > it->n) head = it->n;
            n = head + rnd_below(2000);
            memcpy(w, it->bytes, head); for (k = head; k < n; k++) w[k] = (uint8_t)rnd();
            probe_all(g, it->name, "magic+random", w, n, (unsigned)it_no); break; }
        default: {
            const corpus_item_t *it = &c->items[rnd_below(c->count)];
            n = mutate(it, w, 1 << 17);
            probe_all(g, it->name, "mutated", w, n, (unsigned)it_no); break; }
        }
        if (g_failures > 50) break;
    }
    free(w);
}

/* ---- faza 4: open() kodekow bezposrednio (host z buforem ochronnym jako "cale wejscie") ---- */
typedef struct { const uint8_t *data; size_t n; long live, bad; } Fake;
static void *MAKTABASAUTI_CALL f_alloc(void *u, size_t n) { Fake *f = (Fake *)u; void *p = malloc(n ? n : 1); if (p) f->live++; return p; }
static void *MAKTABASAUTI_CALL f_realloc(void *u, void *p, size_t n) { (void)u; return realloc(p, n); }
static void MAKTABASAUTI_CALL f_free(void *u, void *p) { Fake *f = (Fake *)u; if (!p) { f->bad++; return; } f->live--; free(p); }
static maktabasauti_result_t MAKTABASAUTI_CALL f_map(void *u, maktabasauti_reader_t *r, const uint8_t **d, size_t *n) { Fake *f = (Fake *)u; (void)r; *d = f->data; *n = f->n; return MAKTABASAUTI_OK; }
static int64_t MAKTABASAUTI_CALL f_nope(void *u, maktabasauti_reader_t *r, void *b, size_t n) { (void)u; (void)r; (void)b; (void)n; return MAKTABASAUTI_E_IO; }
static const char *MAKTABASAUTI_CALL f_opt(void *u, const char *k) { (void)u; (void)k; return NULL; }
static void MAKTABASAUTI_CALL f_log(void *u, int l, const char *m) { (void)u; (void)l; (void)m; }

static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9; }

static void open_one(Guard *g, size_t ci, const char *item, const char *mode, const uint8_t *src, size_t n) {
    const maktabasauti_codec_t *c = g_codecs[ci];
    Fake fk; maktabasauti_host_t host; maktabasauti_stream_info_t info; void *state = NULL;
    const uint8_t *d = guard_set(g, src, n, (int)(g_open_calls & 1u));
    double t0 = now_s();
    memset(&fk, 0, sizeof fk); fk.data = d; fk.n = n;
    memset(&host, 0, sizeof host); host.struct_size = sizeof host; host.user = &fk;
    host.alloc = f_alloc; host.realloc = f_realloc; host.free = f_free; host.read = f_nope; host.peek = f_nope; host.map = f_map; host.option = f_opt; host.log = f_log;
    memset(&info, 0, sizeof info); info.struct_size = sizeof info;
    g_codec = c->name; g_item = item; g_mode = mode; g_len = n; g_open_calls++;
    poison_stack((unsigned char)(0x80u + (g_open_calls & 0x7fu)));
    if (c->open(c, &host, NULL, &info, &state) == MAKTABASAUTI_OK) {
        static float out[32 * 1024];
        size_t frames = 0, k; uint64_t actual = 0;
        g_open_ok[ci]++;
        if (!state) FAIL("open returned OK with a NULL state");
        else {
            if (info.channels == 0 || info.channels > 32 || info.sample_rate == 0) FAIL("open OK with invalid info (ch=%u rate=%u)", info.channels, info.sample_rate);
            else {
                size_t want = 1024; if (want * info.channels > sizeof out / sizeof *out) want = sizeof out / sizeof *out / info.channels;
                for (k = 0; k < 6; k++) { if (c->read(state, out, want, &frames) != MAKTABASAUTI_OK) break; if (frames > want) { FAIL("read returned %zu > %zu frames", frames, want); break; } }
                if (c->seek) { {   /* odleglosc ograniczona: przewijanie MIDI jest liniowe w liczbie ramek (znane ograniczenie, docs/AUDYT_SOND.md R2), a uszkodzony plik moze deklarowac godziny */
                    uint64_t target = info.total_frames != MAKTABASAUTI_UNKNOWN_LENGTH ? info.total_frames / 2 : 0;
                    if (target > 2000000u) target = 2000000u;
                    (void)c->seek(state, target, MAKTABASAUTI_SEEK_EXACT, &actual);
                } (void)c->read(state, out, want, &frames); }
                if (c->reset) { (void)c->reset(state); (void)c->read(state, out, want, &frames); }
            }
            c->close(state);
        }
    }
    if (fk.live != 0 || fk.bad != 0) FAIL("allocation imbalance after open/close: live=%ld bad=%ld", fk.live, fk.bad);
    if (now_s() - t0 > 20.0) FAIL("operation took %.1f s (possible hang or quadratic behaviour)", now_s() - t0);
}

static void phase_open(Guard *g, const corpus_t *c, unsigned long iters) {
    unsigned long it_no;
    size_t i, ci;
    uint8_t *w = (uint8_t *)malloc(1 << 17);
    if (!w) abort();
    for (i = 0; i < c->count; i++)                                 /* niezmodyfikowane pliki: kazdy kodek (rowniez obcy) */
        for (ci = 0; ci < g_ncodecs; ci++) open_one(g, ci, c->items[i].name, "open-clean", c->items[i].bytes, c->items[i].n);
    for (it_no = 0; it_no < iters; it_no++) {
        const corpus_item_t *it = &c->items[rnd_below(c->count)];
        size_t n = mutate(it, w, 1 << 17);
        if (it_no % 8 == 7) { for (ci = 0; ci < g_ncodecs; ci++) open_one(g, ci, it->name, "open-forced", w, n); }
        else {                                                    /* realny przebieg: kodeki, ktorych sonda przepuszcza dane */
            const uint8_t *d = guard_set(g, w, n, 1);
            int32_t score[64]; size_t cnt = 0;
            for (ci = 0; ci < g_ncodecs; ci++) { score[ci] = call_probe(ci, d, n, (unsigned)it_no); cnt += score[ci] > 0; }
            for (ci = 0; ci < g_ncodecs; ci++) if (score[ci] > 0) open_one(g, ci, it->name, "open-probed", w, n);
            (void)cnt;
        }
        if (g_failures > 50) break;
    }
    free(w);
}

static unsigned long env_ul(const char *name, unsigned long def) { const char *v = getenv(name); return v && *v ? strtoul(v, NULL, 10) : def; }

int main(int argc, char **argv) {
    corpus_t corpus; Guard guard; size_t i, maxn = 1 << 17; unsigned long iters = env_ul("MKS_AUDIT_ITERS", 30000), oiters = env_ul("MKS_AUDIT_OPEN_ITERS", 1500);
    double t0 = now_s();
    memset(&corpus, 0, sizeof corpus);
    g_rng ^= env_ul("MKS_AUDIT_SEED", 1) * 0x9E3779B97F4A7C15ull;
    install_handlers();
    g_codecs = mks_legacy_codecs(&g_ncodecs);
    if (!g_codecs || !g_ncodecs || g_ncodecs + N_EXTRAS > 64) { fprintf(stderr, "no codecs\n"); return 2; }
    corpus_add_synthetic(&corpus);
    for (i = 1; i < (size_t)argc; i++) if (!corpus_add_file(&corpus, argv[i], NULL)) { fprintf(stderr, "cannot read %s\n", argv[i]); return 2; }
    for (i = 0; i < corpus.count; i++) if (corpus.items[i].n + 64 > maxn) maxn = corpus.items[i].n + 64;
    guard_init(&guard, maxn);

    phase_cross_and_sizes(&guard, &corpus);
    printf("phase 1 (cross + prefixes + boundary sizes): %lu probe calls, %d failures\n", g_probe_calls, g_failures);
    phase_extremes(&guard, &corpus);
    printf("phase 2 (extreme byte/word values at every header offset): %lu probe calls, %d failures\n", g_probe_calls, g_failures);
    phase_random(&guard, &corpus, iters);
    printf("phase 3 (random / magic+random / mutated, %lu iterations): %lu probe calls, %d failures\n", iters, g_probe_calls, g_failures);
    phase_open(&guard, &corpus, oiters);
    printf("phase 4 (open+read+seek+reset+close, %lu mutated iterations): %lu open calls, %d failures\n", oiters, g_open_calls, g_failures);

    printf("claims per codec (clean corpus) / successful opens (all phases):\n");
    for (i = 0; i < N_ENTRIES; i++) printf("  %-30s claims=%-4lu opens_ok=%lu\n", entry_name(i), g_claims[i], g_open_ok[i]);
    printf("probe audit: %zu corpus items, %zu codecs, %.1f s, %d failures\n", corpus.count, g_ncodecs, now_s() - t0, g_failures);
    corpus_free(&corpus);
    return g_failures ? 1 : 0;
}
