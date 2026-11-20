/* MaktabaSauti: PIERWSZE uzycie kazdego kodeka z wielu watkow naraz ("zimny start").
 *
 * Wyscigi danych w inicjalizacji leniwej (tablice CRC, wykrywanie mozliwosci procesora, tablice kodow) zdarzaja sie TYLKO podczas pierwszego otwarcia w procesie; test, ktory
 * najpierw dekoduje plik w jednym watku, juz ich nie widzi (tak przeoczylismy wyscig crc32_init w stb_vorbis i inicjalizacje CPU w dr_flac).  Dlatego:
 *   - wzorzec (kod wyniku, parametry strumienia, skrot PCM kazdego pliku korpusu) liczy osobny proces potomny z jednym watkiem i przekazuje go potokiem;
 *   - nastepnie kolejne procesy potomne (fork: swiezy stan, nic jeszcze nie zainicjowane) uruchamiaja N watkow, ktore startuja jednoczesnie na barierze i od razu otwieraja
 *     i dekoduja WSZYSTKIE pliki korpusu (kazdy watek w innej kolejnosci), kazdy wynik musi byc rowny wzorcowi;
 *   - proces nadrzedny nie dotyka zadnego kodeka, wiec kazde dziecko startuje "na zimno".
 * Pod ThreadSanitizerem (CI: wymagany) kazdy niezabezpieczony zapis do stanu globalnego jest zglaszany.  usage: test_maktabasauti_coldstart THREADS PROCESSES FILE... */
#include "maktabasauti/maktabasauti.h"
#include "mks_probe_corpus.h"
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct { int32_t r; uint32_t rate, channels; char codec[32]; uint64_t hash; } Rec;

static uint64_t fnv(uint64_t h, const void *p, size_t n) { const uint8_t *b = (const uint8_t *)p; size_t i; for (i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } return h; }

static Rec recognize(const uint8_t *b, size_t n) {
    Rec rec; maktabasauti_io_t io; maktabasauti_open_params_t p; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info;
    unsigned char buf[2048 * 8 * 4]; size_t got, i;
    memset(&rec, 0, sizeof rec); rec.hash = 1469598103934665603ull;
    rec.r = maktabasauti_io_from_memory(b, n, NULL, &io);
    if (rec.r != MAKTABASAUTI_OK) return rec;
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION; p.io = io;
    rec.r = maktabasauti_decoder_open(&p, &d);
    if (rec.r != MAKTABASAUTI_OK) { io.close(io.user); return rec; }
    info.struct_size = sizeof info;
    if (maktabasauti_decoder_get_info(d, &info) == MAKTABASAUTI_OK) {
        rec.rate = info.sample_rate; rec.channels = info.channels;
        for (i = 0; i < sizeof rec.codec - 1 && info.codec_name[i]; i++) rec.codec[i] = info.codec_name[i];
        for (i = 0; i < 8; i++) {                                    /* kilka porcji: dekodowanie, nie tylko otwarcie */
            if (maktabasauti_decoder_read(d, buf, 2048, MAKTABASAUTI_FORMAT_S16, &got) != MAKTABASAUTI_OK) break;
            rec.hash = fnv(rec.hash, buf, got * info.channels * 2);
        }
    }
    maktabasauti_decoder_close(d);
    return rec;
}
static int rec_equal(const Rec *a, const Rec *b) { return a->r == b->r && a->rate == b->rate && a->channels == b->channels && a->hash == b->hash && !strcmp(a->codec, b->codec); }

typedef struct { const corpus_t *c; Rec *out; int id, nthreads; pthread_mutex_t *mu; pthread_cond_t *cv; int *arrived; } Job;
static void *worker(void *arg) {
    Job *j = (Job *)arg; size_t k;
    pthread_mutex_lock(j->mu);                                       /* bariera: wszystkie watki startuja jednoczesnie */
    (*j->arrived)++;
    if (*j->arrived == j->nthreads) pthread_cond_broadcast(j->cv);
    else while (*j->arrived < j->nthreads) pthread_cond_wait(j->cv, j->mu);
    pthread_mutex_unlock(j->mu);
    for (k = 0; k < j->c->count; k++) {
        size_t idx = (k + (size_t)j->id * 5) % j->c->count;
        j->out[idx] = recognize(j->c->items[idx].bytes, j->c->items[idx].n);
    }
    return NULL;
}

/* uruchamia `nthreads` watkow, kazdy liczy wyniki calego korpusu; results[t] to tablica wynikow watku t */
static void run_threads(const corpus_t *c, int nthreads, Rec **results) {
    pthread_t tid[64]; Job job[64]; pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER; pthread_cond_t cv = PTHREAD_COND_INITIALIZER; int arrived = 0, t;
    for (t = 0; t < nthreads; t++) { job[t].c = c; job[t].out = results[t]; job[t].id = t; job[t].nthreads = nthreads; job[t].mu = &mu; job[t].cv = &cv; job[t].arrived = &arrived; pthread_create(&tid[t], NULL, worker, &job[t]); }
    for (t = 0; t < nthreads; t++) pthread_join(tid[t], NULL);
}

int main(int argc, char **argv) {
    corpus_t corpus; int nthreads, nproc, i, p, failures = 0; Rec *ref, *res[64]; int fd[2]; pid_t pid; size_t n, k; int opened = 0;
    if (argc < 3) { fprintf(stderr, "usage: %s THREADS PROCESSES FILE...\n", argv[0]); return 2; }
    nthreads = atoi(argv[1]); nproc = atoi(argv[2]);
    if (nthreads < 2 || nthreads > 64) { fprintf(stderr, "THREADS must be 2..64\n"); return 2; }
    memset(&corpus, 0, sizeof corpus);
    corpus_add_synthetic(&corpus);
    for (i = 3; i < argc; i++) if (!corpus_add_file(&corpus, argv[i], NULL)) { fprintf(stderr, "cannot read %s\n", argv[i]); return 2; }
    n = corpus.count;
    ref = (Rec *)calloc(n, sizeof *ref);
    if (!ref) return 2;

    /* wzorzec z procesu potomnego z jednym watkiem (zimny start bez wspolbieznosci) */
    if (pipe(fd) != 0) return 2;
    pid = fork();
    if (pid == 0) {
        Rec *r1[1]; int rc = 0; size_t off = 0; const uint8_t *src;
        r1[0] = ref; close(fd[0]);
        for (k = 0; k < n; k++) ref[k] = recognize(corpus.items[k].bytes, corpus.items[k].n);
        (void)r1; src = (const uint8_t *)ref;
        while (off < n * sizeof *ref) { ssize_t w = write(fd[1], src + off, n * sizeof *ref - off); if (w <= 0) { rc = 1; break; } off += (size_t)w; }
        _exit(rc);
    }
    close(fd[1]);
    { size_t off = 0; uint8_t *dst = (uint8_t *)ref; while (off < n * sizeof *ref) { ssize_t r = read(fd[0], dst + off, n * sizeof *ref - off); if (r <= 0) break; off += (size_t)r; } if (off != n * sizeof *ref) { fprintf(stderr, "reference child failed\n"); return 2; } }
    close(fd[0]);
    { int st = 0; waitpid(pid, &st, 0); if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) { fprintf(stderr, "reference child failed (status %d)\n", st); return 2; } }
    for (k = 0; k < n; k++) opened += ref[k].r == MAKTABASAUTI_OK;

    for (p = 0; p < nproc; p++) {
        pid = fork();
        if (pid == 0) {                                              /* swiezy proces: zaden kodek nie byl jeszcze uzyty */
            int bad = 0, t;
            for (t = 0; t < nthreads; t++) { res[t] = (Rec *)calloc(n, sizeof **res); if (!res[t]) _exit(2); }
            run_threads(&corpus, nthreads, res);
            for (t = 0; t < nthreads; t++) for (k = 0; k < n; k++) if (!rec_equal(&res[t][k], &ref[k])) { fprintf(stderr, "process %d thread %d item %s: result differs from the single-thread reference\n", p, t, corpus.items[k].name); bad++; }
            _exit(bad ? 1 : 0);
        }
        { int st = 0; waitpid(pid, &st, 0); if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) { fprintf(stderr, "cold-start process %d FAILED (status %d)\n", p, st); failures++; } }
    }
    printf("maktabasauti cold start: %d fresh processes x %d simultaneous threads x %zu inputs (%d open OK), %d failures\n", nproc, nthreads, n, opened, failures);
    free(ref); corpus_free(&corpus);
    return failures ? 1 : 0;
}
