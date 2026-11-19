/* MaktabaSauti: niezaleznosc instancji.  Wiele watkow jednoczesnie otwiera dekodery (nowe API) na tych samych, tylko do odczytu plikach, czyta je w roznych formatach
 * probek, przewija i resetuje; wynik kazdego musi byc bit w bit rowny wynikowi z jednego watku, a licznik alokacji kazdego watku (wlasny alokator) musi wrocic do zera.
 * Wspolne sa tylko rejestr domyslny i wlasny rejestr (oba niezmienne po utworzeniu).  Uruchamiany pod ThreadSanitizerem (CI: wymagany) i ASan/UBSan.
 * Dodatkowo (audyt sond): ROWNOLEGLE ROZPOZNAWANIE roznych formatow: syntetyczne pliki wszystkich formatow, obce dane, puste i krotkie bufory oraz uszkodzone warianty
 * (obciete, z odwroconymi bitami, ze skrajnymi wartosciami naglowka) sa otwierane z wielu watkow (rejestr domyslny i wlasny); wynik kazdego (kod, parametry strumienia,
 * skrot pierwszych porcji PCM) musi byc rowny wynikowi z jednego watku, a licznik alokacji wrocic do zera.
 * usage: test_maktabasauti_threads THREADS ROUNDS FILE... [-- CORPUS_FILE...]   (FILE: musza sie dekodowac; CORPUS_FILE: tylko do rozpoznawania, np. Opus/ALAC bez dekodera) */
#include "maktabasauti/maktabasauti.h"
#include "mks_probe_corpus.h"
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint8_t *bytes; size_t n; uint64_t hash[3], seek_hash; uint64_t frames; } File;
typedef struct { long live, bad; } Counter;
static void *MAKTABASAUTI_CALL c_alloc(void *u, size_t n) { Counter *c = (Counter *)u; void *p = malloc(n); if (p) c->live++; return p; }
static void MAKTABASAUTI_CALL c_free(void *u, void *p) { Counter *c = (Counter *)u; if (!p) { c->bad++; return; } c->live--; free(p); }

static uint64_t fnv(uint64_t h, const void *p, size_t n) { const uint8_t *b = (const uint8_t *)p; size_t i; for (i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } return h; }
static const maktabasauti_sample_format_t formats[3] = { MAKTABASAUTI_FORMAT_F32, MAKTABASAUTI_FORMAT_S16, MAKTABASAUTI_FORMAT_S24 };

static int decode(const File *f, const maktabasauti_registry_t *reg, Counter *cnt, uint64_t hash[3], uint64_t *seek_hash, uint64_t *frames) {
    size_t k;
    for (k = 0; k < 3; k++) {
        maktabasauti_allocator_t a; maktabasauti_open_params_t p; maktabasauti_io_t io; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info;
        static __thread unsigned char buf[2048 * 8 * 8]; uint64_t h = 1469598103934665603ull, total = 0; size_t got, bytes_per;
        memset(&a, 0, sizeof a); a.struct_size = sizeof a; a.user = cnt; a.alloc = c_alloc; a.realloc = NULL; a.free = c_free;
        if (maktabasauti_io_from_memory(f->bytes, f->n, &a, &io) != MAKTABASAUTI_OK) return 0;
        memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION; p.io = io; p.allocator = &a; p.registry = reg;
        if (maktabasauti_decoder_open(&p, &d) != MAKTABASAUTI_OK) { io.close(io.user); return 0; }
        info.struct_size = sizeof info;
        if (maktabasauti_decoder_get_info(d, &info) != MAKTABASAUTI_OK) return 0;
        bytes_per = maktabasauti_format_bytes(formats[k]) * info.channels;
        for (;;) {
            maktabasauti_result_t r = maktabasauti_decoder_read(d, buf, 2048, formats[k], &got);
            if (r == MAKTABASAUTI_EOF) break;
            if (r != MAKTABASAUTI_OK) return 0;
            h = fnv(h, buf, got * bytes_per); total += got;
        }
        hash[k] = h; *frames = total;
        if (k == 0) {                                      /* przewijanie i reset w srodku */
            uint64_t sh = 1469598103934665603ull; int i;
            if (maktabasauti_decoder_seek(d, info.total_frames / 2u, MAKTABASAUTI_SEEK_EXACT, NULL) != MAKTABASAUTI_OK) return 0;
            for (i = 0; i < 4 && maktabasauti_decoder_read(d, buf, 2048, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK; i++) sh = fnv(sh, buf, got * bytes_per);
            if (maktabasauti_decoder_reset(d) != MAKTABASAUTI_OK) return 0;
            if (maktabasauti_decoder_read(d, buf, 2048, MAKTABASAUTI_FORMAT_F32, &got) != MAKTABASAUTI_OK) return 0;
            sh = fnv(sh, buf, got * bytes_per);
            *seek_hash = sh;
        }
        maktabasauti_decoder_close(d);                     /* zamyka tez wejscie z pamieci */
    }
    return 1;
}


/* ---- rownolegle rozpoznawanie: korpus + warianty ---- */
typedef struct { maktabasauti_result_t r; uint32_t rate, channels; char codec[32]; uint64_t hash; } Rec;
typedef struct { const uint8_t *bytes; size_t n; const char *name; Rec ref; } Variant;

static Rec recognize(const uint8_t *b, size_t n, const maktabasauti_registry_t *reg, Counter *cnt) {
    Rec rec; maktabasauti_allocator_t a; maktabasauti_open_params_t p; maktabasauti_io_t io; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info;
    static __thread unsigned char buf[1024 * 8 * 8]; size_t got, i;
    memset(&rec, 0, sizeof rec); rec.hash = 1469598103934665603ull;
    memset(&a, 0, sizeof a); a.struct_size = sizeof a; a.user = cnt; a.alloc = c_alloc; a.realloc = NULL; a.free = c_free;
    rec.r = maktabasauti_io_from_memory(b, n, &a, &io);
    if (rec.r != MAKTABASAUTI_OK) return rec;
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION; p.io = io; p.allocator = &a; p.registry = reg;
    rec.r = maktabasauti_decoder_open(&p, &d);
    if (rec.r != MAKTABASAUTI_OK) { io.close(io.user); return rec; }
    info.struct_size = sizeof info;
    if (maktabasauti_decoder_get_info(d, &info) == MAKTABASAUTI_OK) {
        rec.rate = info.sample_rate; rec.channels = info.channels;
        for (i = 0; i < sizeof rec.codec - 1 && info.codec_name[i]; i++) rec.codec[i] = info.codec_name[i];
        for (i = 0; i < 4; i++) {
            if (maktabasauti_decoder_read(d, buf, 1024, MAKTABASAUTI_FORMAT_S16, &got) != MAKTABASAUTI_OK) break;
            rec.hash = fnv(rec.hash, buf, got * info.channels * 2);
        }
    }
    maktabasauti_decoder_close(d);
    return rec;
}
static int rec_equal(const Rec *x, const Rec *y) { return x->r == y->r && x->rate == y->rate && x->channels == y->channels && x->hash == y->hash && !strcmp(x->codec, y->codec); }

static uint64_t vrng = 0x9E3779B97F4A7C15ull;
static uint64_t vrnd(void) { vrng ^= vrng >> 12; vrng ^= vrng << 25; vrng ^= vrng >> 27; return vrng * 2685821657736338717ull; }
static void variants_build(const corpus_t *c, Variant **out, size_t *count) {
    size_t i, k, total = 0, cap = c->count * 8;
    Variant *v = (Variant *)calloc(cap, sizeof *v);
    if (!v) abort();
    for (i = 0; i < c->count; i++) {
        const corpus_item_t *it = &c->items[i];
        for (k = 0; k < 8; k++) {
            size_t n = it->n; uint8_t *b = (uint8_t *)malloc(n ? n : 1);
            if (!b) abort();
            memcpy(b, it->bytes, n);
            switch (k) {
            case 0: break;                                                      /* bez zmian */
            case 1: n = n / 2; break;                                           /* obciety w polowie */
            case 2: n = n < 3 ? n : 3; break;                                   /* tylko 3 bajty */
            case 3: if (n) b[vrnd() % (n < 64 ? n : 64)] ^= (uint8_t)(1u << (vrnd() % 8)); break;
            case 4: if (n >= 8) { size_t at = 4 + vrnd() % (n < 64 ? n - 4 : 60); if (at + 4 <= n) memset(b + at, 0xFF, 4); } break;
            case 5: if (n >= 8) { size_t at = vrnd() % (n < 48 ? n - 3 : 45); memset(b + at, 0x7F, 3); b[at] = 0x80; } break;
            case 6: n = n ? n - 1 : 0; break;                                   /* bez ostatniego bajtu */
            default: { size_t j; for (j = 16; j < n; j++) b[j] = (uint8_t)vrnd(); } break;   /* naglowek + losowy ogon */
            }
            v[total].bytes = b; v[total].n = n; v[total].name = it->name; total++;
        }
    }
    *out = v; *count = total;
}
typedef struct { const Variant *v; size_t count; int rounds, id; const maktabasauti_registry_t *reg; int failures; } RecJob;
static void *rec_worker(void *arg) {
    RecJob *j = (RecJob *)arg; int r; size_t k;
    for (r = 0; r < j->rounds; r++)
        for (k = 0; k < j->count; k++) {
            const Variant *x = &j->v[(k * 7 + (size_t)j->id * 13 + (size_t)r) % j->count];
            Counter cnt; Rec rec; memset(&cnt, 0, sizeof cnt);
            rec = recognize(x->bytes, x->n, j->reg, &cnt);
            if (!rec_equal(&rec, &x->ref) || cnt.live != 0 || cnt.bad != 0) { fprintf(stderr, "recognition thread %d round %d (%s, %zu bytes): MISMATCH or leak (r=%d vs %d live=%ld)\n", j->id, r, x->name, x->n, (int)rec.r, (int)x->ref.r, cnt.live); j->failures++; }
        }
    return NULL;
}

typedef struct { const File *files; int nfiles, rounds, id; const maktabasauti_registry_t *reg; int failures; } Job;
static void *worker(void *arg) {
    Job *j = (Job *)arg; int r, k;
    for (r = 0; r < j->rounds; r++)
        for (k = 0; k < j->nfiles; k++) {
            const File *f = &j->files[(k + j->id + r) % j->nfiles]; Counter cnt; uint64_t h[3] = { 0 }, sh = 0, fr = 0;
            memset(&cnt, 0, sizeof cnt);
            if (!decode(f, j->reg, &cnt, h, &sh, &fr) || h[0] != f->hash[0] || h[1] != f->hash[1] || h[2] != f->hash[2] || sh != f->seek_hash || fr != f->frames || cnt.live != 0 || cnt.bad != 0) {
                fprintf(stderr, "thread %d round %d: MISMATCH or leak (live=%ld bad=%ld)\n", j->id, r, cnt.live, cnt.bad);
                j->failures++;
            }
        }
    return NULL;
}

int main(int argc, char **argv) {
    int threads, rounds, nfiles, i, t, failures = 0, extra_from = argc; File *files; pthread_t *tid; Job *jobs;
    maktabasauti_registry_t *own = NULL;
    if (argc < 4) return 2;
    threads = atoi(argv[1]); rounds = atoi(argv[2]); nfiles = argc - 3;
    for (i = 3; i < argc; i++) if (!strcmp(argv[i], "--")) { nfiles = i - 3; extra_from = i + 1; break; }
    files = (File *)calloc((size_t)nfiles, sizeof *files);
    for (i = 0; i < nfiles; i++) {
        FILE *fp = fopen(argv[3 + i], "rb"); Counter cnt = { 0, 0 };
        if (!fp) { perror(argv[3 + i]); return 2; }
        fseek(fp, 0, SEEK_END); files[i].n = (size_t)ftell(fp); fseek(fp, 0, SEEK_SET);
        files[i].bytes = (uint8_t *)malloc(files[i].n);
        if (!files[i].bytes || fread(files[i].bytes, 1, files[i].n, fp) != files[i].n) return 2;
        fclose(fp);
        if (!decode(&files[i], NULL, &cnt, files[i].hash, &files[i].seek_hash, &files[i].frames)) { fprintf(stderr, "cannot decode %s\n", argv[3 + i]); return 2; }
    }
    /* wlasny rejestr wspoldzielony (tylko do odczytu) obok domyslnego */
    if (maktabasauti_registry_create(NULL, &own) != MAKTABASAUTI_OK || maktabasauti_registry_add_registry(own, maktabasauti_default_registry()) != MAKTABASAUTI_OK) return 2;
    tid = (pthread_t *)calloc((size_t)threads, sizeof *tid); jobs = (Job *)calloc((size_t)threads, sizeof *jobs);
    for (t = 0; t < threads; t++) { jobs[t].files = files; jobs[t].nfiles = nfiles; jobs[t].rounds = rounds; jobs[t].id = t; jobs[t].reg = (t & 1) ? own : NULL; pthread_create(&tid[t], NULL, worker, &jobs[t]); }
    for (t = 0; t < threads; t++) { pthread_join(tid[t], NULL); failures += jobs[t].failures; }
    printf("maktabasauti threads: %d threads x %d rounds x %d files x 3 formats, %d mismatches\n", threads, rounds, nfiles, failures);
    {   /* rownolegle rozpoznawanie roznych formatow i uszkodzonych wariantow */
        corpus_t corpus; Variant *vars = NULL; size_t nvars = 0, vi; RecJob *rj; int rfail = 0; unsigned long ok_opens = 0;
        memset(&corpus, 0, sizeof corpus);
        corpus_add_synthetic(&corpus);
        for (i = 0; i < nfiles; i++) corpus_add_file(&corpus, argv[3 + i], NULL);
        for (i = extra_from; i < argc; i++) if (!corpus_add_file(&corpus, argv[i], NULL)) { fprintf(stderr, "cannot read %s\n", argv[i]); return 2; }
        variants_build(&corpus, &vars, &nvars);
        for (vi = 0; vi < nvars; vi++) { Counter c0; memset(&c0, 0, sizeof c0); vars[vi].ref = recognize(vars[vi].bytes, vars[vi].n, NULL, &c0); if (c0.live != 0 || c0.bad != 0) { fprintf(stderr, "reference recognition leak (%s)\n", vars[vi].name); return 2; } ok_opens += vars[vi].ref.r == MAKTABASAUTI_OK; }
        rj = (RecJob *)calloc((size_t)threads, sizeof *rj);
        for (t = 0; t < threads; t++) { rj[t].v = vars; rj[t].count = nvars; rj[t].rounds = rounds; rj[t].id = t; rj[t].reg = (t & 1) ? own : NULL; pthread_create(&tid[t], NULL, rec_worker, &rj[t]); }
        for (t = 0; t < threads; t++) { pthread_join(tid[t], NULL); rfail += rj[t].failures; }
        printf("maktabasauti parallel recognition: %d threads x %d rounds x %zu variants (%lu open OK, rest rejected), %d mismatches\n", threads, rounds, nvars, ok_opens, rfail);
        failures += rfail;
        for (vi = 0; vi < nvars; vi++) free((void *)vars[vi].bytes);
        free(vars); free(rj); corpus_free(&corpus);
    }
    maktabasauti_registry_destroy(own);
    for (i = 0; i < nfiles; i++) free(files[i].bytes);
    free(files); free(tid); free(jobs);
    return failures ? 1 : 0;
}
