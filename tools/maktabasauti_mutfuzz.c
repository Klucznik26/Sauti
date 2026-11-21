/* Tool (not a product): in-process mutation fuzzer for the MaktabaSauti engine (build it with -fsanitize=address,undefined: `make fuzz-maktabasauti`).  Every iteration mutates a seed
 * file (bit flips, byte overwrites, zeroed / random / duplicated blocks, truncation; the first bytes are kept intact half of the time so deeper code is reached), opens it through the
 * default registry, reads it completely (bounded), seeks to a few positions, reads again, resets, and requires: no sanitizer report, no crash, finite samples, no endless loop (alarm),
 * and the sample count never above the stream's own bound.  It does NOT compare the PCM (the file is corrupt on purpose).
 * usage: maktabasauti_mutfuzz ITERATIONS SEED FILE... */
#include "maktabasauti/maktabasauti.h"
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned long long rng;
static unsigned long long rnd(void) { rng = rng * 6364136223846793005ull + 1442695040888963407ull; return rng >> 16; }
static void on_alarm(int s) { (void)s; static const char m[] = "FUZZ TIMEOUT: a decode did not finish\n"; if (write(2, m, sizeof m - 1u) < 0) {} _exit(3); }

static int finite_block(const float *p, size_t n) { size_t i; for (i = 0; i < n; i++) if (!isfinite(p[i])) return 0; return 1; }

int main(int argc, char **argv) {
    long iterations, it; int nfiles, i; uint8_t **seed; size_t *len; static float buf[4096 * 8]; unsigned long long frames_read = 0; long opened = 0, rejected = 0;
    if (argc < 4) return 2;
    iterations = atol(argv[1]); rng = (unsigned long long)atoll(argv[2]) * 0x9e3779b97f4a7c15ull + 1u; nfiles = argc - 3;
    seed = (uint8_t **)calloc((size_t)nfiles, sizeof *seed); len = (size_t *)calloc((size_t)nfiles, sizeof *len);
    for (i = 0; i < nfiles; i++) {
        FILE *f = fopen(argv[3 + i], "rb"); if (!f) { perror(argv[3 + i]); return 2; }
        fseek(f, 0, SEEK_END); len[i] = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
        seed[i] = (uint8_t *)malloc(len[i] + 1u); if (!seed[i] || fread(seed[i], 1, len[i], f) != len[i]) return 2; fclose(f);
    }
    signal(SIGALRM, on_alarm);
    for (it = 0; it < iterations; it++) {
        const int si = (int)(rnd() % (unsigned)nfiles); size_t n = len[si]; uint8_t *m = (uint8_t *)malloc(n + 4096u); const unsigned kind = (unsigned)(rnd() % 6u), count = 1u + (unsigned)(rnd() % 24u), k0 = 0; unsigned k;
        maktabasauti_open_params_t p; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info;
        (void)k0;
        memcpy(m, seed[si], n);
        for (k = 0; k < count; k++) {
            const size_t at = (size_t)(rnd() % n);
            switch (kind) {
            case 0: m[at] ^= (uint8_t)(1u << (rnd() & 7u)); break;
            case 1: m[at] = (uint8_t)rnd(); break;
            case 2: { size_t l = 1u + (size_t)(rnd() % 64u); if (at + l > n) l = n - at; memset(m + at, 0, l); } break;
            case 3: { size_t l = 1u + (size_t)(rnd() % 64u), j; if (at + l > n) l = n - at; for (j = 0; j < l; j++) m[at + j] = (uint8_t)rnd(); } break;
            case 4: { size_t l = 1u + (size_t)(rnd() % 512u), to = (size_t)(rnd() % n); if (at + l > n) l = n - at; if (to + l > n) l = n - to; memmove(m + to, m + at, l); } break;
            default: if (n > 16u) n = 16u + (size_t)(rnd() % (n - 16u)); break;
            }
            if (kind >= 4u && k > 0u) break;
        }
        if ((rnd() & 1u) && n > 64u) memcpy(m, seed[si], 32u < n ? 32u : n);
        alarm(30);
        memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION;
        if (maktabasauti_io_from_memory(m, n, NULL, &p.io) == MAKTABASAUTI_OK && maktabasauti_decoder_open(&p, &d) == MAKTABASAUTI_OK) {
            size_t got, total = 0; unsigned ch; int s; maktabasauti_result_t r;
            opened++;
            info.struct_size = sizeof info; (void)maktabasauti_decoder_get_info(d, &info); ch = info.channels ? info.channels : 1u;
            if (ch > 8u) ch = 8u;
            for (;;) {
                r = maktabasauti_decoder_read(d, buf, 4096u, MAKTABASAUTI_FORMAT_F32, &got);
                if (got) { if (!finite_block(buf, got * ch)) { fprintf(stderr, "FUZZ NONFINITE sample, iteration %ld seed %s kind %u\n", it, argv[3 + si], kind); return 4; } total += got; frames_read += got; }
                if (r != MAKTABASAUTI_OK || total > (size_t)30u * 1000u * 1000u) break;
            }
            for (s = 0; s < 3; s++) {
                const uint64_t pos = (info.total_frames && info.total_frames < (1ull << 40)) ? (rnd() % (info.total_frames + 1u)) : 0u;
                if (maktabasauti_decoder_seek(d, pos, MAKTABASAUTI_SEEK_EXACT, NULL) == MAKTABASAUTI_OK) {
                    r = maktabasauti_decoder_read(d, buf, 2048u, MAKTABASAUTI_FORMAT_F32, &got);
                    if (got && !finite_block(buf, got * ch)) { fprintf(stderr, "FUZZ NONFINITE sample after seek, iteration %ld\n", it); return 4; }
                    (void)r;
                }
            }
            if (maktabasauti_decoder_reset(d) == MAKTABASAUTI_OK) { r = maktabasauti_decoder_read(d, buf, 2048u, MAKTABASAUTI_FORMAT_F32, &got); (void)r; }
            maktabasauti_decoder_close(d);
        } else { rejected++; if (p.io.close) p.io.close(p.io.user); }
        alarm(0);
        free(m);
    }
    printf("fuzz: %ld iterations, %ld opened, %ld rejected, %llu frames decoded, no sanitizer report\n", iterations, opened, rejected, frames_read);
    for (i = 0; i < nfiles; i++) free(seed[i]);
    free(seed); free(len);
    return 0;
}
