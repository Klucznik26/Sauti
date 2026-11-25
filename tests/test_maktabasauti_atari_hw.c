/* MaktabaSauti: pliki AVR, SND (Sun/NeXT AU) i DVS wyprodukowane przez PRAWDZIWY program 525 ("Five to Five", WinRec 1.41) w emulatorze Hatari z syntetycznych WAV-ow
 * (tests/data/atari_hw/, tests/data/make_atari_hw.py).  Wzorce: AVR i SND = FFmpeg, DVS = dekoder samego programu 525; wszystkie probki musza byc IDENTYCZNE (takze deltapack i 8-bit).
 * Kazdy plik jest dekodowany dwa razy: wejscie przewijalne ze znanym rozmiarem oraz nieprzewijalne bez rozmiaru z odczytami po 7 bajtow.  Program 525 zapisuje w naglowku AVR mono
 * liczbe probek zrodla (za duzo): przy znanym rozmiarze plik musi sie konczyc czysto (EOF), bez rozmiaru E_TRUNCATED po wszystkich danych.  Test czyta pliki wzgledem katalogu repozytorium. */
#define _POSIX_C_SOURCE 200809L
#include "maktabasauti/maktabasauti.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #c); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); g_failures++; } } while (0)

typedef struct { const uint8_t *data; size_t n, pos, max_chunk; int seekable, size_known; } Src;
static int64_t MAKTABASAUTI_CALL s_read(void *u, void *buf, size_t size) {
    Src *s = (Src *)u; size_t give = s->n - s->pos;
    if (s->max_chunk && size > s->max_chunk) size = s->max_chunk;
    if (give > size) give = size;
    if (give) memcpy(buf, s->data + s->pos, give);
    s->pos += give;
    return (int64_t)give;
}
static int64_t MAKTABASAUTI_CALL s_seek(void *u, int64_t off, int wh) {
    Src *s = (Src *)u; const int64_t base = wh == 0 ? 0 : wh == 1 ? (int64_t)s->pos : (int64_t)s->n;
    if (base + off < 0 || (uint64_t)(base + off) > (uint64_t)s->n) return MAKTABASAUTI_E_OUT_OF_RANGE;
    s->pos = (size_t)(base + off); return base + off;
}
static int64_t MAKTABASAUTI_CALL s_size(void *u) { return (int64_t)((Src *)u)->n; }
static void MAKTABASAUTI_CALL s_close(void *u) { (void)u; }
static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); uint8_t *b; long sz;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    b = (uint8_t *)malloc((size_t)sz + 1u);
    if (b && fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); b = NULL; }
    fclose(f); *n = (size_t)sz; return b;
}

typedef struct { const char *name, *ext; unsigned channels, rate; int overstated; } Fx;
static void run(const Fx *x, const uint8_t *file, size_t fn, const int16_t *ref, size_t rn, int known) {
    Src s; maktabasauti_open_params_t p; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info; maktabasauti_result_t r;
    float *out = (float *)malloc((rn + 64) * sizeof(float)); size_t cnt = 0, i;
    memset(&s, 0, sizeof s); s.data = file; s.n = fn; s.seekable = known; s.size_known = known; s.max_chunk = known ? 0 : 7;
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION;
    p.io.struct_size = sizeof p.io; p.io.user = &s; p.io.read = s_read; p.io.close = s_close; p.io.seek = s.seekable ? s_seek : NULL; p.io.size = s.size_known ? s_size : NULL;
    r = maktabasauti_decoder_open(&p, &d);
    CHECK(r == MAKTABASAUTI_OK, "%s %s: open (%d)", x->name, known ? "known" : "stream", (int)r);
    if (r != MAKTABASAUTI_OK) { free(out); return; }
    memset(&info, 0, sizeof info); info.struct_size = sizeof info; maktabasauti_decoder_get_info(d, &info);
    CHECK(info.channels == x->channels && info.sample_rate == x->rate, "%s: %u ch @ %u Hz, expected %u @ %u", x->name, info.channels, info.sample_rate, x->channels, x->rate);
    if (known) CHECK(info.total_frames == rn / x->channels, "%s: length %llu vs %zu", x->name, (unsigned long long)info.total_frames, rn / x->channels);
    for (;;) {
        size_t got = 0; r = maktabasauti_decoder_read(d, out + cnt, 300, MAKTABASAUTI_FORMAT_F32, &got);
        if (got && cnt + got * x->channels <= rn + 64) cnt += got * x->channels; else if (got) { CHECK(0, "%s: more data than the reference", x->name); break; }
        if (r != MAKTABASAUTI_OK) break;
    }
    CHECK(r == MAKTABASAUTI_EOF || (!known && x->overstated && r == MAKTABASAUTI_E_TRUNCATED), "%s %s: end %d", x->name, known ? "known" : "stream", (int)r);
    CHECK(cnt == rn, "%s %s: %zu samples vs %zu", x->name, known ? "known" : "stream", cnt, rn);
    for (i = 0; i < cnt && i < rn; i++) if (out[i] != (float)ref[i] / 32768.0f) { CHECK(0, "%s %s: sample %zu is %.9g, expected %.9g", x->name, known ? "known" : "stream", i, (double)out[i], (double)ref[i] / 32768.0); break; }
    maktabasauti_decoder_close(d); free(out);
}
int main(void) {
    static const Fx fx[] = {
        { "avr_st16", "avr", 2, 44100, 0 }, { "avr_st8", "avr", 2, 44100, 0 }, { "avr_mo16", "avr", 1, 44100, 1 }, { "avr_mo8", "avr", 1, 44100, 1 }, { "avr_m_mo16", "avr", 1, 44100, 1 },
        { "snd_st16", "snd", 2, 44100, 0 }, { "snd_st16ulaw", "snd", 2, 44100, 0 }, { "snd_st8", "snd", 2, 44100, 0 },
        { "snd_mo16", "snd", 1, 44100, 0 }, { "snd_mo16ulaw", "snd", 1, 44100, 0 }, { "snd_mo8", "snd", 1, 44100, 0 },
        { "dvs_st16", "dvs", 2, 49170, 0 }, { "dvs_st16dp", "dvs", 2, 49170, 0 }, { "dvs_st8", "dvs", 2, 49170, 0 }, { "dvs_mo8", "dvs", 1, 49170, 0 },
    };
    size_t k;
    for (k = 0; k < sizeof fx / sizeof fx[0]; k++) {
        char pa[160], pr[160]; size_t fn, rn; uint8_t *file, *rb;
        snprintf(pa, sizeof pa, "tests/data/atari_hw/%s.%s", fx[k].name, fx[k].ext); snprintf(pr, sizeof pr, "tests/data/atari_hw/%s.ref", fx[k].name);
        file = slurp(pa, &fn); rb = slurp(pr, &rn);
        CHECK(file && rb && rn % 2u == 0u, "%s: missing fixture (run from the repository root)", fx[k].name);
        if (file && rb && rn % 2u == 0u) {
            int16_t *ref = (int16_t *)malloc(rn); size_t i; for (i = 0; i < rn / 2u; i++) ref[i] = (int16_t)(rb[2 * i] | (rb[2 * i + 1] << 8));
            run(&fx[k], file, fn, ref, rn / 2u, 1); run(&fx[k], file, fn, ref, rn / 2u, 0); free(ref);
        }
        free(file); free(rb);
    }
    if (g_failures) { fprintf(stderr, "Atari hardware fixtures: %d FAILURES\n", g_failures); return 1; }
    printf("Atari hardware fixtures: PASS (%zu files from the real 525 converter)\n", sizeof fx / sizeof fx[0]);
    return 0;
}
