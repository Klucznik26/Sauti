/* Syntetyczny modul M.K. Budowany w tescie, bez plikow z zewnatrz.
 * Oczekiwane PCM wynika z zegara PAL 3546895 i z zapisanego okresu, nie z innego odtwarzacza. */
#include "sauti_decoder_mod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_failures++; } } while (0)

typedef struct { size_t live, peak; int bad; } Heap;
typedef struct { size_t n; } Hdr;
static void *MAKTABASAUTI_CALL h_alloc(void *u, size_t n) {
    Heap *m = (Heap *)u; Hdr *h = (Hdr *)malloc(sizeof *h + n);
    if (!h) return NULL;
    h->n = n; m->live += n; if (m->live > m->peak) m->peak = m->live;
    return h + 1;
}
static void *MAKTABASAUTI_CALL h_realloc(void *u, void *p, size_t n) {
    (void)u; (void)p; (void)n; return NULL;
}
static void MAKTABASAUTI_CALL h_free(void *u, void *p) {
    Heap *m = (Heap *)u; Hdr *h;
    if (!p) { m->bad++; return; }
    h = (Hdr *)p - 1; m->live -= h->n; free(h);
}

typedef struct { const uint8_t *data; size_t n; const char *rate; const char *mod_rate; } Mem;
static maktabasauti_result_t MAKTABASAUTI_CALL mem_map(void *u, maktabasauti_reader_t *rd, const uint8_t **data, size_t *size) {
    Mem *m = (Mem *)u; (void)rd; *data = m->data; *size = m->n; return MAKTABASAUTI_OK;
}
static const char *MAKTABASAUTI_CALL mem_option(void *u, const char *key) {
    Mem *m = (Mem *)u;
    if (strcmp(key, "sample_rate") == 0) return m->rate;
    if (strcmp(key, "mod.sample_rate") == 0) return m->mod_rate;
    return NULL;
}
static int64_t MAKTABASAUTI_CALL mem_unused(void *u, maktabasauti_reader_t *rd, void *b, size_t n) {
    (void)u; (void)rd; (void)b; (void)n; return MAKTABASAUTI_E_IO;
}
static int64_t MAKTABASAUTI_CALL mem_seek(void *u, maktabasauti_reader_t *rd, int64_t o, int w) {
    (void)u; (void)rd; (void)o; (void)w; return MAKTABASAUTI_E_NOT_SEEKABLE;
}
static int64_t MAKTABASAUTI_CALL mem_tell(void *u, maktabasauti_reader_t *rd) { (void)u; (void)rd; return 0; }
static int64_t MAKTABASAUTI_CALL mem_size(void *u, maktabasauti_reader_t *rd) { (void)u; (void)rd; return -1; }
static void MAKTABASAUTI_CALL mem_log(void *u, int level, const char *msg) { (void)u; (void)level; (void)msg; }

/* Host ma jedno pole user.  Pakiet trzyma i sterte, i bufor. */
typedef struct { Heap heap; Mem mem; } Bag;
static void *MAKTABASAUTI_CALL b_alloc(void *u, size_t n) { return h_alloc(&((Bag *)u)->heap, n); }
static void MAKTABASAUTI_CALL b_free(void *u, void *p) { h_free(&((Bag *)u)->heap, p); }
static maktabasauti_result_t MAKTABASAUTI_CALL b_map(void *u, maktabasauti_reader_t *rd, const uint8_t **data, size_t *size) {
    return mem_map(&((Bag *)u)->mem, rd, data, size);
}
static const char *MAKTABASAUTI_CALL b_option(void *u, const char *key) { return mem_option(&((Bag *)u)->mem, key); }

typedef struct {
    Bag bag;
    maktabasauti_host_t host;
    maktabasauti_stream_info_t info;
    void *state;
    maktabasauti_result_t rc;
} Dec;

static void dec_open(Dec *d, const uint8_t *data, size_t n, const char *rate, const char *mod_rate) {
    memset(d, 0, sizeof *d);
    d->bag.mem.data = data; d->bag.mem.n = n; d->bag.mem.rate = rate; d->bag.mem.mod_rate = mod_rate;
    d->host.struct_size = sizeof d->host;
    d->host.user = &d->bag;
    d->host.alloc = b_alloc; d->host.realloc = h_realloc; d->host.free = b_free;
    d->host.read = mem_unused; d->host.peek = mem_unused; d->host.seek = mem_seek;
    d->host.tell = mem_tell; d->host.size = mem_size; d->host.map = b_map;
    d->host.option = b_option; d->host.log = mem_log;
    d->rc = maktabasauti_mod_desc.open(&maktabasauti_mod_desc, &d->host, NULL, &d->info, &d->state);
}

static void dec_close(Dec *d) {
    if (d->state) maktabasauti_mod_desc.close(d->state);
    d->state = NULL;
}

enum { RATE = 44100, TICK = 882, PAULA_PAL = 3546895u }; /* 44100 * 5 / (125 * 2) = 882 */

typedef struct { unsigned index, words, finetune, volume, loop_w, loop_l; const uint8_t *data; } Inst;
typedef struct { unsigned pat, row, ch, sample, period, effect, param; } Cell;

static void put_cell(uint8_t *p, unsigned sample, unsigned period, unsigned effect, unsigned param) {
    p[0] = (uint8_t)((sample & 0xF0u) | ((period >> 8) & 0x0Fu));
    p[1] = (uint8_t)period;
    p[2] = (uint8_t)(((sample & 0x0Fu) << 4) | (effect & 0x0Fu));
    p[3] = (uint8_t)param;
}

static unsigned layout_channels(const char *m) {
    unsigned ch;
    if (!m) return 4u;
    if (memcmp(m, "FLT8", 4) == 0 || memcmp(m, "CD81", 4) == 0 || memcmp(m, "OCTA", 4) == 0 || memcmp(m, "OKTA", 4) == 0) return 8u;
    if (m[0] == 'T' && m[1] == 'D' && m[2] == 'Z' && m[3] >= '1' && m[3] <= '3') return (unsigned)(m[3] - '0');
    if (m[0] >= '1' && m[0] <= '9' && m[1] == 'C' && m[2] == 'H' && m[3] == 'N') return (unsigned)(m[0] - '0');
    if (m[0] >= '0' && m[0] <= '9' && m[1] >= '0' && m[1] <= '9' && m[2] == 'C' && (m[3] == 'H' || m[3] == 'N')) {
        ch = (unsigned)(m[0] - '0') * 10u + (unsigned)(m[1] - '0');
        if (ch >= 10u && ch <= 32u) return ch;
    }
    return 4u;
}

static uint8_t *build(size_t *out_n, const char *magic, unsigned song_len, unsigned restart,
                      const uint8_t orders[128], const Inst *insts, size_t ninst, const Cell *cells, size_t ncells) {
    uint8_t ord[128];
    unsigned maxp = 0, pc, stored, span, i;
    int flt8;
    size_t samples = 0, n, off;
    uint8_t *b;
    memset(ord, 0, sizeof ord);
    if (orders) memcpy(ord, orders, 128u);
    for (i = 0; i < 128u; i++) if (ord[i] > maxp) maxp = ord[i];
    pc = maxp + 1u;
    flt8 = magic && memcmp(magic, "FLT8", 4) == 0;
    /* FLT8 trzyma pary patternow 4-kanalowych: pozycja P potrzebuje tez P+1. */
    stored = flt8 ? pc + 1u : pc;
    span = flt8 ? 4u : layout_channels(magic);
    for (i = 0; i < ninst; i++) samples += (size_t)insts[i].words * 2u;
    n = 1084u + (size_t)stored * 64u * span * 4u + samples;
    b = (uint8_t *)calloc(1, n);
    if (!b) return NULL;
    memcpy(b, "SAUTI-MOD-TEST", 14u);
    b[950] = (uint8_t)song_len;
    b[951] = (uint8_t)restart;
    memcpy(b + 952, ord, 128u);
    memcpy(b + 1080, magic, 4u);
    for (i = 0; i < ninst; i++) {
        uint8_t *ip = b + 20u + (insts[i].index - 1u) * 30u;
        unsigned w = insts[i].words;
        ip[22] = (uint8_t)(w >> 8); ip[23] = (uint8_t)w;
        ip[24] = (uint8_t)insts[i].finetune;
        ip[25] = (uint8_t)insts[i].volume;
        ip[26] = (uint8_t)(insts[i].loop_w >> 8); ip[27] = (uint8_t)insts[i].loop_w;
        ip[28] = (uint8_t)(insts[i].loop_l >> 8); ip[29] = (uint8_t)insts[i].loop_l;
    }
    for (i = 0; i < ncells; i++) {
        unsigned pat = cells[i].pat, ch = cells[i].ch;
        uint8_t *cell;
        if (flt8) {
            pat += ch >> 2;
            ch &= 3u;
        }
        cell = b + 1084u + ((size_t)pat * 64u + cells[i].row) * span * 4u + (size_t)ch * 4u;
        put_cell(cell, cells[i].sample, cells[i].period, cells[i].effect, cells[i].param);
    }
    off = 1084u + (size_t)stored * 64u * span * 4u;
    for (i = 0; i < ninst; i++) {
        size_t nb = (size_t)insts[i].words * 2u;
        if (nb && insts[i].data) memcpy(b + off, insts[i].data, nb);
        off += nb;
    }
    *out_n = n;
    return b;
}

static uint64_t timed_song(unsigned rows, unsigned speed0, unsigned speed_at_row0, unsigned tempo0, unsigned tempo_after_tick0) {
    unsigned row, speed = speed0, tempo = tempo0, rem = 0;
    uint64_t sum = 0;
    for (row = 0; row < rows; row++) {
        unsigned tick, sp;
        if (row == 0 && speed_at_row0) speed = speed_at_row0;
        sp = speed;
        for (tick = 0; tick < sp; tick++) {
            unsigned den = tempo * 2u;
            unsigned acc = rem + RATE * 5u;
            sum += acc / den;
            rem = acc % den;
            if (row == 0 && tick == 0 && tempo_after_tick0) tempo = tempo_after_tick0;
        }
    }
    return sum;
}

static int32_t spec_voice(uint64_t i, unsigned period, const uint8_t *sample, unsigned length,
                          unsigned loop_start, unsigned loop_len, unsigned volume) {
    uint64_t den = (uint64_t)period * RATE;
    uint64_t pos = (i * PAULA_PAL) / den;
    unsigned idx;
    int32_t s;
    if (!period || !sample || !length) return 0;
    if (loop_len) {
        uint64_t end = (uint64_t)loop_start + loop_len;
        if (pos >= end) pos = (uint64_t)loop_start + (pos - loop_start) % loop_len;
    } else if (pos >= length) {
        return 0;
    }
    idx = (unsigned)pos;
    s = sample[idx] < 128u ? (int32_t)sample[idx] : (int32_t)sample[idx] - 256;
    return s * (int32_t)volume;
}

static void expect_channel(const uint8_t *mod, size_t n, unsigned ch, unsigned period,
                           const uint8_t *sample, unsigned length, unsigned loop_start, unsigned loop_len,
                           unsigned volume, size_t frames) {
    Dec d;
    int16_t *pcm;
    size_t got = 0, i;
    int right = (ch & 3u) == 1u || (ch & 3u) == 2u;
    dec_open(&d, mod, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    if (d.rc != MAKTABASAUTI_OK) return;
    pcm = (int16_t *)malloc(frames * 2u * sizeof *pcm);
    CHECK(pcm != NULL);
    CHECK(maktabasauti_mod_desc.read(d.state, pcm, frames, &got) == MAKTABASAUTI_OK);
    CHECK(got == frames);
    for (i = 0; i < frames && pcm; i++) {
        int32_t s = spec_voice(i, period, sample, length, loop_start, loop_len, volume);
        int16_t L = right ? 0 : (int16_t)s;
        int16_t R = right ? (int16_t)s : 0;
        if (pcm[i * 2u] != L || pcm[i * 2u + 1u] != R) {
            fprintf(stderr, "FAIL ramka %zu: L %d R %d, oczekiwane %d %d\n", i, pcm[i * 2u], pcm[i * 2u + 1u], L, R);
            g_failures++;
            break;
        }
    }
    free(pcm);
    dec_close(&d);
    CHECK(d.bag.heap.live == 0);
    CHECK(d.bag.heap.bad == 0);
}

static void test_probe(void) {
    uint8_t hdr[1084];
    memset(hdr, 0, sizeof hdr);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, 1083u, NULL) == 0);
    memcpy(hdr + 1080, "M.K.", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, 1084u, NULL) == 70);
    memcpy(hdr + 1080, "8CHN", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, 1084u, ".mod") == 70);
    memcpy(hdr + 1080, "6CHN", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "M&K!", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "FLT8", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "10CH", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "12CN", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "32CH", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "TDZ3", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "CD81", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "OCTA", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "OKTA", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "1CHN", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "XXXX", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 0);
    memcpy(hdr + 1080, "33CH", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 0);
    memcpy(hdr + 1080, "FLT6", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 0);
    memcpy(hdr + 1080, "TDZ4", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 0);
    memset(hdr, 0, sizeof hdr);
    hdr[470] = 1;
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, 600u, NULL) == 60);
    hdr[470] = 0;
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 0);
}

static void test_reject(void) {
    uint8_t ones[8] = {127, 127, 127, 127, 127, 127, 127, 127};
    Inst inst = {1, 2, 0, 64, 0, 1, ones};
    Cell cell = {0, 0, 0, 1, 428, 0, 0};
    uint8_t orders[128];
    uint8_t *b;
    size_t n = 0;
    Dec d;
    memset(orders, 0, sizeof orders);
    b = build(&n, "XXXX", 1, 0, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_E_UNSUPPORTED_FORMAT);
    dec_close(&d); free(b);
    b = build(&n, "FLT6", 1, 0, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_E_UNSUPPORTED_FORMAT);
    dec_close(&d); free(b);
    b = build(&n, "33CH", 1, 0, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_E_UNSUPPORTED_FORMAT);
    dec_close(&d); free(b);
    b = build(&n, "TDZ4", 1, 0, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_E_UNSUPPORTED_FORMAT);
    dec_close(&d); free(b);
    b = build(&n, "M.K.", 0, 0, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_E_CORRUPT_DATA);
    dec_close(&d); free(b);
    b = build(&n, "M.K.", 200, 0, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_E_CORRUPT_DATA);
    dec_close(&d); free(b);
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, 1084u + 100u, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_E_TRUNCATED);
    dec_close(&d);
    dec_open(&d, b, n - 1u, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_E_TRUNCATED);
    dec_close(&d); free(b);
    inst.loop_l = 4; /* 8 bajtow petli przy probce 4 bajty */
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_E_CORRUPT_DATA);
    dec_close(&d); free(b);
    inst.loop_l = 1; inst.words = 2;
    cell.sample = 32;
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_E_CORRUPT_DATA);
    dec_close(&d); free(b);
    orders[0] = 64;
    cell.sample = 1;
    b = build(&n, "M.K.", 1, 0, orders, &inst, 1, &cell, 1);
    dec_open(&d, b, 1084u, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_E_UNSUPPORTED_FEATURE);
    dec_close(&d); free(b);
    orders[0] = 0; orders[5] = 1;
    b = build(&n, "M.K.", 1, 0, orders, &inst, 1, &cell, 1);
    dec_open(&d, b, 1084u + 1024u + 4u, NULL, NULL); /* jeden pattern i probka, a order wymaga dwoch */
    CHECK(d.rc == MAKTABASAUTI_E_TRUNCATED);
    dec_close(&d);
    dec_open(&d, b, 100u, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_E_UNSUPPORTED_FORMAT);
    dec_close(&d); free(b);
    CHECK(d.bag.heap.live == 0);
}

static void test_play(void) {
    uint8_t square[4] = {127, 0x80, 127, 0x80};
    uint8_t flat[4] = {127, 127, 127, 127};
    uint8_t lead[8] = {10, 10, 10, 10, 127, 127, 127, 127};
    Inst inst;
    Cell cells[2];
    uint8_t *b;
    size_t n = 0;
    Dec d;
    uint64_t plain;
    char tag[32];
    size_t need = 0;
    unsigned ch;

    memset(&inst, 0, sizeof inst);
    /* Petla 2 slow: dlugosc 1 slowa w naglowku oznacza brak petli. */
    inst.index = 1; inst.words = 2; inst.volume = 64; inst.loop_l = 2; inst.data = square; inst.finetune = 0x0F;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0, 0};
    b = build(&n, "M.K.", 1, 3, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.sample_rate == 44100u);
    CHECK(d.info.channels == 2u);
    CHECK(d.info.native_format == MAKTABASAUTI_FORMAT_S16);
    plain = (uint64_t)64u * 6u * TICK;
    CHECK(d.info.total_frames == plain);
    CHECK(maktabasauti_mod_desc.get_tag(d.state, "title", tag, sizeof tag, &need) == MAKTABASAUTI_OK);
    CHECK(strcmp(tag, "SAUTI-MOD-TEST") == 0);
    CHECK(maktabasauti_mod_desc.get_tag(d.state, "magic", tag, sizeof tag, NULL) == MAKTABASAUTI_OK);
    CHECK(strcmp(tag, "M.K.") == 0);
    CHECK(maktabasauti_mod_desc.get_tag(d.state, "restart", tag, sizeof tag, NULL) == MAKTABASAUTI_OK);
    CHECK(strcmp(tag, "3") == 0);
    CHECK(maktabasauti_mod_desc.get_tag(d.state, "finetune.1", tag, sizeof tag, NULL) == MAKTABASAUTI_OK);
    CHECK(strcmp(tag, "-1") == 0);
    CHECK(maktabasauti_mod_desc.get_tag(d.state, "title", tag, 4u, &need) == MAKTABASAUTI_E_BUFFER_TOO_SMALL);
    CHECK(need == 15u);
    CHECK(maktabasauti_mod_desc.get_tag(d.state, "artist", tag, sizeof tag, NULL) == MAKTABASAUTI_E_UNSUPPORTED_FEATURE);
    dec_close(&d);
    expect_channel(b, n, 0, 428, square, 4, 0, 4, 64, 4000);
    free(b);

    /* Finetune +7 nie stroi okresu.  E11 nie jest jeszcze efektem. */
    inst.finetune = 0x07; inst.loop_w = 0; inst.loop_l = 2; inst.words = 2;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0x11};
    b = build(&n, "FLT4", 1, 0, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(maktabasauti_mod_desc.get_tag(d.state, "finetune.1", tag, sizeof tag, NULL) == MAKTABASAUTI_OK);
    CHECK(strcmp(tag, "7") == 0);
    CHECK(strcmp(d.info.codec_profile, "FLT4") == 0);
    dec_close(&d);
    expect_channel(b, n, 0, 428, square, 4, 0, 4, 64, 8000);
    free(b);

    inst.finetune = 0x08;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x07, 0x37}; /* tremolo jeszcze nie dziala */
    b = build(&n, "4CHN", 1, 0, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(maktabasauti_mod_desc.get_tag(d.state, "finetune.1", tag, sizeof tag, NULL) == MAKTABASAUTI_OK);
    CHECK(strcmp(tag, "-8") == 0);
    dec_close(&d);
    expect_channel(b, n, 0, 428, square, 4, 0, 4, 64, 2000);
    free(b);

    for (ch = 0; ch < 4u; ch++) {
        cells[0] = (Cell){0, 0, ch, 1, 428, 0, 0};
        inst.finetune = 0; inst.loop_l = 2; inst.words = 2; inst.data = square; inst.volume = 64;
        b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
        expect_channel(b, n, ch, 428, square, 4, 0, 4, 64, 500);
        free(b);
    }

    /* Stala probka, Cxx obcina 0x7F do 64. */
    inst.words = 2; inst.volume = 64; inst.loop_w = 0; inst.loop_l = 2; inst.data = flat; inst.finetune = 0;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0C, 0x7F};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    expect_channel(b, n, 0, 428, flat, 4, 0, 4, 64, 300);
    free(b);
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0C, 0x20};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    expect_channel(b, n, 0, 428, flat, 4, 0, 4, 32, 300);
    free(b);

    /* Glosnosc instrumentu > 64 jest obcieta. */
    inst.volume = 80; inst.loop_l = 2;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0, 0};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    expect_channel(b, n, 0, 428, flat, 4, 0, 4, 64, 200);
    free(b);

    /* Lead-in gra raz, potem petla czterech probek 127. */
    inst.words = 4; inst.volume = 64; inst.loop_w = 2; inst.loop_l = 2; inst.data = lead;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0, 0};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    expect_channel(b, n, 0, 428, lead, 8, 4, 4, 64, 5000);
    free(b);

    /* Jednorazowa probka cichnie po czterech bajtach. */
    inst.words = 2; inst.loop_w = 0; inst.loop_l = 1; inst.data = flat;
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    expect_channel(b, n, 0, 428, flat, 4, 0, 0, 64, 80);
    free(b);
}

static void test_timing(void) {
    uint8_t flat[4] = {127, 127, 127, 127};
    Inst inst = {1, 2, 0, 64, 0, 2, flat};
    Cell cells[2];
    uint8_t *b;
    size_t n = 0, got = 0, i;
    Dec d;
    int16_t *pcm;
    uint64_t loud;

    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0F, 0x00};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == (uint64_t)64u * 6u * TICK);
    dec_close(&d); free(b);

    cells[0].param = 0x01;
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.info.total_frames == (uint64_t)64u * 1u * TICK);
    dec_close(&d); free(b);

    cells[0].param = 0x03;
    cells[1] = (Cell){0, 1, 0, 0, 0, 0x0C, 0x00};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == (uint64_t)64u * 3u * TICK);
    loud = 3ull * TICK;
    pcm = (int16_t *)malloc((size_t)(loud + 10u) * 2u * sizeof *pcm);
    CHECK(maktabasauti_mod_desc.read(d.state, pcm, (size_t)loud + 10u, &got) == MAKTABASAUTI_OK);
    CHECK(got == loud + 10u);
    CHECK(pcm[0] == 127 * 64);
    CHECK(pcm[(size_t)(loud - 1u) * 2u] == 127 * 64);
    CHECK(pcm[(size_t)loud * 2u] == 0);
    free(pcm);
    dec_close(&d); free(b);

    cells[0].param = 0x1F;
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.info.total_frames == (uint64_t)64u * 31u * TICK);
    dec_close(&d); free(b);

    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0B, 0x00}; /* B00 nie zapetla */
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == (uint64_t)64u * 6u * TICK);
    dec_close(&d); free(b);

    cells[0].effect = 0x0D; cells[0].param = 0x00;
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == (uint64_t)64u * 6u * TICK);
    dec_close(&d); free(b);

    /* Tempo 250 od nastepnego taktu.  Wiersz 1 gasi glosnosc. */
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0F, 0xFA};
    cells[1] = (Cell){0, 1, 0, 0, 0, 0x0C, 0x00};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == timed_song(64, 6, 0, 125, 250));
    loud = TICK + 5ull * 441u;
    pcm = (int16_t *)malloc((size_t)(loud + 4u) * 2u * sizeof *pcm);
    CHECK(maktabasauti_mod_desc.read(d.state, pcm, (size_t)loud + 4u, &got) == MAKTABASAUTI_OK);
    for (i = 0; i < (size_t)loud; i++) CHECK(pcm[i * 2u] == 127 * 64);
    CHECK(pcm[(size_t)loud * 2u] == 0);
    free(pcm);
    dec_close(&d); free(b);

    cells[0].param = 0x20; /* tempo 32, nie predkosc */
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == timed_song(64, 6, 0, 125, 32));
    dec_close(&d); free(b);

    /* Nizsza czestotliwosc z opcji mod.sample_rate. */
    cells[0] = (Cell){0, 0, 0, 1, 428, 0, 0};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, NULL, "22050");
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.sample_rate == 22050u);
    CHECK(d.info.total_frames == (uint64_t)64u * 6u * 441u);
    dec_close(&d);
    dec_open(&d, b, n, "0", NULL);
    CHECK(d.rc == MAKTABASAUTI_E_INVALID_ARGUMENT);
    dec_close(&d);
    dec_open(&d, b, n, "384001", NULL);
    CHECK(d.rc == MAKTABASAUTI_E_INVALID_ARGUMENT);
    dec_close(&d);
    dec_open(&d, b, n, "44k", NULL);
    CHECK(d.rc == MAKTABASAUTI_E_INVALID_ARGUMENT);
    dec_close(&d);
    free(b);
    CHECK(d.bag.heap.live == 0);
}

static void test_seek_and_sum(void) {
    uint8_t flat[4] = {127, 127, 127, 127};
    uint8_t neg[4] = {0x80, 0x80, 0x80, 0x80};
    Inst insts[2];
    Cell cells[2];
    uint8_t orders[128];
    uint8_t *b;
    size_t n = 0, got = 0;
    Dec a, c;
    int16_t x[80], y[64];
    uint64_t actual = 0;
    size_t i;

    memset(insts, 0, sizeof insts);
    insts[0] = (Inst){1, 2, 0, 64, 0, 2, flat};
    insts[1] = (Inst){2, 2, 0, 64, 0, 2, neg};
    cells[0] = (Cell){0, 0, 0, 1, 428, 0, 0};
    cells[1] = (Cell){0, 0, 3, 2, 428, 0, 0};
    b = build(&n, "M.K.", 1, 0, NULL, insts, 2, cells, 2);
    dec_open(&a, b, n, "44100", NULL);
    CHECK(a.rc == MAKTABASAUTI_OK);
    CHECK(maktabasauti_mod_desc.read(a.state, x, 32, &got) == MAKTABASAUTI_OK);
    /* 127*64 + (-128)*64 = -64 na lewym; prawy milczy. */
    for (i = 0; i < 32u; i++) {
        CHECK(x[i * 2u] == -64);
        CHECK(x[i * 2u + 1u] == 0);
    }
    CHECK(maktabasauti_mod_desc.seek(a.state, a.info.total_frames + 1u, 0, &actual) == MAKTABASAUTI_E_OUT_OF_RANGE);
    CHECK(maktabasauti_mod_desc.read(a.state, y, 8, &got) == MAKTABASAUTI_OK);
    dec_open(&c, b, n, "44100", NULL);
    CHECK(maktabasauti_mod_desc.read(c.state, x, 40, &got) == MAKTABASAUTI_OK);
    CHECK(memcmp(y, x + 32u * 2u, 8u * 2u * sizeof(int16_t)) == 0);
    CHECK(maktabasauti_mod_desc.seek(a.state, 0, 0, &actual) == MAKTABASAUTI_OK);
    CHECK(actual == 0u);
    CHECK(maktabasauti_mod_desc.read(a.state, y, 8, &got) == MAKTABASAUTI_OK);
    CHECK(memcmp(y, x, 8u * 2u * sizeof(int16_t)) == 0);
    CHECK(maktabasauti_mod_desc.reset(a.state) == MAKTABASAUTI_OK);
    CHECK(maktabasauti_mod_desc.read(a.state, y, 4, &got) == MAKTABASAUTI_OK);
    CHECK(y[0] == -64);
    CHECK(maktabasauti_mod_desc.seek(a.state, a.info.total_frames, 0, &actual) == MAKTABASAUTI_OK);
    CHECK(maktabasauti_mod_desc.read(a.state, y, 4, &got) == MAKTABASAUTI_EOF);
    CHECK(got == 0u);
    dec_close(&a); dec_close(&c); free(b);

    /* Nieuzywany wysoki order wydluza blok patternow, ale utwor gra jeden order. */
    memset(orders, 0, sizeof orders);
    orders[40] = 2;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0, 0};
    b = build(&n, "M!K!", 1, 0, orders, insts, 1, cells, 1);
    dec_open(&a, b, n, NULL, NULL);
    CHECK(a.rc == MAKTABASAUTI_OK);
    CHECK(a.info.total_frames == (uint64_t)64u * 6u * TICK);
    dec_close(&a); free(b);

    /* 65. pattern jest legalny dla M!K!, a dla M.K. nie. */
    memset(orders, 0, sizeof orders);
    orders[0] = 64;
    b = build(&n, "M!K!", 1, 0, orders, insts, 1, cells, 1);
    dec_open(&a, b, n, NULL, NULL);
    CHECK(a.rc == MAKTABASAUTI_OK);
    CHECK(a.info.total_frames == (uint64_t)64u * 6u * TICK);
    dec_close(&a);
    memcpy(b + 1080, "M.K.", 4u);
    dec_open(&a, b, n, NULL, NULL);
    CHECK(a.rc == MAKTABASAUTI_E_UNSUPPORTED_FEATURE);
    dec_close(&a);
    memcpy(b + 1080, "M&K!", 4u);
    dec_open(&a, b, n, NULL, NULL);
    CHECK(a.rc == MAKTABASAUTI_E_UNSUPPORTED_FEATURE);
    dec_close(&a); free(b);
    CHECK(a.bag.heap.live == 0);
}

static unsigned flips(const int16_t *pcm, size_t begin, size_t end) {
    unsigned n = 0;
    int prev = 0;
    size_t i;
    for (i = begin; i < end; i++) {
        int s = pcm[i * 2u] > 0 ? 1 : pcm[i * 2u] < 0 ? -1 : 0;
        if (s && prev && s != prev) n++;
        if (s) prev = s;
    }
    return n;
}

static void expect_period(const int16_t *pcm, size_t tick, unsigned period) {
    unsigned got, ratio;
    if (!pcm) { g_failures++; return; }
    got = flips(pcm, tick * TICK, (tick + 1u) * TICK);
    ratio = (unsigned)((882ull * 3546895ull) / ((unsigned long long)period * 44100ull));
    if (got + 2u < ratio || got > ratio + 2u) {
        fprintf(stderr, "FAIL takt %zu okres %u: zmian znaku %u, wzorzec %u\n", tick, period, got, ratio);
        g_failures++;
    }
}

static int16_t *render_n(Dec *d, const uint8_t *mod, size_t n, size_t frames) {
    int16_t *pcm = (int16_t *)calloc(frames * 2u, sizeof *pcm);
    size_t got = 0;
    dec_open(d, mod, n, "44100", NULL);
    CHECK(d->rc == MAKTABASAUTI_OK);
    if (d->rc != MAKTABASAUTI_OK || !pcm) return pcm;
    CHECK(maktabasauti_mod_desc.read(d->state, pcm, frames, &got) == MAKTABASAUTI_OK);
    CHECK(got == frames);
    return pcm;
}

static void test_effects(void) {
    uint8_t square[4] = {127, 0x80, 127, 0x80};
    uint8_t flat[4] = {127, 127, 127, 127};
    uint8_t lead[64];
    uint8_t *ramp;
    Inst inst;
    Cell cells[3];
    uint8_t *b;
    size_t n = 0;
    Dec d;
    int16_t *pcm;
    memset(lead, 127, sizeof lead);
    lead[0] = 0;
    memset(&inst, 0, sizeof inst);
    inst.index = 1; inst.words = 2; inst.volume = 64; inst.loop_l = 2; inst.data = square;

    cells[0] = (Cell){0, 0, 0, 1, 428, 0x00, 0x30};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, 3u * TICK);
    expect_period(pcm, 0, 428);
    expect_period(pcm, 1, 360); /* C-2 + 3 poltony = D#2 */
    expect_period(pcm, 2, 428);
    dec_close(&d); free(pcm); free(b);

    cells[0].param = 0xC0; /* +12 poltonow = oktawa, C-3 */
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, 2u * TICK);
    expect_period(pcm, 1, 214);
    dec_close(&d); free(pcm); free(b);

    cells[0] = (Cell){0, 0, 0, 1, 428, 0x01, 32};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, 2u * TICK);
    expect_period(pcm, 0, 428);
    expect_period(pcm, 1, 396);
    dec_close(&d); free(pcm); free(b);

    cells[0] = (Cell){0, 0, 0, 1, 120, 0x01, 255};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, 2u * TICK);
    expect_period(pcm, 1, 113);
    dec_close(&d); free(pcm); free(b);

    cells[0] = (Cell){0, 0, 0, 1, 428, 0x02, 40};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, 2u * TICK);
    expect_period(pcm, 1, 468);
    dec_close(&d); free(pcm); free(b);

    cells[0] = (Cell){0, 0, 0, 1, 850, 0x02, 255};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, 2u * TICK);
    expect_period(pcm, 1, 856);
    dec_close(&d); free(pcm); free(b);

    cells[0] = (Cell){0, 0, 0, 1, 428, 0x01, 10};
    cells[1] = (Cell){0, 1, 0, 0, 0, 0x01, 0}; /* powtorzony krok portamento */
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    pcm = render_n(&d, b, n, 8u * TICK);
    expect_period(pcm, 7, 368); /* 428 - 5*10 w wierszu 0, takt 0 wiersza 1 bez slizgu, takt 1 slizga o 10 */
    dec_close(&d); free(pcm); free(b);

    inst.words = 32; inst.loop_l = 32; inst.data = lead;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0, 0};
    cells[1] = (Cell){0, 1, 0, 0, 214, 0x03, 32};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    pcm = render_n(&d, b, n, 6u * TICK + 1u);
    CHECK(pcm[6u * TICK * 2u] == 127 * 64); /* faza nie wraca na bajt 0 */
    dec_close(&d); free(pcm); free(b);

    inst.words = 2; inst.loop_l = 2; inst.data = square; inst.volume = 64;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0, 0};
    cells[1] = (Cell){0, 1, 0, 0, 214, 0x03, 32};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    pcm = render_n(&d, b, n, 8u * TICK);
    expect_period(pcm, 6, 428);
    expect_period(pcm, 7, 396);
    dec_close(&d); free(pcm); free(b);

    inst.words = 2; inst.loop_l = 2; inst.data = square; inst.volume = 64;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x04, 0xF8};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, 3u * TICK);
    expect_period(pcm, 1, 428); /* sinus zaczyna sie od zera */
    expect_period(pcm, 2, 443); /* 254 * 8 / 128 = 15, okres 428 + 15 */
    dec_close(&d); free(pcm); free(b);

    inst.volume = 40; inst.data = flat;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0A, 0x18}; /* w gore o 1, nie w dol o 8 */
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, 2u * TICK);
    CHECK(pcm[0] == 127 * 40);
    CHECK(pcm[TICK * 2u] == 127 * 41);
    dec_close(&d); free(pcm); free(b);

    inst.volume = 60;
    cells[0].param = 0xF0;
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, 3u * TICK);
    CHECK(pcm[0] == 127 * 60);
    CHECK(pcm[TICK * 2u] == 127 * 64);
    CHECK(pcm[2u * TICK * 2u] == 127 * 64);
    dec_close(&d); free(pcm); free(b);

    inst.volume = 10;
    cells[0].param = 0x0F;
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, 2u * TICK);
    CHECK(pcm[0] == 127 * 10);
    CHECK(pcm[TICK * 2u] == 0);
    dec_close(&d); free(pcm); free(b);

    inst.volume = 32; inst.words = 2; inst.loop_l = 2; inst.data = square;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x03, 32};
    cells[1] = (Cell){0, 1, 0, 0, 300, 0x05, 0x04};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    pcm = render_n(&d, b, n, 8u * TICK);
    {
        int a0 = pcm[6u * TICK * 2u], a1 = pcm[7u * TICK * 2u];
        if (a0 < 0) a0 = -a0;
        if (a1 < 0) a1 = -a1;
        CHECK(a0 == 127 * 32 || a0 == 128 * 32);
        CHECK(a1 == 127 * 28 || a1 == 128 * 28);
    }
    expect_period(pcm, 7, 396);
    dec_close(&d); free(pcm); free(b);

    ramp = (uint8_t *)calloc(258, 1);
    ramp[256] = 127;
    inst.volume = 64; inst.words = 129; inst.loop_l = 1; inst.data = ramp;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x09, 1};
    cells[1] = (Cell){0, 1, 0, 1, 428, 0x09, 0};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    pcm = render_n(&d, b, n, 6u * TICK + 1u);
    CHECK(pcm[0] == 127 * 64);
    CHECK(pcm[40u * 2u] == 0);
    CHECK(pcm[6u * TICK * 2u] == 127 * 64);
    dec_close(&d); free(pcm); free(b);

    cells[0] = (Cell){0, 0, 0, 1, 4095, 0, 0};
    cells[1] = (Cell){0, 1, 0, 0, 0, 0x09, 1};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    pcm = render_n(&d, b, n, 6u * TICK + 1u);
    CHECK(pcm[6u * TICK * 2u] == 0); /* samo 9xx bez nuty nie przeskakuje */
    dec_close(&d); free(pcm); free(b); free(ramp);
    CHECK(d.bag.heap.live == 0);
}

static uint8_t *build15(size_t *out_n, unsigned song_len, const uint8_t orders[128],
                        const Inst *insts, size_t ninst, const Cell *cells, size_t ncells) {
    uint8_t ord[128];
    unsigned maxp = 0, pc, i;
    size_t samples = 0, n, off;
    uint8_t *b;
    memset(ord, 0, sizeof ord);
    if (orders) memcpy(ord, orders, 128u);
    for (i = 0; i < 128u; i++) if (ord[i] > maxp) maxp = ord[i];
    pc = maxp + 1u;
    for (i = 0; i < ninst; i++) samples += (size_t)insts[i].words * 2u;
    n = 600u + (size_t)pc * 1024u + samples;
    b = (uint8_t *)calloc(1, n);
    if (!b) return NULL;
    memcpy(b, "SAUTI-MOD-15", 12u);
    b[470] = (uint8_t)song_len;
    memcpy(b + 472, ord, 128u);
    for (i = 0; i < ninst; i++) {
        uint8_t *ip = b + 20u + (insts[i].index - 1u) * 30u;
        unsigned w = insts[i].words;
        ip[22] = (uint8_t)(w >> 8); ip[23] = (uint8_t)w;
        ip[24] = (uint8_t)insts[i].finetune;
        ip[25] = (uint8_t)insts[i].volume;
        ip[26] = (uint8_t)(insts[i].loop_w >> 8); ip[27] = (uint8_t)insts[i].loop_w;
        ip[28] = (uint8_t)(insts[i].loop_l >> 8); ip[29] = (uint8_t)insts[i].loop_l;
    }
    for (i = 0; i < ncells; i++) {
        uint8_t *cell = b + 600u + ((size_t)cells[i].pat * 64u + cells[i].row) * 16u + (size_t)cells[i].ch * 4u;
        put_cell(cell, cells[i].sample, cells[i].period, cells[i].effect, cells[i].param);
    }
    off = 600u + (size_t)pc * 1024u;
    for (i = 0; i < ninst; i++) {
        size_t nb = (size_t)insts[i].words * 2u;
        if (nb && insts[i].data) memcpy(b + off, insts[i].data, nb);
        off += nb;
    }
    *out_n = n;
    return b;
}

static void test_variants(void) {
    uint8_t flat[4] = {127, 127, 127, 127};
    Inst inst = {1, 2, 0, 64, 0, 2, flat};
    Cell cell = {0, 0, 0, 1, 428, 0, 0};
    uint8_t orders[128];
    uint8_t tiny[600];
    uint8_t *b;
    size_t n = 0;
    Dec d;
    char tag[8];
    memset(orders, 0, sizeof orders);

    cell.ch = 4;
    b = build(&n, "8CHN", 1, 0, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(strcmp(d.info.codec_profile, "8CHN") == 0);
    dec_close(&d);
    expect_channel(b, n, 4, 428, flat, 4, 0, 4, 64, 32);
    free(b);

    cell.ch = 5;
    b = build(&n, "6CHN", 1, 0, NULL, &inst, 1, &cell, 1);
    expect_channel(b, n, 5, 428, flat, 4, 0, 4, 64, 16);
    free(b);

    cell.ch = 0;
    b = build(&n, "1CHN", 1, 0, NULL, &inst, 1, &cell, 1);
    expect_channel(b, n, 0, 428, flat, 4, 0, 4, 64, 16);
    free(b);

    cell.ch = 8;
    b = build(&n, "10CH", 1, 0, NULL, &inst, 1, &cell, 1);
    expect_channel(b, n, 8, 428, flat, 4, 0, 4, 64, 8);
    free(b);

    cell.ch = 2;
    b = build(&n, "TDZ3", 1, 0, NULL, &inst, 1, &cell, 1);
    expect_channel(b, n, 2, 428, flat, 4, 0, 4, 64, 8);
    free(b);

    cell.ch = 11;
    b = build(&n, "12CN", 1, 0, NULL, &inst, 1, &cell, 1);
    expect_channel(b, n, 11, 428, flat, 4, 0, 4, 64, 8);
    free(b);

    cell.ch = 7;
    b = build(&n, "CD81", 1, 0, NULL, &inst, 1, &cell, 1);
    expect_channel(b, n, 7, 428, flat, 4, 0, 4, 64, 8);
    free(b);

    cell.ch = 4;
    b = build(&n, "FLT8", 1, 0, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(strcmp(d.info.codec_profile, "FLT8") == 0);
    dec_close(&d);
    expect_channel(b, n, 4, 428, flat, 4, 0, 4, 64, 16);
    free(b);

    orders[0] = 127;
    b = build(&n, "FLT8", 1, 0, orders, &inst, 1, &cell, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_E_UNSUPPORTED_FEATURE);
    dec_close(&d); free(b);

    memset(orders, 0, sizeof orders);
    orders[0] = 64;
    b = build(&n, "8CHN", 1, 0, orders, &inst, 1, NULL, 0);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == (uint64_t)64u * 6u * TICK);
    dec_close(&d); free(b);

    cell.ch = 0;
    b = build(&n, "M&K!", 1, 0, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(strcmp(d.info.codec_profile, "M&K!") == 0);
    dec_close(&d); free(b);

    b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(strcmp(d.info.codec_profile, "15ST") == 0);
    CHECK(maktabasauti_mod_desc.get_tag(d.state, "magic", tag, sizeof tag, NULL) == MAKTABASAUTI_OK);
    CHECK(strcmp(tag, "15ST") == 0);
    CHECK(maktabasauti_mod_desc.get_tag(d.state, "finetune.16", tag, sizeof tag, NULL) == MAKTABASAUTI_E_UNSUPPORTED_FEATURE);
    dec_close(&d);
    expect_channel(b, n, 0, 428, flat, 4, 0, 4, 64, 16);
    free(b);

    cell.sample = 16;
    b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_E_CORRUPT_DATA);
    dec_close(&d); free(b);

    memset(orders, 0, sizeof orders);
    orders[0] = 64;
    b = build15(&n, 1, orders, &inst, 1, NULL, 0);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_E_UNSUPPORTED_FEATURE);
    dec_close(&d); free(b);

    memset(tiny, 0, sizeof tiny);
    tiny[470] = 1;
    dec_open(&d, tiny, sizeof tiny, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_E_TRUNCATED);
    dec_close(&d);
    CHECK(d.bag.heap.live == 0);
}

int main(void) {
    test_probe();
    test_reject();
    test_play();
    test_timing();
    test_seek_and_sum();
    test_effects();
    test_variants();
    if (g_failures) {
        fprintf(stderr, "%d niepowodzen\n", g_failures);
        return 1;
    }
    printf("test_decoder_mod: ok\n");
    return 0;
}
