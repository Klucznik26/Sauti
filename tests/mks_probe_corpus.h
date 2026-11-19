/* MaktabaSauti: korpus do audytu sond (probe) i testow wielowatkowych.  Syntetyczne, MINIMALNE pliki wszystkich formatow z rejestru domyslnego plus kilka "obcych"
 * danych (AVI, PNG, gzip, tekst, zera, 0xFF, sama etykieta ID3).  Nie zawiera zadnych danych uzytkownika: wszystko generowane z kodu.  Pliki nie musza dawac sie
 * w pelni zdekodowac (probe i tak ocenia tylko naglowek); te, ktore sie dekoduja, daja otwarcia w testach "open".
 * Tylko do testow: zadnych zaleznosci poza biblioteka standardowa C. */
#ifndef MKS_PROBE_CORPUS_H
#define MKS_PROBE_CORPUS_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    uint8_t *bytes;
    size_t n;
    char *owned_name;                  /* nazwa z malloc (pliki z dysku) albo NULL */
    const char *owner;                 /* nazwa kodeka rejestru, ktory MA rozpoznac plik (NULL = zaden) */
} corpus_item_t;

typedef struct { corpus_item_t *items; size_t count, cap; } corpus_t;

static inline void corpus_add(corpus_t *c, const char *name, const char *owner, const uint8_t *bytes, size_t n) {
    corpus_item_t *it;
    if (c->count == c->cap) {
        c->cap = c->cap ? c->cap * 2 : 64;
        c->items = (corpus_item_t *)realloc(c->items, c->cap * sizeof *c->items);
        if (!c->items) abort();
    }
    it = &c->items[c->count++];
    it->name = name; it->owner = owner; it->n = n; it->owned_name = NULL;
    it->bytes = (uint8_t *)malloc(n ? n : 1);
    if (!it->bytes) abort();
    if (n) memcpy(it->bytes, bytes, n);
}

static inline void corpus_free(corpus_t *c) {
    size_t i;
    for (i = 0; i < c->count; i++) { free(c->items[i].bytes); free(c->items[i].owned_name); }
    free(c->items);
    memset(c, 0, sizeof *c);
}

/* ---- pomocnicze zapisy ---- */
typedef struct { uint8_t b[8192]; size_t n; } mkbuf_t;
static inline void mk_put(mkbuf_t *m, const void *p, size_t n) { if (m->n + n > sizeof m->b) abort(); memcpy(m->b + m->n, p, n); m->n += n; }
static inline void mk_u8(mkbuf_t *m, unsigned v) { uint8_t b = (uint8_t)v; mk_put(m, &b, 1); }
static inline void mk_le16(mkbuf_t *m, unsigned v) { mk_u8(m, v & 255u); mk_u8(m, (v >> 8) & 255u); }
static inline void mk_le32(mkbuf_t *m, uint32_t v) { mk_le16(m, v & 0xffffu); mk_le16(m, v >> 16); }
static inline void mk_be16(mkbuf_t *m, unsigned v) { mk_u8(m, (v >> 8) & 255u); mk_u8(m, v & 255u); }
static inline void mk_be32(mkbuf_t *m, uint32_t v) { mk_be16(m, v >> 16); mk_be16(m, v & 0xffffu); }
static inline void mk_zero(mkbuf_t *m, size_t n) { while (n--) mk_u8(m, 0); }
static inline void mk_str(mkbuf_t *m, const char *s) { mk_put(m, s, strlen(s)); }
static inline void mk_patch_le32(mkbuf_t *m, size_t at, uint32_t v) { m->b[at] = (uint8_t)v; m->b[at + 1] = (uint8_t)(v >> 8); m->b[at + 2] = (uint8_t)(v >> 16); m->b[at + 3] = (uint8_t)(v >> 24); }
static inline void mk_patch_be32(mkbuf_t *m, size_t at, uint32_t v) { m->b[at] = (uint8_t)(v >> 24); m->b[at + 1] = (uint8_t)(v >> 16); m->b[at + 2] = (uint8_t)(v >> 8); m->b[at + 3] = (uint8_t)v; }
static inline void mk_wave(mkbuf_t *m, size_t samples) {                         /* pseudo-sinus, 16 bit, bez bibliotek matematycznych */
    size_t i; int v = 0, d = 900;
    for (i = 0; i < samples; i++) { v += d; if (v > 12000 || v < -12000) d = -d; mk_le16(m, (unsigned)(v & 0xffff)); }
}

static inline void corpus_add_synthetic(corpus_t *c) {
    mkbuf_t m;
    size_t i, at;

    /* WAV: RIFF/WAVE PCM 16 bit mono 8 kHz */
    memset(&m, 0, sizeof m);
    mk_str(&m, "RIFF"); mk_le32(&m, 0); mk_str(&m, "WAVE"); mk_str(&m, "fmt "); mk_le32(&m, 16);
    mk_le16(&m, 1); mk_le16(&m, 1); mk_le32(&m, 8000); mk_le32(&m, 16000); mk_le16(&m, 2); mk_le16(&m, 16);
    mk_str(&m, "data"); mk_le32(&m, 400); mk_wave(&m, 200);
    mk_patch_le32(&m, 4, (uint32_t)(m.n - 8));
    corpus_add(c, "syn.wav", "wav-pcm", m.b, m.n);

    /* AIFF: COMM (44100 Hz, 80-bit extended) + SSND, 16 bit mono */
    memset(&m, 0, sizeof m);
    mk_str(&m, "FORM"); mk_be32(&m, 0); mk_str(&m, "AIFF"); mk_str(&m, "COMM"); mk_be32(&m, 18);
    mk_be16(&m, 1); mk_be32(&m, 100); mk_be16(&m, 16); mk_be16(&m, 0x400E); mk_be32(&m, 0xAC440000u); mk_be32(&m, 0);
    mk_str(&m, "SSND"); mk_be32(&m, 8 + 200); mk_be32(&m, 0); mk_be32(&m, 0);
    for (i = 0; i < 100; i++) mk_be16(&m, (unsigned)((i * 300u) & 0xffffu));
    mk_patch_be32(&m, 4, (uint32_t)(m.n - 8));
    corpus_add(c, "syn.aiff", "aiff-pcm", m.b, m.n);

    /* VOC: blok 1 (dane 8 bit), terminator */
    memset(&m, 0, sizeof m);
    mk_str(&m, "Creative Voice File"); mk_u8(&m, 0x1a); mk_le16(&m, 26); mk_le16(&m, 0x010A); mk_le16(&m, (~0x010Au + 0x1234u) & 0xffffu);
    mk_u8(&m, 1); mk_u8(&m, (2 + 100) & 255); mk_u8(&m, 0); mk_u8(&m, 0);
    mk_u8(&m, 131); mk_u8(&m, 0);
    for (i = 0; i < 100; i++) mk_u8(&m, 128 + (unsigned)(i & 31));
    mk_u8(&m, 0);
    corpus_add(c, "syn.voc", "creative-voc-pcm", m.b, m.n);

    /* FLAC: sam STREAMINFO (probe i otwarcie naglowka) */
    memset(&m, 0, sizeof m);
    mk_str(&m, "fLaC"); mk_u8(&m, 0x80); mk_u8(&m, 0); mk_u8(&m, 0); mk_u8(&m, 34);
    mk_be16(&m, 4096); mk_be16(&m, 4096); mk_u8(&m, 0); mk_be16(&m, 0); mk_u8(&m, 0); mk_be16(&m, 0);
    { uint64_t v = ((uint64_t)44100 << 44) | ((uint64_t)1 << 41) | ((uint64_t)15 << 36) | 1000u; for (i = 0; i < 8; i++) mk_u8(&m, (unsigned)(v >> (56 - 8 * i)) & 255u); }
    mk_zero(&m, 16);
    corpus_add(c, "syn.flac", "flac-drflac", m.b, m.n);

    /* Ogg Vorbis: strona z naglowkiem identyfikacyjnym */
    memset(&m, 0, sizeof m);
    mk_str(&m, "OggS"); mk_u8(&m, 0); mk_u8(&m, 2); mk_zero(&m, 8); mk_le32(&m, 1); mk_le32(&m, 0); mk_le32(&m, 0); mk_u8(&m, 1); mk_u8(&m, 30);
    mk_u8(&m, 1); mk_str(&m, "vorbis"); mk_le32(&m, 0); mk_u8(&m, 1); mk_le32(&m, 44100); mk_le32(&m, 0); mk_le32(&m, 64000); mk_le32(&m, 0); mk_u8(&m, 0xB8); mk_u8(&m, 1);
    corpus_add(c, "syn.ogg", "ogg-vorbis-stb", m.b, m.n);

    /* MIDI: format 0, jedna nuta */
    memset(&m, 0, sizeof m);
    mk_str(&m, "MThd"); mk_be32(&m, 6); mk_be16(&m, 0); mk_be16(&m, 1); mk_be16(&m, 96);
    mk_str(&m, "MTrk"); mk_be32(&m, 12);
    mk_u8(&m, 0); mk_u8(&m, 0x90); mk_u8(&m, 60); mk_u8(&m, 64); mk_u8(&m, 96); mk_u8(&m, 0x80); mk_u8(&m, 60); mk_u8(&m, 0); mk_u8(&m, 0); mk_u8(&m, 0xFF); mk_u8(&m, 0x2F); mk_u8(&m, 0);
    corpus_add(c, "syn.mid", "midi-basic-synth", m.b, m.n);

    /* Atari AVR: naglowek 128 bajtow + 100 probek 8 bit mono */
    memset(&m, 0, sizeof m);
    mk_str(&m, "2BIT"); mk_put(&m, "synthet\0", 8); mk_be16(&m, 0); mk_be16(&m, 8); mk_be16(&m, 0xFFFF); mk_be16(&m, 0); mk_be16(&m, 0);
    mk_u8(&m, 0); mk_u8(&m, 0); mk_be16(&m, 8000); mk_be32(&m, 100); mk_be32(&m, 0); mk_be32(&m, 100);
    mk_zero(&m, 128 - m.n);
    for (i = 0; i < 100; i++) mk_u8(&m, (unsigned)(i * 3u) & 255u);
    corpus_add(c, "syn.avr", "atari-avr-pcm", m.b, m.n);

    /* Atari Falcon DVSM: PCM 8 bit stereo (mode 0), 16 bajtow naglowka + 64 bajty */
    memset(&m, 0, sizeof m);
    mk_put(&m, "DVSM\0\0", 6); mk_be16(&m, 16); mk_be16(&m, 3); mk_u8(&m, 0); mk_u8(&m, 0); mk_be32(&m, 0);
    for (i = 0; i < 64; i++) mk_u8(&m, (unsigned)(i * 5u) & 255u);
    corpus_add(c, "syn.dvsm", "atari-falcon-dvsm-pcm", m.b, m.n);

    /* Digital Tracker (probe tylko po etykiecie) */
    memset(&m, 0, sizeof m);
    mk_str(&m, "D.T."); mk_zero(&m, 60);
    corpus_add(c, "syn.dtm", "atari-falcon-digital-tracker", m.b, m.n);
    memset(&m, 0, sizeof m);
    mk_str(&m, "SONG"); mk_zero(&m, 60);
    corpus_add(c, "syn_song.dtm", "atari-falcon-digital-tracker", m.b, m.n);

    /* YM3!: 14 rejestrow x 20 klatek */
    memset(&m, 0, sizeof m);
    mk_str(&m, "YM3!");
    for (at = 0; at < 14; at++) for (i = 0; i < 20; i++) mk_u8(&m, at == 7 ? 0x3E : (unsigned)(i & 15));
    corpus_add(c, "syn.ym", "atari-ym2149-experimental", m.b, m.n);
    memset(&m, 0, sizeof m);
    mk_str(&m, "YM6!LeOnArD!"); mk_be32(&m, 10); mk_be32(&m, 1); mk_be16(&m, 0); mk_be32(&m, 0); mk_be32(&m, 2000000); mk_be16(&m, 50); mk_be32(&m, 0); mk_be16(&m, 0);
    mk_u8(&m, 0); mk_u8(&m, 0); mk_u8(&m, 0);                    /* tytul, autor, komentarz (puste) */
    for (at = 0; at < 16; at++) for (i = 0; i < 10; i++) mk_u8(&m, at == 7 ? 0x3E : (unsigned)(i & 15));
    mk_str(&m, "End!");
    corpus_add(c, "syn6.ym", "atari-ym2149-experimental", m.b, m.n);

    /* MP3: ramki MPEG-1 L3 128 kb/s 44,1 kHz (417 B), side info zerowe = cisza; warianty z ID3v2, ID3v1 i APEv2 */
    {
        size_t k;
        memset(&m, 0, sizeof m);
        for (k = 0; k < 8; k++) { mk_u8(&m, 0xFF); mk_u8(&m, 0xFB); mk_u8(&m, 0x90); mk_u8(&m, 0x00); mk_zero(&m, 413); if (k == 3) m.b[m.n - 413 + 0] = 0; }
        corpus_add(c, "syn.mp3", "mp3-minimp3", m.b, m.n);
        {
            mkbuf_t t; memset(&t, 0, sizeof t);
            mk_str(&t, "ID3"); mk_u8(&t, 3); mk_u8(&t, 0); mk_u8(&t, 0); mk_u8(&t, 0); mk_u8(&t, 0); mk_u8(&t, 0); mk_u8(&t, 20); mk_zero(&t, 20);
            mk_put(&t, m.b, 6 * 417);
            corpus_add(c, "syn_id3v2.mp3", "mp3-minimp3", t.b, t.n);
            memset(&t, 0, sizeof t);
            mk_put(&t, m.b, 6 * 417); mk_str(&t, "TAG"); mk_zero(&t, 125);
            corpus_add(c, "syn_id3v1.mp3", "mp3-minimp3", t.b, t.n);
            memset(&t, 0, sizeof t);
            for (k = 0; k < 12; k++) { mk_put(&t, m.b + (k % 8) * 417, 417); }
            mk_zero(&t, 40); mk_str(&t, "APETAGEX"); mk_le32(&t, 2000); mk_le32(&t, 40); mk_zero(&t, 12);
            corpus_add(c, "syn_ape.mp3", "mp3-minimp3", t.b, t.n);
            memset(&t, 0, sizeof t);                                                                                          /* APEv2 z rozmiarem >= 2^31 (UB w oryginalnym minimp3) */
            for (k = 0; k < 12; k++) { mk_put(&t, m.b + (k % 8) * 417, 417); }
            mk_zero(&t, 40); mk_str(&t, "APETAGEX"); mk_le32(&t, 2000); mk_le32(&t, 0xFF000000u); mk_zero(&t, 12);
            corpus_add(c, "syn_ape_huge.mp3", "mp3-minimp3", t.b, t.n);
            memset(&t, 0, sizeof t);
            mk_str(&t, "ID3"); mk_u8(&t, 4); mk_zero(&t, 3); mk_u8(&t, 0x7f); mk_u8(&t, 0x7f); mk_u8(&t, 0x7f); mk_u8(&t, 0x7f); mk_zero(&t, 8);   /* tylko etykieta, rozmiar ogromny */
            corpus_add(c, "syn_id3_only.mp3", "mp3-minimp3", t.b, t.n);
            memset(&t, 0, sizeof t);                                                                                          /* "free format": bitrate 0 */
            for (k = 0; k < 6; k++) { mk_u8(&t, 0xFF); mk_u8(&t, 0xFB); mk_u8(&t, 0x00); mk_u8(&t, 0x00); mk_zero(&t, 300); }
            corpus_add(c, "syn_freeformat.mp3", NULL, t.b, t.n);
        }
    }

    /* MP4: sam pakiet ftyp + mdat (probe po etykiecie) */
    memset(&m, 0, sizeof m);
    mk_be32(&m, 20); mk_str(&m, "ftyp"); mk_str(&m, "M4A "); mk_be32(&m, 0); mk_str(&m, "M4A ");
    mk_be32(&m, 16); mk_str(&m, "mdat"); mk_zero(&m, 8);
    corpus_add(c, "syn.m4a", "aac-native-mp4", m.b, m.n);
    memset(&m, 0, sizeof m);
    mk_be32(&m, 1); mk_str(&m, "ftyp"); mk_be32(&m, 0); mk_be32(&m, 0xFFFFFFFFu);                                            /* rozszerzony rozmiar 64-bit, ogromny */
    corpus_add(c, "syn_bigbox.m4a", "aac-native-mp4", m.b, m.n);

    /* LOAS/LATM: synchronizacja 0x2B7 + dlugosc */
    memset(&m, 0, sizeof m);
    mk_u8(&m, 0x56); mk_u8(&m, 0xE0); mk_u8(&m, 4); mk_zero(&m, 4);
    corpus_add(c, "syn.loas", "aac-native-latm", m.b, m.n);

    /* ADIF: etykieta + zera */
    memset(&m, 0, sizeof m);
    mk_str(&m, "ADIF"); mk_zero(&m, 40);
    corpus_add(c, "syn.adif", "aac-native-adif", m.b, m.n);

    /* ADTS: pojedyncza ramka (7 B naglowka + zera), LC mono 44,1 kHz i LC stereo, z/bez CRC, kanaly 0, 3..7 */
    {
        unsigned cfg;
        for (cfg = 0; cfg <= 7; cfg++) {
            uint8_t h[7 + 20]; size_t len = sizeof h;
            memset(h, 0, sizeof h);
            h[0] = 0xFF; h[1] = 0xF1; h[2] = (uint8_t)((1u << 6) | (4u << 2) | (cfg >> 2));
            h[3] = (uint8_t)(((cfg & 3u) << 6) | ((len >> 11) & 3u)); h[4] = (uint8_t)((len >> 3) & 255u); h[5] = (uint8_t)(((len & 7u) << 5) | 0x1F); h[6] = 0xFC;
            corpus_add(c, "syn.adts", NULL, h, len);       /* wlasciciel zalezy od kanalow: sprawdzany osobno (NULL = nie wymagany) */
        }
    }

    /* "obce" dane */
    { static const uint8_t avi[] = "RIFF\x24\0\0\0" "AVI LIST\x10\0\0\0hdrlavih\0\0\0\0"; corpus_add(c, "foreign.avi", NULL, avi, sizeof avi - 1); }
    { static const uint8_t png[] = "\x89PNG\r\n\x1a\n\0\0\0\rIHDR\0\0\0\x01\0\0\0\x01\x08\x02\0\0\0"; corpus_add(c, "foreign.png", NULL, png, sizeof png - 1); }
    { static const uint8_t gz[] = "\x1f\x8b\x08\0\0\0\0\0\0\x03\x03\0\0\0\0\0\0\0\0\0"; corpus_add(c, "foreign.gz", NULL, gz, sizeof gz - 1); }
    { static const uint8_t txt[] = "Ala ma kota, a kot ma Ale. To nie jest plik audio.\n"; corpus_add(c, "foreign.txt", NULL, txt, sizeof txt - 1); }
    { static const uint8_t opus[] = "OggS\0\x02\0\0\0\0\0\0\0\0\x01\0\0\0\0\0\0\0\0\0\0\0\x01\x13OpusHead\x01\x02\x38\x01\x80\xbb\0\0\0\0\0"; corpus_add(c, "foreign.opus", NULL, opus, sizeof opus - 1); }
    { static const uint8_t wv[] = "wvpk\x20\0\0\0\x10\x04\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"; corpus_add(c, "foreign.wv", NULL, wv, sizeof wv - 1); }
    { uint8_t z[300]; memset(z, 0, sizeof z); corpus_add(c, "foreign.zeros", NULL, z, sizeof z); memset(z, 0xFF, sizeof z); corpus_add(c, "foreign.ff", NULL, z, sizeof z); }
    { static const uint8_t one[] = { 0xFF }; corpus_add(c, "one.byte", NULL, one, 1); }
    { static const uint8_t few[] = { 'R', 'I', 'F' }; corpus_add(c, "few.bytes", NULL, few, sizeof few); }
}

/* wlasciciel (kodek, ktory MA rozpoznac plik) wg nazwy fixtury; NULL = nie wymagany */
static inline const char *corpus_owner_for(const char *path) {
    static const struct { const char *suffix, *owner; } map[] = {
        { "lc.aac", "aac-lc-native-adts" }, { "m51.aac", "aac-lc-native-multi-adts" }, { "he1.m4a", "aac-native-mp4" }, { "he1.loas", "aac-native-latm" }, { "he1.adif", "aac-native-adif" },
        { "probe_real.flac", "flac-drflac" }, { "probe_real.ogg", "ogg-vorbis-stb" }, { "probe_real.mp3", "mp3-minimp3" }, { "probe_real.wav", "wav-pcm" },
        { "probe_real.aiff", "aiff-pcm" }, { "probe_real.voc", "creative-voc-pcm" }
    };
    size_t i, n = strlen(path);
    for (i = 0; i < sizeof map / sizeof *map; i++) { size_t l = strlen(map[i].suffix); if (n >= l && !strcmp(path + n - l, map[i].suffix)) return map[i].owner; }
    return NULL;
}

static inline int corpus_add_file(corpus_t *c, const char *path, const char *owner) {
    FILE *f = fopen(path, "rb");
    long sz; uint8_t *b;
    if (!f) return 0;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return 0; }
    b = (uint8_t *)malloc(sz ? (size_t)sz : 1);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(b); return 0; }
    fclose(f);
    {
        const char *slash = strrchr(path, '/');
        char *dup = (char *)malloc(strlen(slash ? slash + 1 : path) + 1);
        if (!dup) abort();
        strcpy(dup, slash ? slash + 1 : path);
        corpus_add(c, dup, owner ? owner : corpus_owner_for(path), b, (size_t)sz);
        c->items[c->count - 1].owned_name = dup;
    }
    free(b);
    return 1;
}

#endif /* MKS_PROBE_CORPUS_H */
