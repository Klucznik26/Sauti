/* MIDI: przewijanie ma ograniczona prace (docs/AUDYT_SOND.md R2).  Uszkodzony plik deklarujacy godziny/dni nie moze zablokowac aplikacji.
 *  1. cisza jest pomijana DOKLADNIE: seek z przeskokiem ciszy daje te same probki co odczyt ramka po ramce;
 *  2. plik z cisza trwajaca dni: seek na koniec/srodek wraca natychmiast (O(zdarzenia)), nie O(dni);
 *  3. plik z nuta trzymana godzinami (glos gra caly czas): seek daleko konczy sie niepowodzeniem w ograniczonym czasie, a stan zostaje bez zmian (odczyt dalej od tej samej pozycji);
 *  4. przez nowe API: ten przypadek to MAKTABASAUTI_E_NOT_SEEKABLE, dekoder nadal sprawny; seek(0)/reset zawsze dziala.
 * usage: test_midi_seek_bounds */
#define _POSIX_C_SOURCE 200809L
#include "maktabasauti/maktabasauti.h"
#include "sauti_decoder.h"
#include "sauti_decoder_midi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec + (double)t.tv_nsec * 1e-9; }

/* SMF format 0, division 96 tpq, domyslne tempo 500000 us/cwiercnute => 1 tik = 5208,33 us.  Zdarzenia: (delta, bajty...) */
typedef struct { unsigned char b[256]; size_t n; } Midi;
static void put(Midi *m, const unsigned char *p, size_t n) { memcpy(m->b + m->n, p, n); m->n += n; }
static void vlq(unsigned char *o, size_t *n, unsigned v) { unsigned char t[5]; int k = 0, i; t[k++] = v & 0x7f; while ((v >>= 7)) t[k++] = (unsigned char)((v & 0x7f) | 0x80); for (i = k - 1; i >= 0; i--) o[(*n)++] = t[i]; }
static void build(Midi *m, unsigned gap_before_on, unsigned note_len, int voices) {
    unsigned char trk[128]; size_t n = 0; static const unsigned char head[] = { 'M','T','h','d',0,0,0,6,0,0,0,1,0,96 };
    memset(m, 0, sizeof *m); put(m, head, sizeof head);
    { int v; for (v = 0; v < voices; v++) { vlq(trk, &n, v == 0 ? gap_before_on : 0); trk[n++] = 0x90; trk[n++] = (unsigned char)(69 + v); trk[n++] = 100; }
      for (v = 0; v < voices; v++) { vlq(trk, &n, v == 0 ? note_len : 0); trk[n++] = 0x80; trk[n++] = (unsigned char)(69 + v); trk[n++] = 0; } }
    trk[n++] = 0; trk[n++] = 0xff; trk[n++] = 0x2f; trk[n++] = 0;
    put(m, (const unsigned char *)"MTrk", 4); { unsigned char l[4] = { 0, 0, 0, (unsigned char)n }; put(m, l, 4); } put(m, trk, n);
}

static const SautiDecoderOps *const reg[] = { &sauti_decoder_midi_ops };

int main(void) {
    Midi m; SautiDecoder d, ref; static float a[4096 * 2], b[4096 * 2]; int i; double t0;

    /* 1. cisza 2 s, potem nuta; seek (przeskok ciszy) == odczyt ramka po ramce */
    build(&m, 2 * 192, 96, 1);                       /* 2 s = 384 tikow */
    assert(sauti_legacy_decoder_open(&d, reg, 1, m.b, m.n) && sauti_legacy_decoder_open(&ref, reg, 1, m.b, m.n));
    {
        const uint64_t targets[] = { 1, 44100, 88200 - 1, 88200, 88200 + 1, 100000, 120000 };
        for (i = 0; i < (int)(sizeof targets / sizeof *targets); i++) {
            uint64_t k = targets[i], done = 0;
            assert(sauti_legacy_decoder_seek(&d, k));
            assert(sauti_legacy_decoder_seek(&ref, 0));
            while (done < k) { size_t n = k - done > 4096 ? 4096 : (size_t)(k - done); assert(sauti_legacy_decoder_read(&ref, a, n) == n); done += n; }
            assert(sauti_legacy_decoder_read(&d, a, 2048) == 2048 && sauti_legacy_decoder_read(&ref, b, 2048) == 2048);
            assert(memcmp(a, b, 2048 * 2 * sizeof(float)) == 0);                     /* bit w bit */
        }
    }
    sauti_legacy_decoder_close(&d); sauti_legacy_decoder_close(&ref);

    /* 2. cisza 1,4 godziny (1 000 000 tikow; wieksze delty tml i tak odrzuca); seek na srodek i na koniec ma byc natychmiastowy, nie O(godzin) */
    build(&m, 1000000u, 96, 1);
    assert(sauti_legacy_decoder_open(&d, reg, 1, m.b, m.n));
    t0 = now_s();
    assert(d.info.total_frames > (uint64_t)2e8);
    assert(sauti_legacy_decoder_seek(&d, d.info.total_frames / 2));
    assert(sauti_legacy_decoder_seek(&d, d.info.total_frames - 1));
    assert(sauti_legacy_decoder_seek(&d, 0));
    assert(now_s() - t0 < 2.0);
    sauti_legacy_decoder_close(&d);

    /* 3. trzy nuty trzymane 1,4 godziny: glosy graja caly czas; seek daleko = niepowodzenie w ograniczonym czasie, stan bez zmian */
    build(&m, 0, 1000000u, 3);
    assert(sauti_legacy_decoder_open(&d, reg, 1, m.b, m.n) && sauti_legacy_decoder_open(&ref, reg, 1, m.b, m.n));
    assert(sauti_legacy_decoder_read(&d, a, 1000) == 1000 && sauti_legacy_decoder_read(&ref, b, 1000) == 1000);
    t0 = now_s();
    assert(!sauti_legacy_decoder_seek(&d, d.info.total_frames / 2));
    assert(now_s() - t0 < 30.0);
    assert(sauti_legacy_decoder_read(&d, a, 1000) == 1000 && sauti_legacy_decoder_read(&ref, b, 1000) == 1000);
    assert(memcmp(a, b, 1000 * 2 * sizeof(float)) == 0);                              /* pozycja i stan zachowane po nieudanym seek */
    assert(sauti_legacy_decoder_seek(&d, 5000));                                       /* bliskie przewijanie do przodu nadal dziala */
    assert(sauti_legacy_decoder_seek(&d, 0));
    sauti_legacy_decoder_close(&d); sauti_legacy_decoder_close(&ref);

    /* 4. nowe API */
    {
        maktabasauti_io_t io; maktabasauti_open_params_t p; maktabasauti_decoder_t *h = NULL; size_t got; maktabasauti_stream_info_t info; uint64_t actual = 0;
        assert(maktabasauti_io_from_memory(m.b, m.n, NULL, &io) == MAKTABASAUTI_OK);
        memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION; p.io = io;
        assert(maktabasauti_decoder_open(&p, &h) == MAKTABASAUTI_OK);
        info.struct_size = sizeof info; assert(maktabasauti_decoder_get_info(h, &info) == MAKTABASAUTI_OK);
        t0 = now_s();
        assert(maktabasauti_decoder_seek(h, info.total_frames / 2, MAKTABASAUTI_SEEK_EXACT, &actual) == MAKTABASAUTI_E_NOT_SEEKABLE);
        assert(now_s() - t0 < 30.0);
        assert(maktabasauti_decoder_read(h, a, 100, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 100);   /* dekoder sprawny po bledzie */
        assert(maktabasauti_decoder_seek(h, 0, MAKTABASAUTI_SEEK_EXACT, &actual) == MAKTABASAUTI_OK);
        assert(maktabasauti_decoder_reset(h) == MAKTABASAUTI_OK);
        maktabasauti_decoder_close(h);
    }
    printf("MIDI seek bounds: PASS\n");
    return 0;
}
