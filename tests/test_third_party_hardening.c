/* Regresje bledow znalezionych w bibliotekach zewnetrznych przez audyt sond / fuzzing (tests/test_maktabasauti_probe_audit.c); zmiany w third_party opisane w
 * third_party/<biblioteka>/README-SAUTI.md i docs/AUDYT_SOND.md.  Kazdy przypadek tu odtwarza konkretny blad na prawdziwym (malym, wygenerowanym) pliku z poprawnie zmienionymi polami:
 *
 *   stb_vorbis:  (1) liczba komentarzy 0x20000001 -> sizeof(char*)*N obciete do int = alokacja 8 bajtow i zapis daleko poza nia (przepelnienie sterty);
 *                (2) dlugosc vendor/komentarza 0x7FFFFFFF / 0xFFFFFFFF -> alokacje rzedu GiB i miliardy iteracji, len+1 = przepelnienie int;
 *                (3) blad w naglowku ustawien: vorbis_deinit() zwalniala niezainicjowane wskazniki (SIGSEGV w free), wskazniki do pamieci tymczasowej (nieprawidlowe free) i
 *                    NULL comment_list (dereferencja NULL); bloki tymczasowe wyciekaly przy kazdym bledzie; pelny przemiatacz: obciecie na kazdej dlugosci i trzy wartosci w kazdym
 *                    bajcie 4 KiB naglowkow, kazde otwarcie musi sie skonczyc bez awarii i wycieku (LeakSanitizer w buildzie ASan);
 *                (4) get32_packet: przesuniecie wartosci ujemnej (EOP = -1) = UB.
 *   minimp3:     (5) rozmiar APEv2 >= 2^31 (przesuniecie int o 24 bity = UB), (6) liczba ramek w znaczniku Xing >= 2^31 (to samo), (7) niezainicjowane free_format_bytes (b2bdd74).
 *
 * usage: test_third_party_hardening probe_real.ogg probe_real.mp3   -- budowac z ASan+UBSan (-fno-sanitize-recover) i bez sanitizerow */
#include "sauti_decoder.h"
#include "sauti_decoder_mp3.h"
#include "sauti_decoder_vorbis.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
#define CHECK(cond, ...) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); g_failures++; } } while (0)

static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); long sz; uint8_t *b;
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    b = (uint8_t *)malloc((size_t)sz);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) exit(2);
    fclose(f); *n = (size_t)sz;
    return b;
}
static void put_le32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* otwiera i (jesli sie uda) czyta troche, potem zamyka; zwraca 1 gdy otwarcie sie powiodlo.  Wejscie to kopia o dokladnym rozmiarze (ASan widzi kazdy odczyt poza nia). */
static int open_read_close(const SautiDecoderOps *ops, const uint8_t *src, size_t n) {
    uint8_t *copy = (uint8_t *)malloc(n ? n : 1);
    SautiPcmInfo info; void *st; int ok = 0;
    if (!copy) abort();
    if (n) memcpy(copy, src, n);
    memset(&info, 0, sizeof info);
    st = ops->open(copy, n, &info);
    if (st) {
        static float out[8192 * 2];
        size_t want = info.channels ? 4096 / info.channels : 0;
        if (info.channels >= 1 && info.channels <= 2) { (void)ops->read(st, out, want ? want : 1); (void)ops->seek(st, 0); (void)ops->read(st, out, want ? want : 1); }
        ops->close(st);
        ok = 1;
    }
    free(copy);
    return ok;
}

static size_t find(const uint8_t *b, size_t n, const char *pat, size_t plen) {
    size_t i;
    for (i = 0; i + plen <= n; i++) if (!memcmp(b + i, pat, plen)) return i;
    return (size_t)-1;
}

static void test_vorbis(const uint8_t *ogg, size_t n) {
    size_t at = find(ogg, n, "\x03vorbis", 7), k, i; uint8_t *w;
    uint32_t vendor_len;
    CHECK(at != (size_t)-1 && at + 15 < n, "comment header not found in the Ogg fixture");
    if (at == (size_t)-1) return;
    CHECK(open_read_close(&sauti_decoder_vorbis_ops, ogg, n), "the unmodified Ogg fixture must open");
    vendor_len = (uint32_t)ogg[at + 7] | ((uint32_t)ogg[at + 8] << 8) | ((uint32_t)ogg[at + 9] << 16) | ((uint32_t)ogg[at + 10] << 24);
    w = (uint8_t *)malloc(n);
    if (!w) abort();
    {   /* (1) liczba komentarzy: wartosci, dla ktorych sizeof(char*)*N obcina sie do int */
        static const uint32_t counts[] = { 0x20000001u, 0x20000000u, 0x40000001u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 65537u, 0x1FFFFFFFu };
        size_t cnt_at = at + 11 + vendor_len;
        for (k = 0; k < sizeof counts / sizeof *counts; k++) {
            memcpy(w, ogg, n); if (cnt_at + 4 <= n) put_le32(w + cnt_at, counts[k]);
            CHECK(!open_read_close(&sauti_decoder_vorbis_ops, w, n), "comment count 0x%08x must be rejected", counts[k]);
        }
    }
    {   /* (2) dlugosc vendor */
        static const uint32_t lens[] = { 0x7FFFFFFFu, 0xFFFFFFFFu, 0x80000000u, 0x01000001u, 0x00FFFFFFu, 0x20000001u };
        for (k = 0; k < sizeof lens / sizeof *lens; k++) {
            memcpy(w, ogg, n); put_le32(w + at + 7, lens[k]);
            CHECK(!open_read_close(&sauti_decoder_vorbis_ops, w, n), "vendor length 0x%08x must be rejected", lens[k]);
        }
    }
    {   /* (2b) dlugosc pierwszego komentarza (jesli jest) */
        size_t cnt_at = at + 11 + vendor_len;
        uint32_t cnt = cnt_at + 4 <= n ? (uint32_t)ogg[cnt_at] : 0;
        if (cnt_at + 8 <= n && cnt >= 1) {
            static const uint32_t lens[] = { 0x7FFFFFFFu, 0xFFFFFFFFu, 0x01000001u };
            for (k = 0; k < sizeof lens / sizeof *lens; k++) {
                memcpy(w, ogg, n); put_le32(w + cnt_at + 4, lens[k]);
                CHECK(!open_read_close(&sauti_decoder_vorbis_ops, w, n), "comment length 0x%08x must be rejected", lens[k]);
            }
        }
    }
    /* (3) przemiatacz: obciecie na kazdej dlugosci i trzy wartosci w kazdym bajcie pierwszych 4 KiB (naglowki); wynik otwarcia nie ma znaczenia, liczy sie brak awarii/wycieku */
    {
        static const uint8_t vals[] = { 0x00, 0xFF, 0x80 };
        size_t lim = n < 4096 ? n : 4096, opens = 0;
        for (i = 0; i <= n; i += (i < 4200 ? 1 : 97)) (void)open_read_close(&sauti_decoder_vorbis_ops, ogg, i);
        for (i = 0; i < lim; i++)
            for (k = 0; k < sizeof vals; k++) { memcpy(w, ogg, n); w[i] = vals[k]; opens += (size_t)open_read_close(&sauti_decoder_vorbis_ops, w, n); }
        CHECK(opens > 0, "no mutated variant opened at all: the sweep is not reaching the decoder");
        printf("  vorbis: %zu of %zu single-byte variants still opened, rest rejected cleanly\n", opens, lim * sizeof vals);
    }
    free(w);
}

static void test_mp3(const uint8_t *mp3, size_t n) {
    uint8_t *w = (uint8_t *)malloc(n + 1024); size_t at, k;
    if (!w) abort();
    CHECK(open_read_close(&sauti_decoder_mp3_ops, mp3, n), "the unmodified MP3 fixture must open");
    /* (6) licznik ramek w Xing/Info: pole "frames" za flagami (4 bajty big-endian) = 0xFF000000 */
    at = find(mp3, n < 400 ? n : 400, "Xing", 4); if (at == (size_t)-1) at = find(mp3, n < 400 ? n : 400, "Info", 4);
    if (at != (size_t)-1 && at + 12 <= n) {
        static const uint32_t frames[] = { 0xFF000000u, 0x80000000u, 0xFFFFFFFFu, 0x7FFFFFFFu, 0u };
        for (k = 0; k < sizeof frames / sizeof *frames; k++) {
            memcpy(w, mp3, n); w[at + 7] |= 1;                                   /* flaga FRAMES_FLAG */
            w[at + 8] = (uint8_t)(frames[k] >> 24); w[at + 9] = (uint8_t)(frames[k] >> 16); w[at + 10] = (uint8_t)(frames[k] >> 8); w[at + 11] = (uint8_t)frames[k];
            (void)open_read_close(&sauti_decoder_mp3_ops, w, n);                 /* wynik nieistotny: bez UB, bez awarii */
        }
    } else printf("  mp3: no Xing/Info tag in the fixture, Xing check skipped\n");
    /* (5) APEv2 na koncu z rozmiarem >= 2^31 */
    for (k = 0; k < 4; k++) {
        static const uint32_t sizes[] = { 0xFF000000u, 0x80000000u, 0xFFFFFFFFu, 0x7FFFFFFFu };
        memcpy(w, mp3, n); memset(w + n, 0, 64);
        memcpy(w + n + 32 - 32, "APETAGEX", 8);                                   /* stopka: ostatnie 32 bajty pliku */
        put_le32(w + n + 8 + 4, sizes[k]);                                         /* rozmiar tagu w stopce (offset 12) */
        (void)open_read_close(&sauti_decoder_mp3_ops, w, n + 32);
    }
    /* (7) naglowek free-format w buforze o dokladnym rozmiarze (patrz tests/test_decoder_mp3.c: ten sam przypadek, tu przez cale open) */
    {
        uint8_t ff[2000]; memset(ff, 0, sizeof ff); ff[0] = 0xFF; ff[1] = 0xFB;
        (void)open_read_close(&sauti_decoder_mp3_ops, ff, sizeof ff);
    }
    free(w);
}

int main(int argc, char **argv) {
    size_t no, nm; uint8_t *ogg, *mp3;
    if (argc < 3) { fprintf(stderr, "usage: %s probe_real.ogg probe_real.mp3\n", argv[0]); return 2; }
    ogg = slurp(argv[1], &no); mp3 = slurp(argv[2], &nm);
    test_vorbis(ogg, no);
    test_mp3(mp3, nm);
    free(ogg); free(mp3);
    printf("third-party hardening: %s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}
