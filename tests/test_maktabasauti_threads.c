/* MaktabaSauti: niezaleznosc instancji.  Wiele watkow jednoczesnie otwiera dekodery (nowe API) na tych samych, tylko do odczytu plikach, czyta je w roznych formatach
 * probek, przewija i resetuje; wynik kazdego musi byc bit w bit rowny wynikowi z jednego watku, a licznik alokacji kazdego watku (wlasny alokator) musi wrocic do zera.
 * Wspolne sa tylko rejestr domyslny i wlasny rejestr (oba niezmienne po utworzeniu).  Uruchamiany pod ThreadSanitizerem (CI: wymagany) i ASan/UBSan.
 * usage: test_maktabasauti_threads THREADS ROUNDS FILE... */
#include "maktabasauti/maktabasauti.h"
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
    int threads, rounds, nfiles, i, t, failures = 0; File *files; pthread_t *tid; Job *jobs;
    maktabasauti_registry_t *own = NULL;
    if (argc < 4) return 2;
    threads = atoi(argv[1]); rounds = atoi(argv[2]); nfiles = argc - 3;
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
    maktabasauti_registry_destroy(own);
    for (i = 0; i < nfiles; i++) free(files[i].bytes);
    free(files); free(tid); free(jobs);
    return failures ? 1 : 0;
}
