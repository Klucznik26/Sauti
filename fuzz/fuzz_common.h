/* MaktabaSauti: wspolne dla celow fuzzingu (libFuzzer: LLVMFuzzerTestOneInput; tryb powtorki bez clanga: fuzz/standalone_main.c).  Cele nie maja stanu globalnego, kazde wywolanie
 * jest niezalezne; kazdy wynik biblioteki jest dozwolony poza awaria, naruszeniem pamieci, UB, wyciekiem i zawieszeniem (limity czasu/pamieci ustawia uruchamiajacy). */
#ifndef MKS_FUZZ_COMMON_H
#define MKS_FUZZ_COMMON_H
#include "maktabasauti/maktabasauti.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

/* alokator sledzacy: po zamknieciu wszystko musi wrocic (wyciek = abort => blad fuzzingu) */
typedef struct { size_t live; long bad; } FzMem;
static void *fz_alloc(void *u, size_t n) { FzMem *m = (FzMem *)u; size_t *p = (size_t *)malloc(n + 32); if (!p) return NULL; p[0] = n; m->live += n; return (uint8_t *)p + 32; }
static void fz_free(void *u, void *q) { FzMem *m = (FzMem *)u; size_t *p; if (!q) { m->bad++; return; } p = (size_t *)((uint8_t *)q - 32); m->live -= p[0]; free(p); }
static maktabasauti_allocator_t fz_allocator(FzMem *m) { maktabasauti_allocator_t a; memset(&a, 0, sizeof a); a.struct_size = sizeof a; a.user = m; a.alloc = fz_alloc; a.realloc = NULL; a.free = fz_free; return a; }
static void fz_check_mem(const FzMem *m) { if (m->live != 0 || m->bad != 0) abort(); }

/* czyta ze zdekodowanego strumienia ograniczona liczbe porcji (zawsze konczy sie szybko) */
static void fz_exercise(maktabasauti_decoder_t *d) {
    static const maktabasauti_sample_format_t fm[3] = { MAKTABASAUTI_FORMAT_F32, MAKTABASAUTI_FORMAT_S16, MAKTABASAUTI_FORMAT_S24 };
    unsigned char buf[2048 * 8 * 4]; maktabasauti_stream_info_t info; size_t got = 0, i; uint64_t act = 0;
    info.struct_size = sizeof info;
    if (maktabasauti_decoder_get_info(d, &info) != MAKTABASAUTI_OK || info.channels == 0 || info.channels > 8) return;
    for (i = 0; i < 6; i++) { maktabasauti_result_t r = maktabasauti_decoder_read(d, buf, 2048, fm[i % 3], &got); if (r != MAKTABASAUTI_OK) break; }
    (void)maktabasauti_decoder_seek(d, info.total_frames != MAKTABASAUTI_UNKNOWN_LENGTH ? info.total_frames / 3u : 1000u, MAKTABASAUTI_SEEK_EXACT, &act);
    (void)maktabasauti_decoder_read(d, buf, 1024, MAKTABASAUTI_FORMAT_F32, &got);
    (void)maktabasauti_decoder_flush(d);
    (void)maktabasauti_decoder_read(d, buf, 512, MAKTABASAUTI_FORMAT_S16, &got);
    (void)maktabasauti_decoder_reset(d);
    (void)maktabasauti_decoder_read(d, buf, 512, MAKTABASAUTI_FORMAT_F32, &got);
}

/* MemorySanitizer: jadra asemblerowe NASM nie sa instrumentowane (MSan nie widzi ich zapisow i zglasza falszywe niezainicjowane odczyty), wiec przy -DMKS_FUZZ_PURE_C cele uzywaja rejestru
 * z kodekami napisanymi w czystym C (WAV, VOC, AIFF, Vorbis, MIDI, FLAC, DTM, MP3); inaczej rejestr domyslny. Zwraca NULL = domyslny. */
#ifdef MKS_FUZZ_PURE_C
#include "mks_internal.h"
static maktabasauti_registry_t *fz_pure_registry(void) {
    static const char *const names[] = { "wav-pcm", "creative-voc-pcm", "aiff-pcm", "ogg-vorbis-stb", "midi-basic-synth", "flac-drflac", "atari-falcon-digital-tracker", "mp3-minimp3" };
    size_t n, i, k; const maktabasauti_codec_t *const *all = mks_legacy_codecs(&n); maktabasauti_registry_t *r = NULL;
    if (maktabasauti_registry_create(NULL, &r) != MAKTABASAUTI_OK) return NULL;
    for (k = 0; k < sizeof names / sizeof *names; k++) for (i = 0; i < n; i++) if (!strcmp(all[i]->name, names[k])) (void)maktabasauti_registry_add(r, all[i]);
    return r;
}
#else
static maktabasauti_registry_t *fz_pure_registry(void) { return NULL; }
#endif
#endif
