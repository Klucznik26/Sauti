/* Syntetyczny modul M.K. Budowany w tescie, bez plikow z zewnatrz.
 * Oczekiwane PCM wynika z zegara PAL 3546895 i z zapisanego okresu, nie z innego odtwarzacza. */
#include "sauti_decoder_mod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
static const maktabasauti_codec_t *g_codec;
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

typedef struct { const uint8_t *data; size_t n; const char *rate; const char *mod_rate; const char *clock; const char *model; const char *tracker; } Mem;
static maktabasauti_result_t MAKTABASAUTI_CALL mem_map(void *u, maktabasauti_reader_t *rd, const uint8_t **data, size_t *size) {
    Mem *m = (Mem *)u; (void)rd; *data = m->data; *size = m->n; return MAKTABASAUTI_OK;
}
static const char *MAKTABASAUTI_CALL mem_option(void *u, const char *key) {
    Mem *m = (Mem *)u;
    if (strcmp(key, "sample_rate") == 0) return m->rate;
    if (strcmp(key, "mod.sample_rate") == 0) return m->mod_rate;
    if (strcmp(key, "mod.clock") == 0) return m->clock;
    if (strcmp(key, "mod.model") == 0) return m->model;
    if (strcmp(key, "mod.tracker") == 0) return m->tracker;
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

static void dec_open_ex(Dec *d, const uint8_t *data, size_t n, const char *rate, const char *mod_rate,
                        const char *clock, const char *model, const char *tracker) {
    memset(d, 0, sizeof *d);
    d->bag.mem.data = data; d->bag.mem.n = n; d->bag.mem.rate = rate; d->bag.mem.mod_rate = mod_rate;
    d->bag.mem.clock = clock; d->bag.mem.model = model; d->bag.mem.tracker = tracker;
    d->host.struct_size = sizeof d->host;
    d->host.user = &d->bag;
    d->host.alloc = b_alloc; d->host.realloc = h_realloc; d->host.free = b_free;
    d->host.read = mem_unused; d->host.peek = mem_unused; d->host.seek = mem_seek;
    d->host.tell = mem_tell; d->host.size = mem_size; d->host.map = b_map;
    d->host.option = b_option; d->host.log = mem_log;
    {
        const maktabasauti_codec_t *codec = g_codec ? g_codec : &maktabasauti_mod_desc;
        d->rc = codec->open(codec, &d->host, NULL, &d->info, &d->state);
    }
}

static void dec_open(Dec *d, const uint8_t *data, size_t n, const char *rate, const char *mod_rate) {
    dec_open_ex(d, data, n, rate, mod_rate, NULL, NULL, NULL);
}

static void dec_close(Dec *d) {
    if (d->state) maktabasauti_mod_desc.close(d->state);
    d->state = NULL;
}

enum { RATE = 44100, TICK = 882, PAULA_PAL = 3546895u, CLEAR_AT = 11 };
/* 44100 * 5 / (125 * 2) = 882.  Przy okresie 428 klatki 0..10 czytaja wyzerowane bajty, od 11 bajt 2. */

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
    if (memcmp(m, "FLT8", 4) == 0 || memcmp(m, "EXO8", 4) == 0 || memcmp(m, "EX08", 4) == 0
        || memcmp(m, "CD81", 4) == 0 || memcmp(m, "OCTA", 4) == 0 || memcmp(m, "OKTA", 4) == 0) return 8u;
    if (memcmp(m, "CD61", 4) == 0) return 6u;
    if (memcmp(m, "FA04", 4) == 0 || memcmp(m, "FA06", 4) == 0 || memcmp(m, "FA08", 4) == 0)
        return (unsigned)(m[3] - '0');
    if (m[0] == 'T' && m[1] == 'D' && m[2] == 'Z' && m[3] >= '1' && m[3] <= '9') return (unsigned)(m[3] - '0');
    if (m[0] >= '1' && m[0] <= '9' && m[1] == 'C' && m[2] == 'H' && m[3] == 'N') return (unsigned)(m[0] - '0');
    if (m[0] >= '0' && m[0] <= '9' && m[1] >= '0' && m[1] <= '9' && m[2] == 'C' && (m[3] == 'H' || m[3] == 'N')) {
        ch = (unsigned)(m[0] - '0') * 10u + (unsigned)(m[1] - '0');
        if (ch >= 10u && ch <= 99u) return ch;
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
    flt8 = magic && (memcmp(magic, "FLT8", 4) == 0 || memcmp(magic, "EXO8", 4) == 0 || memcmp(magic, "EX08", 4) == 0);
    /* FLT8, EXO8 i EX08 trzymaja pary patternow 4-kanalowych: pozycja P potrzebuje tez P+1. */
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

/* Soundtracker 2.6 / Ice Tracker.  Naglowek 1468, magik na 1464, kazda sciezka ma 256 bajtow.
 * cells[i].pat to numer sciezki, nie patternu.  Kanal wybiera tabela, nie polozenie komorki.
 * table NULL zostawia indeksy zerowe. */
static uint8_t *build_st26(size_t *out_n, const char magic[4], unsigned song_len, unsigned ntracks,
                           const uint8_t *table, const Inst *insts, size_t ninst,
                           const Cell *cells, size_t ncells) {
    unsigned i, copy_n;
    size_t samples = 0, n, off;
    uint8_t *b;
    for (i = 0; i < ninst; i++) samples += (size_t)insts[i].words * 2u;
    n = 1468u + (size_t)ntracks * 256u + samples;
    b = (uint8_t *)calloc(1, n);
    if (!b) return NULL;
    memcpy(b, "SAUTI-MOD-TEST", 14u);
    b[950] = (uint8_t)song_len;
    b[951] = (uint8_t)ntracks;
    copy_n = song_len > 128u ? 128u : song_len;
    if (table && copy_n) memcpy(b + 952, table, (size_t)copy_n * 4u);
    if (magic) memcpy(b + 1464, magic, 4u);
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
        uint8_t *cell;
        if (cells[i].pat >= ntracks) continue;
        cell = b + 1468u + (size_t)cells[i].pat * 256u + (size_t)cells[i].row * 4u;
        put_cell(cell, cells[i].sample, cells[i].period, cells[i].effect, cells[i].param);
    }
    off = 1468u + (size_t)ntracks * 256u;
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
    /* Dwa pierwsze bajty sa zerem przy odczycie.  Plik w tescie zostaje. */
    if (idx < 2u) return 0;
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
    memcpy(hdr + 1080, "N.T.", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "EXO4", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "PATT", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "NSMS", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "LARD", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "EXO8", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "EX08", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "CD61", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "FA04", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "FA06", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "FA08", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "TDZ4", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "TDZ9", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "1CHN", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "XXXX", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 0);
    memcpy(hdr + 1080, "33CH", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "99CH", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 70);
    memcpy(hdr + 1080, "00CH", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 0);
    memcpy(hdr + 1080, "FLT6", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, NULL) == 0);
    memcpy(hdr + 1080, "M.K.", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, sizeof hdr, "a.WOW") == 70);
    CHECK(maktabasauti_mod_wow_desc.probe(&maktabasauti_mod_wow_desc, hdr, sizeof hdr, NULL) == 0);
    CHECK(maktabasauti_mod_wow_desc.probe(&maktabasauti_mod_wow_desc, hdr, sizeof hdr, "tune.mod") == 0);
    CHECK(maktabasauti_mod_wow_desc.probe(&maktabasauti_mod_wow_desc, hdr, sizeof hdr, "a.WOW") == 90);
    CHECK(maktabasauti_mod_wow_desc.probe(&maktabasauti_mod_wow_desc, hdr, sizeof hdr, "dir/tune.wow") == 90);
    CHECK(maktabasauti_mod_wow_desc.probe(&maktabasauti_mod_wow_desc, hdr, sizeof hdr, "C:\\songs\\tune.WOW") == 90);
    memcpy(hdr + 1080, "8CHN", 4u);
    CHECK(maktabasauti_mod_wow_desc.probe(&maktabasauti_mod_wow_desc, hdr, sizeof hdr, "a.wow") == 0);
    memset(hdr, 0, sizeof hdr);
    hdr[470] = 1;
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, 600u, NULL) == 60);
    hdr[45] = 64;
    hdr[472] = 63;
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, 600u, NULL) == 60);
    hdr[45] = 65;
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, 600u, NULL) == 0);
    hdr[45] = 0;
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, 600u, NULL) == 60);
    hdr[472] = 64;
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, 600u, NULL) == 0);
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
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(strcmp(d.info.codec_profile, "33CH") == 0);
    dec_close(&d); free(b);
    b = build(&n, "99CH", 1, 0, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(strcmp(d.info.codec_profile, "99CH") == 0);
    dec_close(&d); free(b);
    {
        uint8_t flat[4];
        uint8_t *extra;
        Inst winst;
        Cell wcell;
        memset(flat, 127, sizeof flat);
        winst = (Inst){1, 2, 0, 64, 0, 2, flat};
        wcell = (Cell){0, 0, 4, 1, 428, 0, 0};
        b = build(&n, "8CHN", 1, 0, NULL, &winst, 1, &wcell, 1);
        memcpy(b + 1080, "M.K.", 4u);
        dec_open(&d, b, n, NULL, NULL);
        CHECK(d.rc == MAKTABASAUTI_OK);
        CHECK(strcmp(d.info.codec_profile, "M.K.") == 0);
        dec_close(&d);
        extra = (uint8_t *)malloc(n + 1u);
        CHECK(extra != NULL);
        if (extra) {
            memcpy(extra, b, n);
            extra[n] = 0xABu;
            dec_open(&d, extra, n + 1u, NULL, NULL);
            CHECK(d.rc == MAKTABASAUTI_OK);
            CHECK(strcmp(d.info.codec_profile, "M.K.") == 0);
            dec_close(&d);
            free(extra);
        }
        g_codec = &maktabasauti_mod_wow_desc;
        dec_open(&d, b, n, NULL, NULL);
        CHECK(d.rc == MAKTABASAUTI_OK);
        CHECK(strcmp(d.info.codec_profile, "WOW") == 0);
        dec_close(&d);
        expect_channel(b, n, 4, 428, flat, 4, 0, 4, 64, 32);
        g_codec = NULL;
        free(b);
        winst.volume = 32;
        b = build(&n, "8CHN", 1, 1, NULL, &winst, 1, &wcell, 1);
        memcpy(b + 1080, "M.K.", 4u);
        dec_open(&d, b, n, NULL, NULL);
        CHECK(d.rc == MAKTABASAUTI_OK);
        CHECK(strcmp(d.info.codec_profile, "M.K.") == 0);
        dec_close(&d);
        g_codec = &maktabasauti_mod_wow_desc;
        dec_open(&d, b, n, NULL, NULL);
        CHECK(d.rc == MAKTABASAUTI_OK);
        CHECK(strcmp(d.info.codec_profile, "WOW") == 0);
        dec_close(&d);
        expect_channel(b, n, 4, 428, flat, 4, 0, 4, 32, 32);
        g_codec = NULL;
        free(b);
        winst.volume = 64;
        wcell.ch = 0;
        b = build(&n, "M.K.", 1, 0, NULL, &winst, 1, &wcell, 1);
        {
            size_t pat = 1024u;
            uint8_t *pad = (uint8_t *)malloc(n + pat);
            CHECK(pad != NULL);
            if (pad) {
                memcpy(pad, b, 1084u + pat);
                memset(pad + 1084u + pat, 0, pat);
                memcpy(pad + 1084u + pat + pat, b + 1084u + pat, n - 1084u - pat);
                dec_open(&d, pad, n + pat, NULL, NULL);
                CHECK(d.rc == MAKTABASAUTI_OK);
                CHECK(strcmp(d.info.codec_profile, "M.K.") == 0);
                dec_close(&d);
                g_codec = &maktabasauti_mod_wow_desc;
                dec_open(&d, pad, n + pat, NULL, NULL);
                CHECK(d.rc == MAKTABASAUTI_OK);
                CHECK(strcmp(d.info.codec_profile, "WOW") == 0);
                dec_close(&d);
                expect_channel(pad, n + pat, 0, 428, flat, 4, 0, 4, 64, 32);
                g_codec = NULL;
                free(pad);
            }
        }
        free(b);
        b = build(&n, "M.K.", 1, 0, NULL, &winst, 1, &wcell, 1);
        g_codec = &maktabasauti_mod_wow_desc;
        dec_open(&d, b, n, NULL, NULL);
        CHECK(d.rc == MAKTABASAUTI_E_TRUNCATED);
        dec_close(&d);
        g_codec = NULL;
        free(b);
        wcell.ch = 32;
        b = build(&n, "33CH", 1, 0, NULL, &winst, 1, &wcell, 1);
        expect_channel(b, n, 32, 428, flat, 4, 0, 4, 64, 16);
        free(b);
        wcell.ch = 98;
        b = build(&n, "99CH", 1, 0, NULL, &winst, 1, &wcell, 1);
        expect_channel(b, n, 98, 428, flat, 4, 0, 4, 64, 8);
        free(b);
        wcell.ch = 0;
        b = build(&n, "M.K.", 1, 0, NULL, &winst, 1, &wcell, 1);
        dec_open(&d, b, n, NULL, NULL);
        CHECK(d.rc == MAKTABASAUTI_OK);
        CHECK(strcmp(d.info.codec_profile, "M.K.") == 0);
        dec_close(&d); free(b);
    }
    b = build(&n, "TDZ4", 1, 0, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(strcmp(d.info.codec_profile, "TDZ4") == 0);
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
    expect_channel(b, n, 0, 431, square, 4, 0, 4, 64, 4000);
    free(b);

    /* Finetune +7, zapisane 428 (C-2): kolumna daje 407.  Efekt 000, bo E00 wlacza filtr. */
    inst.finetune = 0x07; inst.loop_w = 0; inst.loop_l = 2; inst.words = 2;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0, 0};
    b = build(&n, "FLT4", 1, 0, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(maktabasauti_mod_desc.get_tag(d.state, "finetune.1", tag, sizeof tag, NULL) == MAKTABASAUTI_OK);
    CHECK(strcmp(tag, "7") == 0);
    CHECK(strcmp(d.info.codec_profile, "FLT4") == 0);
    dec_close(&d);
    expect_channel(b, n, 0, 407, square, 4, 0, 4, 64, 8000);
    free(b);

    inst.finetune = 0x08;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0, 0};
    b = build(&n, "4CHN", 1, 0, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(maktabasauti_mod_desc.get_tag(d.state, "finetune.1", tag, sizeof tag, NULL) == MAKTABASAUTI_OK);
    CHECK(strcmp(tag, "-8") == 0);
    dec_close(&d);
    expect_channel(b, n, 0, 453, square, 4, 0, 4, 64, 2000);
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
    uint8_t flat[6] = {127, 127, 127, 127, 127, 127};
    Inst inst = {1, 3, 0, 64, 1, 2, flat};
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
    /* F00 na petli nie ma konca.  Seek w poblizu nadal slyszy probke, a daleki skok jest odrzucony. */
    CHECK(d.info.total_frames == MAKTABASAUTI_UNKNOWN_LENGTH);
    CHECK((d.info.flags & MAKTABASAUTI_STREAM_SEEKABLE) != 0u);
    CHECK((d.info.flags & MAKTABASAUTI_STREAM_LIVE) == 0u);
    {
        uint64_t actual = 0;
        int16_t one[2] = {0, 0};
        CHECK(maktabasauti_mod_desc.seek(d.state, 1000u, 0, &actual) == MAKTABASAUTI_OK);
        CHECK(actual == 1000u);
        CHECK(maktabasauti_mod_desc.read(d.state, one, 1u, &got) == MAKTABASAUTI_OK);
        CHECK(got == 1u);
        CHECK(one[0] == 127 * 64);
        CHECK(maktabasauti_mod_desc.seek(d.state, 5000000000ull, 0, &actual) == MAKTABASAUTI_E_OUT_OF_RANGE);
    }
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
    CHECK(pcm[0] == 0);
    CHECK(pcm[(size_t)CLEAR_AT * 2u] == 127 * 64);
    CHECK(pcm[(size_t)(loud - 1u) * 2u] == 127 * 64);
    CHECK(pcm[(size_t)loud * 2u] == 0);
    free(pcm);
    dec_close(&d); free(b);

    cells[0].param = 0x1F;
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.info.total_frames == (uint64_t)64u * 31u * TICK);
    dec_close(&d); free(b);

    /* B00 wraca na ten sam wiersz, wiec utwor konczy sie po tym wierszu. */
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0B, 0x00};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == (uint64_t)6u * TICK);
    dec_close(&d); free(b);

    /* D00 idzie na nastepny order, ktorego nie ma. */
    cells[0].effect = 0x0D; cells[0].param = 0x00;
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == (uint64_t)6u * TICK);
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
    CHECK(pcm[0] == 0);
    for (i = CLEAR_AT; i < (size_t)loud; i++) CHECK(pcm[i * 2u] == 127 * 64);
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
    uint8_t flat[6] = {127, 127, 127, 127, 127, 127};
    uint8_t neg[6] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
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
    insts[0] = (Inst){1, 3, 0, 64, 1, 2, flat};
    insts[1] = (Inst){2, 3, 0, 64, 1, 2, neg};
    cells[0] = (Cell){0, 0, 0, 1, 428, 0, 0};
    cells[1] = (Cell){0, 0, 3, 2, 428, 0, 0};
    b = build(&n, "M.K.", 1, 0, NULL, insts, 2, cells, 2);
    dec_open(&a, b, n, "44100", NULL);
    CHECK(a.rc == MAKTABASAUTI_OK);
    CHECK(maktabasauti_mod_desc.read(a.state, x, 32, &got) == MAKTABASAUTI_OK);
    /* Po wyzerowanych bajtach: 127*64 + (-128)*64 = -64 na lewym.  Prawy milczy. */
    for (i = 0; i < 32u; i++) {
        CHECK(x[i * 2u] == (i < CLEAR_AT ? 0 : -64));
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
    CHECK(y[0] == 0);
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
    size_t begin, end, frames;
    if (!pcm) { g_failures++; return; }
    begin = tick * TICK;
    end = begin + TICK;
    /* Poczatek nuty milczy, bo dwa pierwsze bajty sa zerem.  Kwadrat liczymy od pierwszej klatki z dzwiekiem. */
    while (begin < end && pcm[begin * 2u] == 0) begin++;
    frames = end - begin;
    got = frames ? flips(pcm, begin, end) : 0u;
    ratio = (unsigned)(((unsigned long long)frames * 3546895ull) / ((unsigned long long)period * 44100ull));
    if (!frames || got + 2u < ratio || got > ratio + 2u) {
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
    uint8_t square[6] = {0, 0, 127, 0x80, 127, 0x80};
    uint8_t flat[6] = {127, 127, 127, 127, 127, 127};
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
    inst.index = 1; inst.words = 3; inst.volume = 64; inst.loop_w = 1; inst.loop_l = 2; inst.data = square;

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

    inst.words = 32; inst.loop_w = 0; inst.loop_l = 32; inst.data = lead;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0, 0};
    cells[1] = (Cell){0, 1, 0, 0, 214, 0x03, 32};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    pcm = render_n(&d, b, n, 6u * TICK + 1u);
    CHECK(pcm[6u * TICK * 2u] == 127 * 64); /* faza nie wraca na bajt 0 */
    dec_close(&d); free(pcm); free(b);

    inst.words = 3; inst.loop_w = 1; inst.loop_l = 2; inst.data = square; inst.volume = 64;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0, 0};
    cells[1] = (Cell){0, 1, 0, 0, 214, 0x03, 32};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    pcm = render_n(&d, b, n, 8u * TICK);
    expect_period(pcm, 6, 428);
    expect_period(pcm, 7, 396);
    dec_close(&d); free(pcm); free(b);

    inst.words = 3; inst.loop_w = 1; inst.loop_l = 2; inst.data = square; inst.volume = 64;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x04, 0xF8};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, 3u * TICK);
    expect_period(pcm, 1, 428); /* sinus zaczyna sie od zera */
    expect_period(pcm, 2, 443); /* 254 * 8 / 128 = 15, okres 428 + 15 */
    dec_close(&d); free(pcm); free(b);

    inst.volume = 40; inst.words = 3; inst.loop_w = 1; inst.loop_l = 2; inst.data = flat;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0A, 0x18}; /* w gore o 1, nie w dol o 8 */
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, 2u * TICK);
    CHECK(pcm[0] == 0);
    CHECK(pcm[CLEAR_AT * 2u] == 127 * 40);
    CHECK(pcm[TICK * 2u] == 127 * 41);
    dec_close(&d); free(pcm); free(b);

    inst.volume = 60;
    cells[0].param = 0xF0;
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, 3u * TICK);
    CHECK(pcm[0] == 0);
    CHECK(pcm[CLEAR_AT * 2u] == 127 * 60);
    CHECK(pcm[TICK * 2u] == 127 * 64);
    CHECK(pcm[2u * TICK * 2u] == 127 * 64);
    dec_close(&d); free(pcm); free(b);

    inst.volume = 10;
    cells[0].param = 0x0F;
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, 2u * TICK);
    CHECK(pcm[0] == 0);
    CHECK(pcm[CLEAR_AT * 2u] == 127 * 10);
    CHECK(pcm[TICK * 2u] == 0);
    dec_close(&d); free(pcm); free(b);

    inst.volume = 32; inst.words = 3; inst.loop_w = 1; inst.loop_l = 2; inst.data = square;
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
    inst.volume = 64; inst.words = 129; inst.loop_w = 0; inst.loop_l = 1; inst.data = ramp;
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
    CHECK(d.rc == MAKTABASAUTI_E_UNSUPPORTED_FORMAT);
    dec_close(&d); free(b);

    memset(tiny, 0, sizeof tiny);
    tiny[470] = 1;
    dec_open(&d, tiny, sizeof tiny, NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_E_TRUNCATED);
    dec_close(&d);
    CHECK(d.bag.heap.live == 0);
}

static void test_position(void) {
    uint8_t flat[6] = {127, 127, 127, 127, 127, 127};
    Inst inst = {1, 3, 0, 64, 1, 2, flat};
    Cell cells[3];
    uint8_t orders[128];
    uint8_t *b;
    size_t n = 0, got = 0;
    Dec d;
    int16_t *pcm;
    size_t at;
    memset(orders, 0, sizeof orders);
    orders[1] = 1;
    orders[2] = 2;

    /* B02 omija pattern 1. Cisza jest na celu, wiec dalsze granie byloby nadal glosne. */
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0B, 0x02};
    cells[1] = (Cell){2, 0, 0, 0, 0, 0x0C, 0x00};
    b = build(&n, "M.K.", 3, 0, orders, &inst, 1, cells, 2);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == (uint64_t)(1u + 64u) * 6u * TICK);
    at = 6u * TICK;
    pcm = (int16_t *)malloc((at + 1u) * 2u * sizeof *pcm);
    CHECK(maktabasauti_mod_desc.read(d.state, pcm, at + 1u, &got) == MAKTABASAUTI_OK);
    CHECK(got == at + 1u);
    if (pcm) {
        CHECK(pcm[0] == 0);
        CHECK(pcm[CLEAR_AT * 2u] == 127 * 64);
        CHECK(pcm[(at - 1u) * 2u] == 127 * 64);
        CHECK(pcm[at * 2u] == 0);
    }
    free(pcm);
    dec_close(&d); free(b);

    /* D16 to wiersz 16, nie 22. Cisza lezy na wierszu 16 nastepnego patternu. */
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0D, 0x16};
    cells[1] = (Cell){1, 16, 0, 0, 0, 0x0C, 0x00};
    b = build(&n, "M.K.", 2, 0, orders, &inst, 1, cells, 2);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == (uint64_t)(1u + 48u) * 6u * TICK);
    pcm = (int16_t *)malloc((at + 1u) * 2u * sizeof *pcm);
    CHECK(maktabasauti_mod_desc.read(d.state, pcm, at + 1u, &got) == MAKTABASAUTI_OK);
    if (pcm) {
        CHECK(pcm[0] == 0);
        CHECK(pcm[CLEAR_AT * 2u] == 127 * 64);
        CHECK(pcm[at * 2u] == 0);
    }
    free(pcm);
    dec_close(&d); free(b);
    CHECK(d.bag.heap.live == 0);
}

static void test_extended(void) {
    uint8_t square[6] = {0, 0, 127, 0x80, 127, 0x80};
    uint8_t flat[6] = {127, 127, 127, 127, 127, 127};
    uint8_t shot[8] = {127, 127, 127, 0, 0, 0, 0, 0};
    Inst inst;
    Cell cells[6];
    uint8_t *b;
    size_t n = 0;
    Dec d;
    int16_t *pcm;
    char tag[32];
    memset(&inst, 0, sizeof inst);
    inst.index = 1; inst.words = 3; inst.volume = 64; inst.loop_w = 1; inst.loop_l = 2; inst.data = square;

    /* E1F schodzi o 15 juz w takcie 0.  E10 w nastepnym wierszu powtarza ten krok. */
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0x1F};
    cells[1] = (Cell){0, 1, 0, 0, 0, 0x0E, 0x10};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    pcm = render_n(&d, b, n, 7u * TICK);
    expect_period(pcm, 0, 413);
    expect_period(pcm, 5, 413);
    expect_period(pcm, 6, 398);
    dec_close(&d); free(pcm); free(b);

    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0x2F};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, TICK);
    expect_period(pcm, 0, 443);
    dec_close(&d); free(pcm); free(b);

    /* E5x stroi kolumne, nie zapisany okres.  Tag czyta bajt instrumentu. */
    inst.finetune = 0;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0x57};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(maktabasauti_mod_desc.get_tag(d.state, "finetune.1", tag, sizeof tag, NULL) == MAKTABASAUTI_OK);
    CHECK(strcmp(tag, "0") == 0);
    dec_close(&d);
    pcm = render_n(&d, b, n, 2u * TICK);
    expect_period(pcm, 0, 407);
    expect_period(pcm, 1, 407);
    dec_close(&d); free(pcm); free(b);

    /* Finetune +7, zapisane 856 (C-1): takt 0 gra 814.  Arpeggio +12 schodzi na 407, takt bez poltonu wraca na 814. */
    inst.finetune = 0x07;
    cells[0] = (Cell){0, 0, 0, 1, 856, 0x00, 0xC0};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, 3u * TICK);
    expect_period(pcm, 0, 814);
    expect_period(pcm, 1, 407);
    expect_period(pcm, 2, 814);
    dec_close(&d); free(pcm); free(b);

    /* E42 wybiera kwadrat i zeruje faze.  Nastepny wiersz vibrato 4 18 gra +255. */
    inst.finetune = 0;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0x42};
    cells[1] = (Cell){0, 1, 0, 0, 0, 0x04, 0x18};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    pcm = render_n(&d, b, n, 8u * TICK);
    expect_period(pcm, 6, 428);
    expect_period(pcm, 7, 443);
    dec_close(&d); free(pcm); free(b);

    /* E43 jest kwadratem: +255*8/128 daje 443, a pozycja 32 daje 413.  Sinus na starcie zostalby 428. */
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0x43};
    cells[1] = (Cell){0, 1, 0, 0, 0, 0x04, 0x88};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    pcm = render_n(&d, b, n, 12u * TICK);
    expect_period(pcm, 6, 428);
    expect_period(pcm, 7, 443);
    expect_period(pcm, 11, 413);
    dec_close(&d); free(pcm); free(b);

    /* Rampa E41 trwa kilka wierszy: pozycja 0 to +255 (okres 457), pozycja 16 to +127 (okres 442). */
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0x41};
    cells[1] = (Cell){0, 1, 0, 0, 0, 0x04, 0x1F};
    cells[2] = (Cell){0, 2, 0, 0, 0, 0x04, 0x1F};
    cells[3] = (Cell){0, 3, 0, 0, 0, 0x04, 0x1F};
    cells[4] = (Cell){0, 4, 0, 0, 0, 0x04, 0x1F};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 5);
    pcm = render_n(&d, b, n, 27u * TICK);
    expect_period(pcm, 7, 457);
    expect_period(pcm, 26, 442);
    dec_close(&d); free(pcm); free(b);

    /* Glissando po slizgu 120-5 lapie 113, blizej niz 120.  Takt 0 wiersza zostaje na 120. */
    cells[0] = (Cell){0, 0, 0, 1, 120, 0x0E, 0x31};
    cells[1] = (Cell){0, 1, 0, 0, 100, 0x03, 5};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    pcm = render_n(&d, b, n, 8u * TICK);
    expect_period(pcm, 6, 120);
    expect_period(pcm, 7, 113);
    dec_close(&d); free(pcm); free(b);

    inst.data = flat; inst.volume = 40; inst.words = 3; inst.loop_w = 1; inst.loop_l = 2;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0xA1};
    cells[1] = (Cell){0, 1, 0, 0, 0, 0x0E, 0xA0};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    pcm = render_n(&d, b, n, 6u * TICK + 1u);
    CHECK(pcm[0] == 0);
    CHECK(pcm[CLEAR_AT * 2u] == 127 * 41);
    CHECK(pcm[6u * TICK * 2u] == 127 * 42);
    dec_close(&d); free(pcm); free(b);

    inst.volume = 10;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0xB1};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, CLEAR_AT + 1u);
    CHECK(pcm[0] == 0);
    CHECK(pcm[CLEAR_AT * 2u] == 127 * 9);
    dec_close(&d); free(pcm); free(b);

    inst.volume = 32;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x07, 0x18};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, 2u * TICK + 1u);
    CHECK(pcm[0] == 0);
    CHECK(pcm[CLEAR_AT * 2u] == 127 * 32);
    CHECK(pcm[TICK * 2u] == 127 * 32);
    CHECK(pcm[2u * TICK * 2u] == 127 * 35);
    dec_close(&d); free(pcm); free(b);

    /* E73: ten sam kwadrat w tremolo.  255*8/64 = 31, wiec 32 rosnie do 63 i spada do 1. */
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0x73};
    cells[1] = (Cell){0, 1, 0, 0, 0, 0x07, 0x88};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    pcm = render_n(&d, b, n, 12u * TICK);
    CHECK(pcm[6u * TICK * 2u] == 127 * 32);
    CHECK(pcm[7u * TICK * 2u] == 127 * 63);
    CHECK(pcm[11u * TICK * 2u] == 127 * 1);
    dec_close(&d); free(pcm); free(b);

    inst.volume = 64;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0xC0};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, 1u);
    CHECK(pcm[0] == 0);
    dec_close(&d); free(pcm); free(b);

    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0xC1};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, TICK + 1u);
    CHECK(pcm[0] == 0);
    CHECK(pcm[CLEAR_AT * 2u] == 127 * 64);
    CHECK(pcm[(TICK - 1u) * 2u] == 127 * 64);
    CHECK(pcm[TICK * 2u] == 0);
    dec_close(&d); free(pcm); free(b);

    /* Jednorazowy poczatek ma 127 na bajcie 2, petla od bajtu 4 jest cicha.
     * E93 wraca na poczatek probki, nie na petle, wiec impuls wraca po takcie 3. */
    inst.words = 4; inst.loop_w = 2; inst.loop_l = 2; inst.data = shot;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0x93};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, 3u * TICK + CLEAR_AT + 1u);
    CHECK(pcm[0] == 0);
    CHECK(pcm[CLEAR_AT * 2u] == 127 * 64);
    CHECK(pcm[(CLEAR_AT + 20u) * 2u] == 0);
    CHECK(pcm[3u * TICK * 2u] == 0);
    CHECK(pcm[(3u * TICK + CLEAR_AT) * 2u] == 127 * 64);
    dec_close(&d); free(pcm); free(b);

    inst.words = 3; inst.loop_w = 1; inst.loop_l = 2; inst.data = flat; inst.volume = 64;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0xD3};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, 3u * TICK + CLEAR_AT + 1u);
    CHECK(pcm[0] == 0);
    CHECK(pcm[CLEAR_AT * 2u] == 0);
    CHECK(pcm[(3u * TICK - 1u) * 2u] == 0);
    CHECK(pcm[3u * TICK * 2u] == 0);
    CHECK(pcm[(3u * TICK + CLEAR_AT) * 2u] == 127 * 64);
    dec_close(&d); free(pcm); free(b);

    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0xE1};
    cells[1] = (Cell){0, 1, 0, 0, 0, 0x0C, 0x00};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == (uint64_t)(64u * 6u + 6u) * TICK);
    dec_close(&d);
    pcm = render_n(&d, b, n, 12u * TICK + 1u);
    CHECK(pcm[0] == 0);
    CHECK(pcm[CLEAR_AT * 2u] == 127 * 64);
    CHECK(pcm[6u * TICK * 2u] == 127 * 64);
    CHECK(pcm[12u * TICK * 2u] == 0);
    dec_close(&d); free(pcm); free(b);

    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0xE0};
    cells[1] = (Cell){0, 1, 0, 0, 0, 0x0C, 0x00};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.info.total_frames == (uint64_t)64u * 6u * TICK);
    dec_close(&d);
    pcm = render_n(&d, b, n, 6u * TICK + 1u);
    CHECK(pcm[6u * TICK * 2u] == 0);
    dec_close(&d); free(pcm); free(b);

    /* E60 zapamietuje wiersz, E62 na drugim kanale wraca dwa razy.  C00 robi dziure ciszy. */
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0x60};
    cells[1] = (Cell){0, 1, 0, 0, 0, 0x0C, 0x00};
    cells[2] = (Cell){0, 1, 1, 0, 0, 0x0E, 0x62};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 3);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == (uint64_t)(64u + 4u) * 6u * TICK);
    dec_close(&d);
    pcm = render_n(&d, b, n, 12u * TICK + CLEAR_AT + 1u);
    CHECK(pcm[0] == 0);
    CHECK(pcm[CLEAR_AT * 2u] == 127 * 64);
    CHECK(pcm[6u * TICK * 2u] == 0);
    CHECK(pcm[12u * TICK * 2u] == 0);
    CHECK(pcm[(12u * TICK + CLEAR_AT) * 2u] == 127 * 64);
    dec_close(&d); free(pcm); free(b);

    /* E60 w patternie 0 zostawia wiersz 10.  E62 w nastepnym skacze do przodu, nie na zero. */
    {
        uint8_t orders[128];
        memset(orders, 0, sizeof orders);
        orders[1] = 1;
        cells[0] = (Cell){0, 10, 0, 0, 0, 0x0E, 0x60};
        cells[1] = (Cell){1, 2, 0, 0, 0, 0x0E, 0x62};
        b = build(&n, "M.K.", 2, 0, orders, &inst, 1, cells, 2);
        dec_open(&d, b, n, "44100", NULL);
        CHECK(d.rc == MAKTABASAUTI_OK);
        CHECK(d.info.total_frames == (uint64_t)121u * 6u * TICK);
        dec_close(&d); free(b);
    }

    /* E65 ustawia licznik, B01 zmienia pozycje i zostawia ten licznik.  E61 schodzi z 5. */
    {
        uint8_t orders[128];
        memset(orders, 0, sizeof orders);
        orders[1] = 1;
        cells[0] = (Cell){0, 0, 0, 0, 0, 0x0E, 0x60};
        cells[1] = (Cell){0, 2, 0, 0, 0, 0x0E, 0x65};
        cells[2] = (Cell){0, 2, 1, 0, 0, 0x0B, 0x01};
        cells[3] = (Cell){1, 1, 0, 0, 0, 0x0E, 0x61};
        b = build(&n, "M.K.", 2, 0, orders, &inst, 1, cells, 4);
        dec_open(&d, b, n, "44100", NULL);
        CHECK(d.rc == MAKTABASAUTI_OK);
        CHECK(d.info.total_frames == (uint64_t)75u * 6u * TICK);
        dec_close(&d); free(b);
    }

    /* Kanal 1 domyslnie jest po prawej.  800 przenosi go na lewo. */
    cells[0] = (Cell){0, 0, 1, 1, 428, 0x08, 0x00};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, CLEAR_AT + 1u);
    CHECK(pcm[0] == 0);
    CHECK(pcm[CLEAR_AT * 2u] == 127 * 64);
    CHECK(pcm[CLEAR_AT * 2u + 1u] == 0);
    dec_close(&d); free(pcm); free(b);

    /* E8F to nibble 15 * 17 = 255, czyli prawa strona na kanale 0. */
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0x8F};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, CLEAR_AT + 1u);
    CHECK(pcm[CLEAR_AT * 2u] == 0);
    CHECK(pcm[CLEAR_AT * 2u + 1u] == 127 * 64);
    dec_close(&d); free(pcm); free(b);
    CHECK(d.bag.heap.live == 0);
}

static void test_restart(void) {
    uint8_t flat[6] = {127, 127, 127, 127, 127, 127};
    Inst inst = {1, 3, 0, 64, 1, 2, flat};
    Cell cells[3];
    uint8_t orders[128];
    uint8_t *b;
    size_t n = 0, got = 0;
    Dec d;
    int16_t *pcm;
    size_t at_silence, at_back;
    unsigned i;

    /* Pozycja, ktora juz grala, nie wydluza utworu. */
    memset(orders, 0, sizeof orders);
    b = build(&n, "M.K.", 2, 1, NULL, &inst, 1, NULL, 0);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == (uint64_t)2u * 64u * 6u * TICK);
    dec_close(&d); free(b);

    /* B02 omija pozycje 1. Po koncu listy restart 1 gra ja, a drugi raz juz staje. */
    orders[1] = 1;
    orders[2] = 2;
    orders[3] = 3;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0B, 2};
    cells[1] = (Cell){2, 0, 0, 0, 0, 0x0C, 0x00};
    cells[2] = (Cell){1, 0, 0, 1, 428, 0, 0};
    b = build(&n, "M.K.", 4, 1, orders, &inst, 1, cells, 3);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == (uint64_t)(1u + 5u * 64u) * 6u * TICK);
    at_silence = 6u * TICK;
    at_back = (uint64_t)(1u + 2u * 64u) * 6u * TICK;
    pcm = (int16_t *)malloc((at_back + CLEAR_AT + 1u) * 2u * sizeof *pcm);
    CHECK(maktabasauti_mod_desc.read(d.state, pcm, at_back + CLEAR_AT + 1u, &got) == MAKTABASAUTI_OK);
    CHECK(got == at_back + CLEAR_AT + 1u);
    if (pcm) {
        CHECK(pcm[0] == 0);
        CHECK(pcm[CLEAR_AT * 2u] == 127 * 64);
        CHECK(pcm[at_silence * 2u] == 0);
        CHECK(pcm[(at_back - 1u) * 2u] == 0);
        CHECK(pcm[at_back * 2u] == 0);
        CHECK(pcm[(at_back + CLEAR_AT) * 2u] == 127 * 64);
    }
    free(pcm);
    dec_close(&d); free(b);

    /* Bxx poza lista konczy utwor i nie uruchamia restartu. */
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0B, 5};
    b = build(&n, "M.K.", 3, 1, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == (uint64_t)6u * TICK);
    dec_close(&d); free(b);

    /* 0x78 to nie pozycja 120, nawet gdy ta pozycja zostala pominieta. */
    memset(orders, 0, sizeof orders);
    for (i = 121u; i < 128u; i++) orders[i] = 1;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0B, 121};
    b = build(&n, "M.K.", 128, 0x78, orders, &inst, 1, cells, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == (uint64_t)(1u + 7u * 64u) * 6u * TICK);
    dec_close(&d); free(b);

    /* 0x7F tez nie jest pozycja, przy dlugosci 128. */
    b = build(&n, "M.K.", 128, 0x7F, orders, &inst, 1, cells, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == (uint64_t)(1u + 7u * 64u) * 6u * TICK);
    dec_close(&d); free(b);
    CHECK(d.bag.heap.live == 0);
}

static void test_invert(void) {
    uint8_t looped[4] = {127, 127, 127, 127};
    uint8_t once[4] = {127, 127, 127, 127};
    Inst inst = {1, 2, 0, 64, 0, 2, looped};
    Inst noloop = {1, 2, 0, 64, 0, 1, once};
    Cell cells[8];
    uint8_t *b;
    size_t n = 0, sample_at;
    Dec d;
    int16_t *pcm;

    /* EFf odpala co takt.  Bajty 0 i 1 sa zerem, 0 ^ 0xFF = -1.  Obie strony slysza -64. */
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0xFF};
    cells[1] = (Cell){0, 0, 1, 1, 428, 0, 0};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    sample_at = 1084u + 64u * 4u * 4u;
    pcm = render_n(&d, b, n, 1u);
    CHECK(pcm && pcm[0] == -1 * 64 && pcm[1] == -1 * 64);
    CHECK(b[sample_at] == 127 && b[sample_at + 1u] == 127);
    CHECK(b[sample_at + 2u] == 127 && b[sample_at + 3u] == 127);
    dec_close(&d); free(pcm); free(b);

    /* EF1 dodaje 5.  Takt 0 jeszcze nie siega 128, wiec zostaje zero z poczatku probki. */
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0xF1};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    pcm = render_n(&d, b, n, 1u);
    CHECK(pcm && pcm[0] == 0 && pcm[1] == 0);
    dec_close(&d); free(pcm); free(b);

    /* EF0 gasi efekt. */
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0xF0};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    pcm = render_n(&d, b, n, 1u);
    CHECK(pcm && pcm[0] == 0 && pcm[1] == 0);
    dec_close(&d); free(pcm); free(b);

    /* Jedno slowo petli to brak petli.  EFf nic nie rusza, a bajt 2 gra. */
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0xFF};
    b = build(&n, "M.K.", 1, 0, NULL, &noloop, 1, cells, 1);
    pcm = render_n(&d, b, n, CLEAR_AT + 1u);
    CHECK(pcm && pcm[0] == 0 && pcm[1] == 0);
    CHECK(pcm && pcm[CLEAR_AT * 2u] == 127 * 64);
    dec_close(&d); free(pcm); free(b);

    /* F01: wiersz to jeden takt.  EF1 dochodzi do 130 w wierszu 25, wiec dopiero wtedy odwraca bajt 0.
     * Retrigger tej samej probki slyszy wlasnie odwrocony bajt, a wiersz 24 jeszcze oryginal. */
    cells[0] = (Cell){0, 0, 2, 0, 0, 0x0F, 0x01};
    cells[1] = (Cell){0, 0, 0, 1, 428, 0x0E, 0xF1};
    cells[2] = (Cell){0, 0, 1, 1, 428, 0, 0};
    cells[3] = (Cell){0, 24, 0, 1, 428, 0, 0};
    cells[4] = (Cell){0, 24, 1, 1, 428, 0, 0};
    cells[5] = (Cell){0, 25, 0, 1, 428, 0, 0};
    cells[6] = (Cell){0, 25, 1, 1, 428, 0, 0};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 7);
    pcm = render_n(&d, b, n, 25u * TICK + 1u);
    CHECK(pcm != NULL);
    if (pcm) {
        CHECK(pcm[0] == 0);
        CHECK(pcm[1] == 0);
        CHECK(pcm[24u * TICK * 2u] == 0);
        CHECK(pcm[24u * TICK * 2u + 1u] == 0);
        CHECK(pcm[25u * TICK * 2u] == -1 * 64);
        CHECK(pcm[25u * TICK * 2u + 1u] == -1 * 64);
    }
    dec_close(&d); free(pcm); free(b);
    CHECK(d.bag.heap.live == 0);
}

static void test_sample_clear(void) {
    uint8_t looped[4] = {127, 127, 127, 127};
    uint8_t once4[4] = {127, 127, 127, 127};
    uint8_t once2[2] = {127, 127};
    Inst loop = {1, 2, 0, 64, 0, 2, looped};
    Inst shot4 = {1, 2, 0, 64, 0, 1, once4};
    Inst shot2 = {1, 1, 0, 64, 0, 0, once2};
    Cell cell = {0, 0, 0, 1, 428, 0, 0};
    uint8_t *b;
    size_t n = 0, sample_at, i;
    Dec d;
    int16_t *pcm;

    /* Petla wraca na bajt 0, wiec zero jest w probce, nie tylko na pierwszej klatce.  Plik zostaje 127. */
    b = build(&n, "M.K.", 1, 0, NULL, &loop, 1, &cell, 1);
    sample_at = 1084u + 64u * 4u * 4u;
    pcm = render_n(&d, b, n, 12u * TICK + 1u);
    CHECK(pcm != NULL);
    if (pcm) {
        CHECK(pcm[0] == 0);
        CHECK(pcm[CLEAR_AT * 2u] == 127 * 64);
        CHECK(pcm[12u * TICK * 2u] == 0);
    }
    CHECK(b[sample_at] == 127 && b[sample_at + 1u] == 127);
    CHECK(b[sample_at + 2u] == 127 && b[sample_at + 3u] == 127);
    dec_close(&d); free(pcm); free(b);

    /* Cztery bajty jednorazowo: zerowane sa dwa, nie cztery. */
    b = build(&n, "M.K.", 1, 0, NULL, &shot4, 1, &cell, 1);
    pcm = render_n(&d, b, n, CLEAR_AT + 1u);
    CHECK(pcm && pcm[0] == 0);
    CHECK(pcm && pcm[CLEAR_AT * 2u] == 127 * 64);
    dec_close(&d); free(pcm); free(b);

    /* Dwa bajty jednorazowo znikaja w calosci. */
    b = build(&n, "M.K.", 1, 0, NULL, &shot2, 1, &cell, 1);
    pcm = render_n(&d, b, n, CLEAR_AT + 1u);
    if (pcm) for (i = 0; i < CLEAR_AT + 1u; i++) CHECK(pcm[i * 2u] == 0);
    dec_close(&d); free(pcm); free(b);
    CHECK(d.bag.heap.live == 0);
}

static void test_clock(void) {
    uint8_t *sample = (uint8_t *)calloc(202, 1);
    Inst inst = {1, 101, 0, 64, 0, 0, sample};
    Cell cell = {0, 0, 0, 1, 428, 0, 0};
    uint8_t *b;
    size_t n = 0, got = 0;
    Dec d;
    int16_t *pcm;
    uint64_t frames;
    /* Indeks 200 przy okresie 428: PAL klatki 1065..1069, NTSC 1055..1059. */
    sample[200] = 127;
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    frames = d.info.total_frames;
    dec_close(&d);
    pcm = render_n(&d, b, n, 1071u);
    CHECK(pcm != NULL);
    if (pcm) {
        CHECK(pcm[1055u * 2u] == 0);
        CHECK(pcm[1065u * 2u] == 127 * 64);
        CHECK(pcm[1069u * 2u] == 127 * 64);
        CHECK(pcm[1070u * 2u] == 0);
    }
    dec_close(&d); free(pcm);

    dec_open_ex(&d, b, n, "44100", NULL, "PAL", NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == frames);
    dec_close(&d);

    dec_open_ex(&d, b, n, "44100", NULL, "ntsc", NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == frames);
    pcm = (int16_t *)calloc(1071u * 2u, sizeof *pcm);
    CHECK(maktabasauti_mod_desc.read(d.state, pcm, 1071u, &got) == MAKTABASAUTI_OK);
    if (pcm) {
        CHECK(pcm[1054u * 2u] == 0);
        CHECK(pcm[1055u * 2u] == 127 * 64);
        CHECK(pcm[1059u * 2u] == 127 * 64);
        CHECK(pcm[1060u * 2u] == 0);
        CHECK(pcm[1065u * 2u] == 0);
    }
    dec_close(&d); free(pcm);

    dec_open_ex(&d, b, n, "44100", NULL, "3579545", NULL, NULL);
    CHECK(d.rc == MAKTABASAUTI_E_INVALID_ARGUMENT);
    dec_close(&d);
    free(b); free(sample);
    CHECK(d.bag.heap.live == 0);
}

static void test_latch(void) {
    uint8_t *keep = (uint8_t *)malloc(500);
    uint8_t next[6];
    uint8_t shot[8];
    uint8_t late[4];
    Inst insts[2];
    Cell cells[3];
    uint8_t *b;
    size_t n = 0;
    Dec d;
    int16_t *pcm;
    memset(keep, 90, 500);
    memset(next, 127, sizeof next);
    memset(shot, 0, sizeof shot);
    shot[2] = 127;
    memset(late, 127, sizeof late);
    memset(insts, 0, sizeof insts);
    memset(cells, 0, sizeof cells);

    /* Petla 90 gra dalej po numerze, glosnosc spada od razu.  Koniec przebiegu bierze petle 127. */
    insts[0] = (Inst){1, 250, 0, 64, 1, 249, keep};
    insts[1] = (Inst){2, 3, 0, 32, 1, 2, next};
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0F, 1};
    cells[1] = (Cell){0, 1, 0, 2, 0, 0, 0};
    b = build(&n, "M.K.", 1, 0, NULL, insts, 2, cells, 2);
    pcm = render_n(&d, b, n, 2662u);
    CHECK(pcm[11u * 2u] == 90 * 64);
    CHECK(pcm[881u * 2u] == 90 * 64);
    CHECK(pcm[882u * 2u] == 90 * 32);
    CHECK(pcm[2660u * 2u] == 90 * 32);
    CHECK(pcm[2661u * 2u] == 127 * 32);
    dec_close(&d); free(pcm); free(b);

    /* Jednorazowa probka juz milczy.  Petla startuje od swojego poczatku, nie od bajtu 0. */
    memset(next, 50, sizeof next);
    insts[0] = (Inst){1, 4, 0, 64, 0, 0, shot};
    insts[1] = (Inst){2, 3, 0, 64, 1, 2, next};
    cells[0] = (Cell){0, 0, 0, 1, 428, 0, 0};
    cells[1] = (Cell){0, 1, 0, 2, 0, 0, 0};
    b = build(&n, "M.K.", 1, 0, NULL, insts, 2, cells, 2);
    pcm = render_n(&d, b, n, 5293u);
    CHECK(pcm[CLEAR_AT * 2u] == 127 * 64);
    CHECK(pcm[100u * 2u] == 0);
    CHECK(pcm[5292u * 2u] == 50 * 64);
    dec_close(&d); free(pcm); free(b);

    /* Nuta po zatrzasku gra nowa probke od poczatku.  Dwa pierwsze bajty nadal milcza. */
    insts[0] = (Inst){1, 250, 0, 64, 0, 0, keep};
    insts[1] = (Inst){2, 2, 0, 64, 0, 0, late};
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0F, 1};
    cells[1] = (Cell){0, 1, 0, 2, 0, 0, 0};
    cells[2] = (Cell){0, 2, 0, 0, 428, 0, 0};
    b = build(&n, "M.K.", 1, 0, NULL, insts, 2, cells, 3);
    pcm = render_n(&d, b, n, 1776u);
    CHECK(pcm[882u * 2u] == 90 * 64);
    CHECK(pcm[1764u * 2u] == 0);
    CHECK(pcm[1775u * 2u] == 127 * 64);
    dec_close(&d); free(pcm); free(b);
    free(keep);
    CHECK(d.bag.heap.live == 0);
}

static uint64_t mean_abs(const int16_t *pcm, size_t begin, size_t end) {
    uint64_t s = 0;
    size_t i, n = end > begin ? end - begin : 0;
    if (!pcm || !n) return 0;
    for (i = begin; i < end; i++) {
        int v = pcm[i * 2u];
        if (v < 0) v = -v;
        s += (uint64_t)v;
    }
    return s / n;
}

/* Zachowania, ktorych stare oczekiwania nie rozrozniaja: kumulowany 9xx, petla od zera,
 * F00 na jednorazowej probce, Bxx za lista, Fxx 15-instrumentowego, rampa tremolo,
 * filtr E0x, opcja a500, nazwa instrumentu i egzotyczne magiki. */
static void test_coverage(void) {
    uint8_t square[4] = {127, 0x80, 127, 0x80};
    uint8_t flat4[4] = {127, 127, 127, 127};
    uint8_t flat6[6] = {127, 127, 127, 127, 127, 127};
    uint8_t once[8] = {9, 9, 0, 0, 0, 0, 127, 127};
    uint8_t *far = (uint8_t *)calloc(514, 1);
    uint8_t orders[128];
    Inst inst;
    Cell cells[5];
    uint8_t *b;
    size_t n = 0, i;
    Dec d;
    int16_t *dry, *led, *off, *rc;
    uint64_t dry_mean, led_mean, rc_mean;
    char tag[32];
    memset(&inst, 0, sizeof inst);
    memset(cells, 0, sizeof cells);
    memset(orders, 0, sizeof orders);
    CHECK(far != NULL);
    far[256] = 50;
    far[512] = 127;

    /* 9xx param 1 gra bajt 256, a nastepna nuta bez instrumentu startuje z 512. */
    inst.index = 1; inst.words = 257; inst.volume = 64; inst.loop_w = 0; inst.loop_l = 1; inst.data = far;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x09, 1};
    cells[1] = (Cell){0, 1, 0, 0, 428, 0, 0};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    dry = render_n(&d, b, n, 6u * TICK + 1u);
    CHECK(dry[0] == 50 * 64);
    CHECK(dry[6u * TICK * 2u] == 127 * 64);
    dec_close(&d); free(dry); free(b);

    /* Petla od bajtu 0: cala probka gra raz, wiec bajt 6 jest slyszalny. */
    inst.words = 4; inst.loop_w = 0; inst.loop_l = 2; inst.data = once; inst.volume = 64;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0, 0};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    dry = render_n(&d, b, n, 40u);
    CHECK(dry[31u * 2u] == 0);
    CHECK(dry[32u * 2u] == 127 * 64);
    dec_close(&d); free(dry); free(b);

    /* F00 na jednorazowej probce konczy sie po pierwszym takcie. */
    inst.words = 2; inst.loop_l = 1; inst.data = flat4;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0F, 0x00};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == (uint64_t)TICK);
    dec_close(&d); free(b);

    /* Bxx za koniec listy wraca na pozycje 0, a D16 zostawia wiersz 16. */
    inst.words = 2; inst.loop_l = 2; inst.data = square;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0B, 5};
    cells[1] = (Cell){0, 0, 1, 0, 0, 0x0D, 0x16};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 2);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == (uint64_t)(1u + 48u) * 6u * TICK);
    dec_close(&d); free(b);

    /* 15 instrumentow: kazde niezerowe Fxx jest predkoscia, takze 32. */
    inst.words = 2; inst.loop_l = 1; inst.data = flat4;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0F, 32};
    b = build15(&n, 1, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(strcmp(d.info.codec_profile, "15ST") == 0);
    CHECK(d.info.total_frames == (uint64_t)64u * 32u * TICK);
    dec_close(&d); free(b);

    /* Rampa tremolo patrzy na polowke pozycji vibrato.  Po 225 krokach pozycja to 33, delta 31. */
    inst.words = 3; inst.volume = 32; inst.loop_w = 1; inst.loop_l = 2; inst.data = flat6;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x04, 0xF0};
    cells[1] = (Cell){0, 1, 0, 0, 0, 0x04, 0xF0};
    cells[2] = (Cell){0, 2, 0, 0, 0, 0x04, 0xF0};
    cells[3] = (Cell){0, 3, 0, 0, 0, 0x0E, 0x71};
    cells[4] = (Cell){0, 4, 0, 0, 0, 0x07, 0x18};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 5);
    dry = render_n(&d, b, n, 26u * TICK + 1u);
    CHECK(dry[25u * TICK * 2u] == 127 * 32);
    CHECK(dry[26u * TICK * 2u] == 127 * 63);
    dec_close(&d); free(dry); free(b);

    /* E00 tlumi kwadrat okresu 113.  E01 jest suchym miksem.  Stala po E00 wraca do 127*64. */
    inst.volume = 64; inst.words = 3; inst.loop_w = 1; inst.loop_l = 2; inst.data = flat6;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0x00};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    dry = render_n(&d, b, n, (size_t)TICK + 1u);
    CHECK(dry[(size_t)TICK * 2u] >= 127 * 64 - 1);
    CHECK(dry[(size_t)TICK * 2u] <= 127 * 64 + 1);
    dec_close(&d); free(dry); free(b);

    inst.data = (const uint8_t *)"\x00\x00\x7f\x80\x7f\x80";
    cells[0] = (Cell){0, 0, 0, 1, 113, 0, 0};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    dry = render_n(&d, b, n, 2u * TICK);
    dec_close(&d);
    cells[0].effect = 0x0E; cells[0].param = 0x00;
    free(b);
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    led = render_n(&d, b, n, 2u * TICK);
    dec_close(&d); free(b);
    cells[0].param = 0x01;
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    off = render_n(&d, b, n, 2u * TICK);
    dec_close(&d); free(b);
    dry_mean = mean_abs(dry, TICK, 2u * TICK);
    led_mean = mean_abs(led, TICK, 2u * TICK);
    if (!(dry_mean > 1000u && led_mean * 2u < dry_mean)) {
        fprintf(stderr, "FAIL filtr E00: suchy %llu led %llu\n",
                (unsigned long long)dry_mean, (unsigned long long)led_mean);
        g_failures++;
    }
    for (i = 0; i < 2u * TICK && dry && off; i++) {
        if (dry[i * 2u] != off[i * 2u] || dry[i * 2u + 1u] != off[i * 2u + 1u]) {
            fprintf(stderr, "FAIL E01 ramka %zu: %d %d, suchy %d %d\n",
                    i, off[i * 2u], off[i * 2u + 1u], dry[i * 2u], dry[i * 2u + 1u]);
            g_failures++;
            break;
        }
    }
    free(dry); free(led); free(off);

    /* a500 tlumi ten sam kwadrat i nie zmienia skali stalej.  Zly model oddaje maske. */
    cells[0] = (Cell){0, 0, 0, 1, 113, 0, 0};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    memset(&d, 0, sizeof d);
    dec_open_ex(&d, b, n, "44100", NULL, NULL, "a500", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    rc = (int16_t *)calloc(2u * TICK * 2u, sizeof *rc);
    if (d.rc == MAKTABASAUTI_OK && rc) {
        size_t got = 0;
        CHECK(maktabasauti_mod_desc.read(d.state, rc, 2u * TICK, &got) == MAKTABASAUTI_OK);
    }
    dec_close(&d);
    rc_mean = mean_abs(rc, TICK, 2u * TICK);
    if (!(rc_mean + rc_mean / 5u < dry_mean)) {
        fprintf(stderr, "FAIL a500: suchy %llu rc %llu\n",
                (unsigned long long)dry_mean, (unsigned long long)rc_mean);
        g_failures++;
    }
    free(rc); free(b);

    inst.data = flat6; inst.words = 3; inst.loop_w = 1; inst.loop_l = 2;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0, 0};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    dec_open_ex(&d, b, n, "44100", NULL, NULL, "A500", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    rc = (int16_t *)calloc(((size_t)TICK + 1u) * 2u, sizeof *rc);
    if (d.rc == MAKTABASAUTI_OK && rc) {
        size_t got = 0;
        CHECK(maktabasauti_mod_desc.read(d.state, rc, (size_t)TICK + 1u, &got) == MAKTABASAUTI_OK);
        CHECK(rc[(size_t)TICK * 2u] >= 127 * 64 - 1);
        CHECK(rc[(size_t)TICK * 2u] <= 127 * 64 + 1);
    }
    dec_close(&d); free(rc); free(b);

    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    memcpy(b + 20, "Bass", 4u);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(maktabasauti_mod_desc.get_tag(d.state, "name.1", tag, sizeof tag, NULL) == MAKTABASAUTI_OK);
    CHECK(strcmp(tag, "Bass") == 0);
    CHECK(maktabasauti_mod_desc.get_tag(d.state, "name.0", tag, sizeof tag, NULL) == MAKTABASAUTI_E_UNSUPPORTED_FEATURE);
    CHECK(maktabasauti_mod_desc.get_tag(d.state, "name.32", tag, sizeof tag, NULL) == MAKTABASAUTI_E_UNSUPPORTED_FEATURE);
    dec_close(&d); free(b);

    orders[0] = 64;
    inst.words = 2; inst.loop_w = 0; inst.loop_l = 1; inst.data = flat4;
    cells[0] = (Cell){0, 0, 0, 1, 428, 0, 0};
    b = build(&n, "PATT", 1, 0, orders, &inst, 1, cells, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(strcmp(d.info.codec_profile, "PATT") == 0);
    dec_close(&d); free(b);
    b = build(&n, "N.T.", 1, 0, orders, &inst, 1, cells, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_E_UNSUPPORTED_FEATURE);
    dec_close(&d); free(b);
    b = build(&n, "M!K!", 1, 0, orders, &inst, 1, cells, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    dec_close(&d); free(b);

    orders[0] = 126;
    b = build(&n, "EXO8", 1, 0, orders, &inst, 1, cells, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(strcmp(d.info.codec_profile, "EXO8") == 0);
    dec_close(&d); free(b);
    orders[0] = 127;
    b = build(&n, "EX08", 1, 0, orders, &inst, 1, cells, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_E_UNSUPPORTED_FEATURE);
    dec_close(&d); free(b);

    inst.words = 2; inst.loop_w = 0; inst.loop_l = 2; inst.volume = 64; inst.data = square;
    cells[0] = (Cell){0, 0, 4, 1, 428, 0, 0};
    b = build(&n, "EXO8", 1, 0, NULL, &inst, 1, cells, 1);
    expect_channel(b, n, 4, 428, square, 4, 0, 4, 64, 200);
    free(b);
    b = build(&n, "EX08", 1, 0, NULL, &inst, 1, cells, 1);
    expect_channel(b, n, 4, 428, square, 4, 0, 4, 64, 200);
    free(b);
    cells[0].ch = 5;
    b = build(&n, "CD61", 1, 0, NULL, &inst, 1, cells, 1);
    expect_channel(b, n, 5, 428, square, 4, 0, 4, 64, 200);
    free(b);
    b = build(&n, "FA06", 1, 0, NULL, &inst, 1, cells, 1);
    expect_channel(b, n, 5, 428, square, 4, 0, 4, 64, 200);
    free(b);
    cells[0].ch = 7;
    b = build(&n, "FA08", 1, 0, NULL, &inst, 1, cells, 1);
    expect_channel(b, n, 7, 428, square, 4, 0, 4, 64, 200);
    free(b);
    cells[0].ch = 8;
    b = build(&n, "TDZ9", 1, 0, NULL, &inst, 1, cells, 1);
    expect_channel(b, n, 8, 428, square, 4, 0, 4, 64, 200);
    free(b);
    cells[0].ch = 0;
    b = build(&n, "FA04", 1, 0, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(strcmp(d.info.codec_profile, "FA04") == 0);
    dec_close(&d); free(b);
    b = build(&n, "NSMS", 1, 0, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    dec_close(&d); free(b);
    b = build(&n, "LARD", 1, 0, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    dec_close(&d); free(b);
    b = build(&n, "EXO4", 1, 0, NULL, &inst, 1, cells, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    dec_close(&d); free(b);
    memset(orders, 0, sizeof orders);
    b = build(&n, "N.T.", 1, 0, orders, &inst, 1, cells, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    dec_close(&d); free(b);

    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 1);
    dec_open_ex(&d, b, n, "44100", NULL, NULL, "amiga", NULL);
    CHECK(d.rc == MAKTABASAUTI_E_INVALID_ARGUMENT);
    dec_close(&d);
    free(b); free(far);
    CHECK(d.bag.heap.live == 0);
}

extern void maktabasauti_mod_voice_add(int32_t acc[2], const uint8_t *sample, const uint8_t *flip,
                                       uint32_t idx, uint32_t length, uint32_t clear_n,
                                       int32_t vol, uint32_t pan);

static void voice_add_c(int32_t acc[2], const uint8_t *sample, const uint8_t *flip, uint32_t idx,
                        uint32_t length, uint32_t clear_n, int32_t vol, uint32_t pan) {
    uint8_t b;
    int32_t s;
    if (!sample || idx >= length) return;
    b = idx < clear_n ? 0u : sample[idx];
    if (flip && ((flip[idx >> 3] >> (idx & 7u)) & 1u)) b ^= 0xFFu;
    s = (b < 128u ? (int32_t)b : (int32_t)b - 256) * vol;
    acc[0] += s * (255 - (int32_t)pan) / 255;
    acc[1] += s * (int32_t)pan / 255;
}

static void check_add(const uint8_t *sample, const uint8_t *flip, uint32_t idx, uint32_t length,
                      uint32_t clear_n, int32_t vol, uint32_t pan) {
    int32_t a[2], b[2];
    a[0] = b[0] = 7;
    a[1] = b[1] = 9;
    voice_add_c(a, sample, flip, idx, length, clear_n, vol, pan);
    maktabasauti_mod_voice_add(b, sample, flip, idx, length, clear_n, vol, pan);
    CHECK(a[0] == b[0] && a[1] == b[1]);
}

static void test_mixer(void) {
    uint8_t sample[6] = {127, 80, 10, 200, 0x80, 10};
    uint8_t flip[1] = {0};
    unsigned idx;
    for (idx = 0; idx < 4u; idx++) {
        check_add(sample, NULL, idx, 6u, 2u, 64, 0);
        check_add(sample, NULL, idx, 6u, 4u, 64, 255);
        check_add(sample, NULL, idx, 6u, 0u, 64, 128);
    }
    check_add(sample, NULL, 2u, 6u, 2u, 64, 255);
    check_add(sample, NULL, 4u, 6u, 2u, 64, 128);
    check_add(sample, NULL, 4u, 6u, 0u, -3, 0);
    flip[0] = (uint8_t)(1u << 5);
    check_add(sample, flip, 5u, 6u, 2u, 64, 0);
    check_add(NULL, NULL, 0u, 0u, 2u, 64, 0);
    check_add(sample, NULL, 6u, 6u, 2u, 64, 0);
    check_add(sample, NULL, 0u, 0u, 4u, 64, 128);
}

static int16_t *render_opt(Dec *d, const uint8_t *mod, size_t n, size_t frames, const char *tracker) {
    int16_t *pcm = (int16_t *)calloc(frames * 2u, sizeof *pcm);
    size_t got = 0;
    dec_open_ex(d, mod, n, "44100", NULL, NULL, NULL, tracker);
    CHECK(d->rc == MAKTABASAUTI_OK);
    if (d->rc != MAKTABASAUTI_OK || !pcm) return pcm;
    CHECK(maktabasauti_mod_desc.read(d->state, pcm, frames, &got) == MAKTABASAUTI_OK);
    CHECK(got == frames);
    return pcm;
}

static void test_tracker(void) {
    Dec d;
    uint8_t ones[8];
    uint8_t square[4] = {127, 0x80, 127, 0x80};
    uint8_t lead[8] = {0, 0, 0, 0, 127, 127, 127, 127};
    uint8_t head[8] = {127, 0, 0, 0, 50, 0, 0, 0};
    uint8_t only[20];
    uint8_t *funk, *b, *raw;
    int16_t *pcm;
    size_t n;
    Inst inst;
    Cell cell;
    char tag[8];
    memset(ones, 127, sizeof ones);
    memset(&inst, 0, sizeof inst);
    memset(&cell, 0, sizeof cell);

    inst = (Inst){1, 4, 0, 64, 0, 0, ones};
    cell = (Cell){0, 0, 0, 1, 428, 0, 0};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, &cell, 1);
    pcm = render_opt(&d, b, n, 23u, "pt");
    CHECK(pcm && pcm[11u * 2u] == 127 * 64 && pcm[16u * 2u] == 127 * 64);
    dec_close(&d); free(pcm); free(b);
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, &cell, 1);
    pcm = render_opt(&d, b, n, 23u, "protracker");
    CHECK(pcm && pcm[11u * 2u] == 127 * 64);
    dec_close(&d); free(pcm); free(b);
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, &cell, 1);
    pcm = render_opt(&d, b, n, 23u, "PT10");
    CHECK(pcm && pcm[11u * 2u] == 0 && pcm[16u * 2u] == 0 && pcm[22u * 2u] == 127 * 64);
    dec_close(&d); free(pcm); free(b);
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, &cell, 1);
    pcm = render_opt(&d, b, n, 23u, "protracker10");
    CHECK(pcm && pcm[16u * 2u] == 0 && pcm[22u * 2u] == 127 * 64);
    dec_close(&d); free(pcm); free(b);

    /* EF $F, petla 16 B od bajtu 16.  Takt 0 slyszy 40, takt 1 slyszy 60, nie 50. */
    funk = (uint8_t *)calloc(96u, 1u);
    memset(funk + 16, 40, 16u);
    memset(funk + 32, 50, 16u);
    memset(funk + 48, 60, 16u);
    inst = (Inst){1, 48, 0, 64, 8, 8, funk};
    cell = (Cell){0, 0, 0, 1, 428, 0x0E, 0xFF};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, &cell, 1);
    pcm = render_opt(&d, b, n, (size_t)TICK + 201u, "pt10");
    CHECK(pcm && pcm[200u * 2u] == 40 * 64);
    CHECK(pcm && pcm[((size_t)TICK + 200u) * 2u] == 60 * 64);
    dec_close(&d); free(pcm); free(b); free(funk);

    /* 32768 slow: koniec modulo 65536 jest zerem.  Okno zostaje na 40, nie schodzi na 60. */
    funk = (uint8_t *)calloc(65536u, 1u);
    CHECK(funk != NULL);
    memset(funk + 16, 40, 16u);
    memset(funk + 48, 60, 16u);
    inst = (Inst){1, 32768, 0, 64, 8, 8, funk};
    cell = (Cell){0, 0, 0, 1, 428, 0x0E, 0xFF};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, &cell, 1);
    pcm = render_opt(&d, b, n, (size_t)TICK + 201u, "pt10");
    CHECK(pcm && pcm[200u * 2u] == 40 * 64);
    CHECK(pcm && pcm[((size_t)TICK + 200u) * 2u] == 40 * 64);
    dec_close(&d); free(pcm); free(b); free(funk);

    /* 32808 slow: limit owija sie do 80 bajtow.  Takt 2 slysz 50, takt 3 wraca na 40, nie na 60. */
    funk = (uint8_t *)calloc(65616u, 1u);
    CHECK(funk != NULL);
    memset(funk + 16, 40, 16u);
    memset(funk + 64, 50, 16u);
    memset(funk + 80, 60, 16u);
    inst = (Inst){1, 32808, 0, 64, 8, 8, funk};
    cell = (Cell){0, 0, 0, 1, 428, 0x0E, 0xFF};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, &cell, 1);
    pcm = render_opt(&d, b, n, 3u * TICK + 201u, "pt10");
    CHECK(pcm && pcm[(2u * TICK + 200u) * 2u] == 50 * 64);
    CHECK(pcm && pcm[(3u * TICK + 200u) * 2u] == 40 * 64);
    dec_close(&d); free(pcm); free(b); free(funk);

    raw = (uint8_t *)(ones);
    inst = (Inst){1, 1, 0, 64, 0, 0, raw};
    cell = (Cell){0, 0, 0, 1, 428, 0, 0};
    b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
    b[471] = 124;
    pcm = render_opt(&d, b, n, 1u, "ultimate");
    CHECK(pcm && pcm[0] == 127 * 64);
    dec_close(&d); free(pcm); free(b);

    inst = (Inst){1, 4, 0, 64, 4, 2, lead};
    cell = (Cell){0, 0, 0, 1, 428, 0, 0};
    b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
    b[471] = 124;
    pcm = render_opt(&d, b, n, 1u, "ust");
    CHECK(pcm && pcm[0] == 127 * 64);
    dec_close(&d); free(pcm); free(b);

    memset(only, 50, sizeof only);
    memset(only + 10, 127, 4u);
    inst = (Inst){1, 10, 0, 64, 10, 2, only};
    b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
    b[471] = 124;
    pcm = render_opt(&d, b, n, 1u, "ust");
    CHECK(pcm && pcm[0] == 127 * 64);
    dec_close(&d); free(pcm); free(b);

    inst = (Inst){1, 4, 0, 64, 4, 0, head};
    b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
    b[471] = 124;
    pcm = render_opt(&d, b, n, 1u, "ust");
    CHECK(pcm && pcm[0] == 127 * 64);
    dec_close(&d); free(pcm); free(b);

    inst = (Inst){1, 2, 7, 64, 0, 2, square};
    cell = (Cell){0, 0, 0, 1, 428, 0, 0};
    b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
    b[471] = 124;
    dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "ust");
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(maktabasauti_mod_desc.get_tag(d.state, "finetune.1", tag, sizeof tag, NULL) == MAKTABASAUTI_OK);
    CHECK(strcmp(tag, "7") == 0);
    dec_close(&d);
    pcm = render_opt(&d, b, n, TICK, "ust");
    expect_period(pcm, 0, 428);
    dec_close(&d); free(pcm); free(b);

    cell = (Cell){0, 0, 0, 1, 856, 1, 0xC0};
    b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
    b[471] = 124;
    pcm = render_opt(&d, b, n, 3u * TICK, "ust");
    expect_period(pcm, 0, 856);
    expect_period(pcm, 1, 428);
    expect_period(pcm, 2, 856);
    dec_close(&d); free(pcm); free(b);

    cell = (Cell){0, 0, 0, 1, 428, 0, 0x0C};
    b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
    b[471] = 124;
    pcm = render_opt(&d, b, n, 2u * TICK, "ust");
    expect_period(pcm, 1, 428);
    dec_close(&d); free(pcm); free(b);

    cell = (Cell){0, 0, 0, 1, 428, 2, 0x03};
    b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
    b[471] = 124;
    pcm = render_opt(&d, b, n, 6u * TICK, "ust");
    expect_period(pcm, 0, 428);
    expect_period(pcm, 5, 413);
    dec_close(&d); free(pcm); free(b);

    cell = (Cell){0, 0, 0, 1, 428, 2, 0x20};
    b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
    b[471] = 124;
    pcm = render_opt(&d, b, n, 6u * TICK, "ust");
    expect_period(pcm, 5, 438);
    dec_close(&d); free(pcm); free(b);

    cell = (Cell){0, 0, 0, 1, 428, 2, 0x12};
    b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
    b[471] = 124;
    pcm = render_opt(&d, b, n, 6u * TICK, "ust");
    expect_period(pcm, 5, 428);
    dec_close(&d); free(pcm); free(b);

    cell = (Cell){0, 0, 0, 1, 428, 0x0C, 10};
    b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
    b[471] = 124;
    pcm = render_opt(&d, b, n, 1u, "ust");
    CHECK(pcm && pcm[0] == 127 * 64);
    dec_close(&d); free(pcm); free(b);

    cell = (Cell){0, 0, 0, 1, 428, 0x0F, 32};
    b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
    b[471] = 124;
    dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "ust");
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == 338688u);
    dec_close(&d); free(b);

    cell = (Cell){0, 0, 0, 1, 428, 0, 0};
    b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
    b[471] = 120;
    dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "ust");
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == 349884u);
    dec_close(&d); free(b);

    inst = (Inst){1, 2, 0, 64, 0, 2, square};
    cell = (Cell){0, 0, 0, 1, 428, 0, 0};
    b = build(&n, "M.K.", 1, 120, NULL, &inst, 1, &cell, 1);
    dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "ust");
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == 338688u);
    dec_close(&d); free(b);

    inst = (Inst){1, 5000, 0, 64, 0, 0, NULL};
    b = build15(&n, 1, NULL, &inst, 1, NULL, 0);
    dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "ust");
    CHECK(d.rc == MAKTABASAUTI_E_UNSUPPORTED_FEATURE);
    dec_close(&d); free(b);

    {
        uint8_t flat[4];
        Cell rows[2];
        memset(flat, 127, sizeof flat);
        inst = (Inst){1, 2, 0, 64, 0, 2, square};
        cell = (Cell){0, 0, 0, 1, 428, 1, 0x0F};
        b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
        b[471] = 124;
        pcm = render_opt(&d, b, n, 12u * TICK, "st2");
        expect_period(pcm, 5, 353);
        dec_close(&d); free(pcm); free(b);

        rows[0] = (Cell){0, 0, 0, 1, 428, 1, 0x0F};
        rows[1] = (Cell){0, 1, 0, 0, 0, 1, 0x00};
        b = build15(&n, 1, NULL, &inst, 1, rows, 2);
        b[471] = 124;
        pcm = render_opt(&d, b, n, 12u * TICK, "soundtracker2");
        expect_period(pcm, 11, 353);
        dec_close(&d); free(pcm); free(b);

        inst.volume = 32;
        inst.data = flat;
        cell = (Cell){0, 0, 0, 1, 428, 0x0D, 0x10};
        b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
        b[471] = 124;
        pcm = render_opt(&d, b, n, (size_t)TICK + 1u, "st2");
        CHECK(pcm && pcm[0] == 127 * 32 && pcm[(size_t)TICK * 2u] == 127 * 33);
        dec_close(&d); free(pcm); free(b);

        inst.volume = 40;
        rows[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0x01};
        rows[1] = (Cell){0, 1, 0, 0, 0, 0, 0x01};
        b = build15(&n, 1, NULL, &inst, 1, rows, 2);
        b[471] = 124;
        pcm = render_opt(&d, b, n, 8u * TICK, "st2");
        CHECK(pcm && pcm[5u * TICK * 2u] == 127 * 35);
        CHECK(pcm && pcm[7u * TICK * 2u] == 127 * 34);
        dec_close(&d); free(pcm); free(b);

        rows[1] = (Cell){0, 1, 0, 0, 0, 1, 0x00};
        b = build15(&n, 1, NULL, &inst, 1, rows, 2);
        b[471] = 124;
        pcm = render_opt(&d, b, n, 8u * TICK, "st2");
        CHECK(pcm && pcm[7u * TICK * 2u] == 127 * 35);
        dec_close(&d); free(pcm); free(b);

        inst.volume = 32;
        cell = (Cell){0, 0, 0, 1, 428, 0x0A, 0x10};
        b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
        b[471] = 124;
        pcm = render_opt(&d, b, n, (size_t)TICK + 1u, "st2");
        CHECK(pcm && pcm[(size_t)TICK * 2u] == 127 * 32);
        dec_close(&d); free(pcm); free(b);

        inst = (Inst){1, 2, 0, 64, 0, 2, square};
        cell = (Cell){0, 0, 0, 1, 428, 0x0F, 0x14};
        b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
        b[471] = 124;
        dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "st2");
        CHECK(d.rc == MAKTABASAUTI_OK);
        CHECK(d.info.total_frames == 225792u);
        dec_close(&d); free(b);

        cell.param = 0x20;
        b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
        b[471] = 124;
        dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "st2");
        CHECK(d.rc == MAKTABASAUTI_OK);
        CHECK(d.info.total_frames == 338688u);
        dec_close(&d); free(b);

        cell = (Cell){0, 0, 0, 0, 0, 0x0D, 0x16};
        b = build15(&n, 2, NULL, &inst, 1, &cell, 1);
        b[471] = 124;
        dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "master");
        CHECK(d.rc == MAKTABASAUTI_OK);
        CHECK(d.info.total_frames == 10584u);
        dec_close(&d); free(b);

        cell = (Cell){0, 0, 0, 1, 428, 0x0E, 0x1F};
        b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
        b[471] = 124;
        pcm = render_opt(&d, b, n, 2u * TICK, "st9");
        expect_period(pcm, 0, 428);
        expect_period(pcm, 1, 428);
        dec_close(&d); free(pcm); free(b);

        b = build(&n, "M.K.", 1, 120, NULL, &inst, 1, &cell, 1);
        dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "st2");
        CHECK(d.rc == MAKTABASAUTI_OK);
        CHECK(d.info.total_frames == 338688u);
        dec_close(&d); free(b);

        inst = (Inst){1, 5000, 0, 64, 0, 0, NULL};
        b = build15(&n, 1, NULL, &inst, 1, NULL, 0);
        dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "st2");
        CHECK(d.rc == MAKTABASAUTI_E_UNSUPPORTED_FEATURE);
        dec_close(&d); free(b);

        b = build15(&n, 1, NULL, &inst, 1, NULL, 0);
        dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "st9");
        CHECK(d.rc == MAKTABASAUTI_OK);
        dec_close(&d); free(b);

        inst.words = 16385;
        b = build15(&n, 1, NULL, &inst, 1, NULL, 0);
        dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "st9");
        CHECK(d.rc == MAKTABASAUTI_E_UNSUPPORTED_FEATURE);
        dec_close(&d); free(b);
    }

    {
        uint8_t flat[4];
        uint8_t volw[4] = {0x00, 0x20, 0x00, 0x20};
        uint8_t perw[4] = {0x03, 0x58, 0x03, 0x58};
        uint8_t bytes[8] = {0, 0, 0, 0, 127, 127, 127, 127};
        uint8_t worded[8] = {127, 127, 127, 127, 50, 50, 50, 50};
        uint8_t sq[4] = {127, 0x80, 127, 0x80};
        Inst two[2];
        Cell rows[3];
        memset(flat, 127, sizeof flat);

        inst = (Inst){1, 2, 0, 64, 0, 2, sq};
        cell = (Cell){0, 0, 0, 1, 428, 0, 0};
        b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
        b[471] = 120;
        dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "tjc");
        CHECK(d.rc == MAKTABASAUTI_OK);
        CHECK(d.info.total_frames == 282240u);
        dec_close(&d);
        dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "jungle");
        CHECK(d.rc == MAKTABASAUTI_OK);
        CHECK(d.info.total_frames == 282240u);
        dec_close(&d); free(b);

        cell = (Cell){0, 0, 0, 1, 428, 0x0F, 0x0F};
        b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
        b[471] = 120;
        dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "tjc");
        CHECK(d.rc == MAKTABASAUTI_OK);
        CHECK(d.info.total_frames == 282240u);
        dec_close(&d); free(b);

        cell = (Cell){0, 0, 0, 1, 428, 1, 0x0F};
        b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
        pcm = render_opt(&d, b, n, 5u * TICK, "tjc");
        expect_period(pcm, 0, 413);
        expect_period(pcm, 4, 353);
        dec_close(&d); free(pcm); free(b);

        cell = (Cell){0, 0, 0, 1, 856, 0, 0xC0};
        b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
        pcm = render_opt(&d, b, n, 3u * TICK, "tjc");
        expect_period(pcm, 0, 428);
        expect_period(pcm, 1, 856);
        expect_period(pcm, 2, 856);
        dec_close(&d); free(pcm); free(b);

        inst.volume = 40;
        inst.data = flat;
        cell = (Cell){0, 0, 0, 1, 428, 0x0D, 0x13};
        b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
        pcm = render_opt(&d, b, n, (size_t)TICK + 1u, "tjc");
        CHECK(pcm && pcm[0] == 127 * 38 && pcm[(size_t)TICK * 2u] == 127 * 36);
        dec_close(&d); free(pcm); free(b);

        inst.volume = 32;
        rows[0] = (Cell){0, 0, 0, 1, 428, 0x0E, 0x10};
        rows[1] = (Cell){0, 1, 0, 0, 0, 0, 0x01};
        rows[2] = (Cell){0, 2, 0, 0, 0, 0, 0x00};
        b = build15(&n, 1, NULL, &inst, 1, rows, 3);
        pcm = render_opt(&d, b, n, 15u * TICK, "tjc");
        CHECK(pcm && pcm[0] == 127 * 33);
        CHECK(pcm && pcm[4u * TICK * 2u] == 127 * 37);
        CHECK(pcm && pcm[5u * TICK * 2u] == 127 * 38);
        CHECK(pcm && pcm[9u * TICK * 2u] == 127 * 42);
        CHECK(pcm && pcm[14u * TICK * 2u] == 127 * 42);
        dec_close(&d); free(pcm); free(b);

        two[0] = (Inst){1, 2, 0, 64, 0, 2, volw};
        two[1] = (Inst){2, 2, 0, 64, 0, 2, flat};
        rows[0] = (Cell){0, 0, 0, 1, 428, 3, 0};
        rows[1] = (Cell){0, 0, 1, 2, 428, 0, 0};
        b = build15(&n, 1, NULL, two, 2, rows, 2);
        pcm = render_opt(&d, b, n, 1u, "tjc");
        CHECK(pcm && pcm[0] == 0 && pcm[1] == 127 * 32);
        dec_close(&d); free(pcm); free(b);

        rows[0].effect = 6;
        b = build15(&n, 1, NULL, two, 2, rows, 2);
        pcm = render_opt(&d, b, n, 1u, "jungle");
        CHECK(pcm && pcm[0] == 0 && pcm[1] == 127 * 32);
        dec_close(&d); free(pcm); free(b);

        rows[0] = (Cell){0, 0, 2, 1, 428, 4, 0};
        rows[1] = (Cell){0, 0, 3, 2, 428, 0, 0};
        two[0].data = perw;
        two[1].data = sq;
        b = build15(&n, 1, NULL, two, 2, rows, 2);
        pcm = render_opt(&d, b, n, TICK, "tjc");
        expect_period(pcm, 0, 856);
        dec_close(&d); free(pcm); free(b);

        two[0].data = flat;
        two[1].data = flat;
        rows[0] = (Cell){0, 0, 2, 2, 428, 0, 0};
        rows[1] = (Cell){0, 0, 3, 1, 428, 3, 0};
        b = build15(&n, 1, NULL, two, 2, rows, 2);
        pcm = render_opt(&d, b, n, 1u, "tjc");
        CHECK(pcm && pcm[0] == 0 && pcm[1] == 127 * 64);
        dec_close(&d); free(pcm); free(b);

        inst = (Inst){1, 5000, 0, 64, 0, 0, NULL};
        b = build15(&n, 1, NULL, &inst, 1, NULL, 0);
        dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "tjc");
        CHECK(d.rc == MAKTABASAUTI_E_UNSUPPORTED_FEATURE);
        dec_close(&d); free(b);

        rows[0] = (Cell){0, 0, 0, 1, 428, 3, 0};
        rows[1] = (Cell){0, 0, 1, 2, 428, 0, 0};
        two[0].data = volw;
        two[1].data = flat;
        b = build15(&n, 1, NULL, two, 2, rows, 2);
        b[471] = 124;
        pcm = render_opt(&d, b, n, 1u, "st2");
        CHECK(pcm && pcm[1] == 127 * 64);
        dec_close(&d); free(pcm); free(b);

        inst = (Inst){1, 2, 0, 32, 0, 2, flat};
        cell = (Cell){0, 0, 0, 1, 428, 0x0A, 0x10};
        b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, &cell, 1);
        pcm = render_opt(&d, b, n, (size_t)TICK + 1u, "st23");
        CHECK(pcm && pcm[0] == 127 * 32 && pcm[(size_t)TICK * 2u] == 127 * 33);
        dec_close(&d); free(pcm); free(b);

        cell = (Cell){0, 0, 0, 0, 0, 0x0D, 0x16};
        b = build15(&n, 2, NULL, &inst, 1, &cell, 1);
        dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "st23");
        CHECK(d.rc == MAKTABASAUTI_OK);
        CHECK(d.info.total_frames == 10584u);
        dec_close(&d); free(b);

        cell = (Cell){0, 0, 0, 1, 428, 0x0F, 0x14};
        b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
        b[471] = 120;
        dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "st24");
        CHECK(d.rc == MAKTABASAUTI_OK);
        CHECK(d.info.total_frames == 225792u);
        dec_close(&d); free(b);

        cell = (Cell){0, 0, 0, 1, 428, 0, 0};
        b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
        b[471] = 120;
        dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "st23");
        CHECK(d.rc == MAKTABASAUTI_OK);
        CHECK(d.info.total_frames == 338688u);
        dec_close(&d); free(b);

        inst = (Inst){1, 4, 0, 64, 4, 2, bytes};
        cell = (Cell){0, 0, 0, 1, 428, 0, 0};
        b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
        pcm = render_opt(&d, b, n, 1u, "st23");
        CHECK(pcm && pcm[0] == 127 * 64);
        dec_close(&d); free(pcm); free(b);

        inst = (Inst){1, 2, 7, 64, 0, 2, sq};
        cell = (Cell){0, 0, 0, 1, 428, 0, 0};
        b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, &cell, 1);
        dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "st23");
        CHECK(d.rc == MAKTABASAUTI_OK);
        CHECK(maktabasauti_mod_desc.get_tag(d.state, "finetune.1", tag, sizeof tag, NULL) == MAKTABASAUTI_OK);
        CHECK(strcmp(tag, "7") == 0);
        dec_close(&d);
        pcm = render_opt(&d, b, n, TICK, "st23");
        expect_period(pcm, 0, 428);
        dec_close(&d); free(pcm); free(b);

        inst = (Inst){1, 2, 0, 64, 0, 2, sq};
        rows[0] = (Cell){0, 0, 0, 1, 428, 1, 0x0F};
        rows[1] = (Cell){0, 1, 0, 0, 0, 0, 0x0C};
        b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, rows, 2);
        pcm = render_opt(&d, b, n, 9u * TICK, "st23");
        expect_period(pcm, 6, 428);
        expect_period(pcm, 8, 214);
        dec_close(&d); free(pcm); free(b);

        rows[0] = (Cell){0, 0, 0, 1, 856, 0x0F, 0x08};
        rows[1] = (Cell){0, 1, 0, 0, 0, 0, 0xC0};
        b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, rows, 2);
        pcm = render_opt(&d, b, n, 16u * TICK, "st23");
        expect_period(pcm, 9, 428);
        expect_period(pcm, 15, 856);
        dec_close(&d); free(pcm); free(b);

        rows[0] = (Cell){0, 0, 0, 1, 428, 0, 0};
        rows[1] = (Cell){0, 1, 0, 1, 856, 3, 0x0F};
        b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, rows, 2);
        pcm = render_opt(&d, b, n, 8u * TICK, "st23");
        expect_period(pcm, 6, 856);
        expect_period(pcm, 7, 856);
        dec_close(&d); free(pcm); free(b);

        inst = (Inst){1, 16385, 0, 64, 0, 0, NULL};
        b = build15(&n, 1, NULL, &inst, 1, NULL, 0);
        dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "st23");
        CHECK(d.rc == MAKTABASAUTI_E_UNSUPPORTED_FEATURE);
        dec_close(&d); free(b);
        inst.words = 16384;
        b = build15(&n, 1, NULL, &inst, 1, NULL, 0);
        dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "st23");
        CHECK(d.rc == MAKTABASAUTI_OK);
        dec_close(&d); free(b);

        inst = (Inst){1, 4, 0, 64, 2, 2, worded};
        cell = (Cell){0, 0, 0, 1, 428, 0, 0};
        b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
        b[471] = 120;
        pcm = render_opt(&d, b, n, 23u, "st25");
        CHECK(pcm && pcm[0] == 127 * 64 && pcm[22u * 2u] == 50 * 64);
        dec_close(&d); free(pcm);
        dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "st25");
        CHECK(d.rc == MAKTABASAUTI_OK);
        CHECK(d.info.total_frames == 338688u);
        dec_close(&d); free(b);

        inst = (Inst){1, 2, 0, 64, 0, 2, sq};
        rows[0] = (Cell){0, 0, 0, 1, 428, 0, 0};
        rows[1] = (Cell){0, 1, 0, 1, 856, 3, 0x0F};
        b = build15(&n, 1, NULL, &inst, 1, rows, 2);
        pcm = render_opt(&d, b, n, 8u * TICK, "st25");
        expect_period(pcm, 6, 428);
        expect_period(pcm, 7, 443);
        dec_close(&d); free(pcm); free(b);

        inst = (Inst){1, 2, 0, 32, 0, 2, flat};
        cell = (Cell){0, 0, 0, 1, 428, 5, 0x10};
        b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
        pcm = render_opt(&d, b, n, (size_t)TICK + 1u, "st25");
        CHECK(pcm && pcm[(size_t)TICK * 2u] == 127 * 32);
        dec_close(&d); free(pcm); free(b);

        inst = (Inst){1, 2, 0, 64, 0, 2, sq};
        cell = (Cell){0, 0, 0, 1, 428, 0x0F, 0x28};
        b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
        b[471] = 120;
        dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "st25");
        CHECK(d.rc == MAKTABASAUTI_OK);
        CHECK(d.info.total_frames == 451584u);
        dec_close(&d); free(b);

        cell.param = 0x20;
        b = build15(&n, 1, NULL, &inst, 1, &cell, 1);
        b[471] = 120;
        dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "st25");
        CHECK(d.rc == MAKTABASAUTI_OK);
        CHECK(d.info.total_frames == 338688u);
        dec_close(&d); free(b);

        inst = (Inst){1, 32769, 0, 64, 0, 0, NULL};
        b = build15(&n, 1, NULL, &inst, 1, NULL, 0);
        dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "st25");
        CHECK(d.rc == MAKTABASAUTI_E_UNSUPPORTED_FEATURE);
        dec_close(&d); free(b);
        inst.words = 32768;
        b = build15(&n, 1, NULL, &inst, 1, NULL, 0);
        dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "st25");
        CHECK(d.rc == MAKTABASAUTI_OK);
        dec_close(&d); free(b);
    }

    inst = (Inst){1, 2, 0, 64, 0, 2, square};
    cell = (Cell){0, 0, 0, 1, 428, 0, 0};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, &cell, 1);
    dec_open_ex(&d, b, n, "44100", NULL, NULL, NULL, "soundtracker");
    CHECK(d.rc == MAKTABASAUTI_E_INVALID_ARGUMENT);
    dec_close(&d); free(b);
    CHECK(d.bag.heap.live == 0);
}

static void test_st26(void) {
    static const uint8_t id4[4] = {0, 1, 2, 3};
    uint8_t flat[8];
    uint8_t worded[8] = {127, 127, 127, 127, 50, 50, 50, 50};
    uint8_t square[6] = {0, 0, 127, 0x80, 127, 0x80};
    uint8_t far[258];
    uint8_t low[4] = {50, 50, 50, 50};
    uint8_t *b, *hdr;
    int16_t *pcm;
    size_t n = 0;
    Dec d;
    Inst inst, insts[2];
    Cell cell, cells[2];
    char tag[8];
    memset(flat, 127, sizeof flat);
    memset(far, 0, sizeof far);
    far[0] = 50;
    far[256] = 127;
    memset(&inst, 0, sizeof inst);
    memset(&cell, 0, sizeof cell);

    hdr = (uint8_t *)calloc(1, 1468u);
    CHECK(hdr != NULL);
    hdr[950] = 1;
    hdr[951] = 1;
    memcpy(hdr + 1464, "MTN", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, 1468u, NULL) == 70);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, 1467u, NULL) == 0);
    dec_open(&d, hdr, 1468u, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_E_TRUNCATED);
    dec_close(&d);
    memcpy(hdr + 1464, "MNT", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, 1468u, NULL) == 0);
    memcpy(hdr + 1464, "IT10", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, 1468u, NULL) == 70);
    hdr[951] = 0;
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, 1468u, NULL) == 0);
    hdr[951] = 1;
    hdr[950] = 0;
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, 1468u, NULL) == 0);
    hdr[950] = 200;
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, 1468u, NULL) == 0);
    hdr[950] = 1;
    hdr[45] = 65;
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, 1468u, NULL) == 0);
    hdr[45] = 0;
    hdr[952] = 1;
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, hdr, 1468u, NULL) == 0);
    CHECK(maktabasauti_mod_wow_desc.probe(&maktabasauti_mod_wow_desc, hdr, 1468u, "a.wow") == 0);
    free(hdr);

    inst = (Inst){1, 2, 0, 64, 0, 2, flat};
    cell = (Cell){0, 0, 0, 1, 428, 0, 0};
    b = build_st26(&n, "MTN", 1, 4, id4, &inst, 1, &cell, 1);
    b[1080] = 'A'; b[1081] = 'B'; b[1082] = 'C'; b[1083] = 'D';
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(strcmp(d.info.codec_profile, "MTN") == 0);
    CHECK(maktabasauti_mod_desc.get_tag(d.state, "restart", tag, sizeof tag, NULL) == MAKTABASAUTI_OK);
    CHECK(strcmp(tag, "0") == 0);
    dec_close(&d);
    pcm = render_n(&d, b, n, 1u);
    CHECK(pcm && pcm[0] == 127 * 64 && pcm[1] == 0);
    dec_close(&d); free(pcm); free(b);

    b = build_st26(&n, "IT10", 1, 4, id4, &inst, 1, &cell, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(strcmp(d.info.codec_profile, "IT10") == 0);
    dec_close(&d);
    pcm = render_n(&d, b, n, 1u);
    CHECK(pcm && pcm[0] == 127 * 64 && pcm[1] == 0);
    dec_close(&d); free(pcm); free(b);

    insts[0] = (Inst){1, 2, 0, 64, 0, 2, flat};
    insts[1] = (Inst){2, 2, 0, 64, 0, 2, low};
    cells[0] = (Cell){0, 0, 0, 1, 428, 0, 0};
    cells[1] = (Cell){1, 0, 0, 2, 428, 0, 0};
    b = build_st26(&n, "MTN", 1, 4, id4, insts, 2, cells, 2);
    pcm = render_n(&d, b, n, 1u);
    CHECK(pcm && pcm[0] == 127 * 64 && pcm[1] == 50 * 64);
    dec_close(&d); free(pcm); free(b);

    inst = (Inst){1, 4, 0, 64, 2, 2, worded};
    cell = (Cell){0, 0, 0, 1, 428, 0, 0};
    b = build_st26(&n, "MTN", 1, 4, id4, &inst, 1, &cell, 1);
    pcm = render_n(&d, b, n, 23u);
    CHECK(pcm && pcm[0] == 127 * 64 && pcm[22u * 2u] == 50 * 64);
    dec_close(&d); free(pcm); free(b);

    inst = (Inst){1, 3, 7, 64, 1, 2, square};
    cell = (Cell){0, 0, 0, 1, 856, 0, 0};
    b = build_st26(&n, "MTN", 1, 4, id4, &inst, 1, &cell, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(maktabasauti_mod_desc.get_tag(d.state, "finetune.1", tag, sizeof tag, NULL) == MAKTABASAUTI_OK);
    CHECK(strcmp(tag, "7") == 0);
    dec_close(&d);
    pcm = render_n(&d, b, n, TICK);
    expect_period(pcm, 0, 856);
    dec_close(&d); free(pcm);
    pcm = render_opt(&d, b, n, TICK, "st25");
    CHECK(strcmp(d.info.codec_profile, "MTN") == 0);
    expect_period(pcm, 0, 856);
    dec_close(&d); free(pcm); free(b);

    inst = (Inst){1, 3, 0, 64, 1, 2, square};
    cells[0] = (Cell){0, 0, 0, 1, 856, 0, 0xC0};
    b = build_st26(&n, "MTN", 1, 4, id4, &inst, 1, cells, 1);
    pcm = render_n(&d, b, n, 3u * TICK);
    expect_period(pcm, 0, 856);
    expect_period(pcm, 1, 428);
    expect_period(pcm, 2, 856);
    dec_close(&d); free(pcm); free(b);

    cell = (Cell){0, 0, 0, 1, 428, 1, 0x0F};
    b = build_st26(&n, "MTN", 1, 4, id4, &inst, 1, &cell, 1);
    pcm = render_n(&d, b, n, 6u * TICK);
    expect_period(pcm, 0, 428);
    expect_period(pcm, 5, 353);
    dec_close(&d); free(pcm); free(b);

    cell = (Cell){0, 0, 0, 1, 428, 0x0E, 0x1F};
    b = build_st26(&n, "MTN", 1, 4, id4, &inst, 1, &cell, 1);
    pcm = render_n(&d, b, n, 2u * TICK);
    expect_period(pcm, 0, 428);
    expect_period(pcm, 1, 428);
    dec_close(&d); free(pcm); free(b);

    inst = (Inst){1, 2, 0, 64, 0, 2, flat};
    cell = (Cell){0, 0, 0, 1, 428, 0x0E, 0x00};
    b = build_st26(&n, "MTN", 1, 4, id4, &inst, 1, &cell, 1);
    pcm = render_n(&d, b, n, (size_t)TICK + 1u);
    CHECK(pcm && pcm[0] < 127 * 32);
    CHECK(pcm[(size_t)TICK * 2u] >= 127 * 64 - 1 && pcm[(size_t)TICK * 2u] <= 127 * 64 + 1);
    dec_close(&d); free(pcm); free(b);

    cell.param = 0x10;
    b = build_st26(&n, "MTN", 1, 4, id4, &inst, 1, &cell, 1);
    pcm = render_n(&d, b, n, 1u);
    CHECK(pcm && pcm[0] == 127 * 64 && pcm[1] == 0);
    dec_close(&d); free(pcm); free(b);

    cell = (Cell){0, 0, 0, 1, 428, 0x0E, 0x0F};
    b = build_st26(&n, "MTN", 1, 4, id4, &inst, 1, &cell, 1);
    pcm = render_opt(&d, b, n, 1u, "st25");
    CHECK(pcm && pcm[0] == 127 * 64 && pcm[1] == 0);
    dec_close(&d); free(pcm); free(b);

    inst.volume = 32;
    cell = (Cell){0, 0, 0, 1, 428, 5, 0x10};
    b = build_st26(&n, "MTN", 1, 4, id4, &inst, 1, &cell, 1);
    pcm = render_n(&d, b, n, (size_t)TICK + 1u);
    CHECK(pcm && pcm[(size_t)TICK * 2u] == 127 * 32);
    dec_close(&d); free(pcm); free(b);

    cell = (Cell){0, 0, 0, 1, 428, 7, 0x18};
    b = build_st26(&n, "MTN", 1, 4, id4, &inst, 1, &cell, 1);
    pcm = render_n(&d, b, n, 2u * TICK + 1u);
    CHECK(pcm && pcm[2u * TICK * 2u] == 127 * 32);
    dec_close(&d); free(pcm); free(b);

    inst.volume = 64;
    cell = (Cell){0, 0, 0, 1, 428, 8, 255};
    b = build_st26(&n, "MTN", 1, 4, id4, &inst, 1, &cell, 1);
    pcm = render_n(&d, b, n, 1u);
    CHECK(pcm && pcm[0] == 127 * 64 && pcm[1] == 0);
    dec_close(&d); free(pcm); free(b);

    cell = (Cell){0, 0, 0, 1, 428, 0x0C, 10};
    b = build_st26(&n, "MTN", 1, 4, id4, &inst, 1, &cell, 1);
    pcm = render_n(&d, b, n, 1u);
    CHECK(pcm && pcm[0] == 127 * 10);
    dec_close(&d); free(pcm); free(b);

    inst = (Inst){1, 129, 0, 64, 0, 1, far};
    cell = (Cell){0, 0, 0, 1, 428, 9, 1};
    b = build_st26(&n, "MTN", 1, 4, id4, &inst, 1, &cell, 1);
    pcm = render_n(&d, b, n, 1u);
    CHECK(pcm && pcm[0] == 50 * 64);
    dec_close(&d); free(pcm); free(b);

    inst = (Inst){1, 2, 0, 64, 0, 2, flat};
    cell = (Cell){0, 0, 0, 1, 428, 0x0F, 0x28};
    b = build_st26(&n, "MTN", 1, 1, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == 451584u);
    dec_close(&d); free(b);

    cell.param = 0x00;
    b = build_st26(&n, "MTN", 1, 1, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == 338688u);
    CHECK(d.info.total_frames != MAKTABASAUTI_UNKNOWN_LENGTH);
    dec_close(&d); free(b);

    cell.param = 0x20;
    b = build_st26(&n, "MTN", 1, 1, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == 338688u);
    dec_close(&d); free(b);

    cell = (Cell){0, 0, 0, 0, 0, 0x0B, 0};
    b = build_st26(&n, "MTN", 2, 1, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == 6u * TICK);
    dec_close(&d); free(b);

    cell = (Cell){0, 0, 0, 0, 0, 0x0D, 0x16};
    b = build_st26(&n, "MTN", 2, 1, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == 49u * 6u * TICK);
    dec_close(&d); free(b);

    inst = (Inst){1, 2, 0, 64, 0, 2, flat};
    cell = (Cell){0, 0, 0, 1, 428, 0, 0};
    b = build_st26(&n, "MTN", 1, 1, NULL, &inst, 1, &cell, 1);
    b[956] = 255;
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    dec_close(&d);
    b[952] = 1;
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, b, n, NULL) == 0);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_E_UNSUPPORTED_FORMAT);
    dec_close(&d); free(b);

    cell = (Cell){0, 0, 0, 32, 428, 0, 0};
    b = build_st26(&n, "MTN", 1, 1, NULL, &inst, 1, &cell, 1);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_E_CORRUPT_DATA);
    dec_close(&d); free(b);

    inst = (Inst){1, 32769, 0, 64, 0, 0, NULL};
    b = build_st26(&n, "MTN", 1, 1, NULL, &inst, 1, NULL, 0);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_OK);
    CHECK(d.info.total_frames == 338688u);
    dec_close(&d); free(b);

    /* 1CHN ma krotki pattern, wiec bajty MTN leza w probce, nie w komorce.
     * Znany magik na 1080 wygrywa: prefiks ProTrackera zostaje, profil nie jest MTN. */
    {
        uint8_t *wide = (uint8_t *)calloc(160u, 1u);
        CHECK(wide != NULL);
        memset(wide, 127, 160u);
        inst = (Inst){1, 80, 0, 64, 0, 1, wide};
        cell = (Cell){0, 0, 0, 1, 428, 0, 0};
        b = build(&n, "1CHN", 1, 1, NULL, &inst, 1, &cell, 1);
        memcpy(b + 1464, "MTN", 4u);
        CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, b, n, NULL) == 70);
        pcm = render_n(&d, b, n, (size_t)CLEAR_AT + 1u);
        CHECK(strcmp(d.info.codec_profile, "1CHN") == 0);
        CHECK(pcm && pcm[0] == 0 && pcm[(size_t)CLEAR_AT * 2u] == 127 * 64);
        dec_close(&d); free(pcm); free(b); free(wide);
    }
    /* Te same cztery bajty w komorce M.K. to probka 68.  Plik jest uszkodzony, nie staje sie MTN. */
    inst = (Inst){1, 2, 0, 64, 0, 2, flat};
    cell = (Cell){0, 0, 0, 1, 428, 0, 0};
    b = build(&n, "M.K.", 1, 1, NULL, &inst, 1, &cell, 1);
    memcpy(b + 1464, "MTN", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, b, n, NULL) == 70);
    dec_open(&d, b, n, "44100", NULL);
    CHECK(d.rc == MAKTABASAUTI_E_CORRUPT_DATA);
    dec_close(&d); free(b);
    CHECK(d.bag.heap.live == 0);
}

/* Pollak: niedokonczony cel 3xx przezywa inna nute.  Cel skasowany po dojsciu nie sciaga dalej. */
static void test_tone_target(void) {
    uint8_t square[6] = {0, 0, 127, 0x80, 127, 0x80};
    Inst inst;
    Cell cells[4];
    uint8_t *b;
    size_t n = 0;
    Dec d;
    int16_t *pcm;
    memset(&inst, 0, sizeof inst);
    inst.index = 1; inst.words = 3; inst.volume = 64; inst.loop_w = 1; inst.loop_l = 2; inst.data = square;

    /* 428 schodzi o 1 przez piec taktow, do 423, nie do 214.  Nuta 856 zostawia cel.  Krok 200 daje 656. */
    cells[0] = (Cell){0, 0, 0, 1, 428, 0, 0};
    cells[1] = (Cell){0, 1, 0, 0, 214, 0x03, 1};
    cells[2] = (Cell){0, 2, 0, 0, 856, 0, 0};
    cells[3] = (Cell){0, 3, 0, 0, 0, 0x03, 200};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 4);
    pcm = render_n(&d, b, n, 20u * TICK);
    expect_period(pcm, 11, 423);
    expect_period(pcm, 12, 856);
    expect_period(pcm, 18, 856);
    expect_period(pcm, 19, 656);
    dec_close(&d); free(pcm); free(b);

    /* Krok 255 dochodzi do 214 od razu i kasuje cel.  Potem 3xx bez nuty zostaje na 856. */
    cells[1] = (Cell){0, 1, 0, 0, 214, 0x03, 255};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 4);
    pcm = render_n(&d, b, n, 20u * TICK);
    expect_period(pcm, 6, 428);
    expect_period(pcm, 7, 214);
    expect_period(pcm, 19, 856);
    dec_close(&d); free(pcm); free(b);

    /* 5xy przy grajacym glosie ustawia ten sam cel.  Predkosc 1 zostaje z pierwszego 3xx. */
    cells[0] = (Cell){0, 0, 0, 1, 428, 0x03, 1};
    cells[1] = (Cell){0, 1, 0, 0, 214, 0x05, 0};
    cells[2] = (Cell){0, 2, 0, 0, 856, 0, 0};
    cells[3] = (Cell){0, 3, 0, 0, 0, 0x03, 200};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 4);
    pcm = render_n(&d, b, n, 20u * TICK);
    expect_period(pcm, 11, 423);
    expect_period(pcm, 19, 656);
    dec_close(&d); free(pcm); free(b);

    /* st25 nie dziedziczy tej reguly: nuta 856 zastepuje cel, wiec 3xx bez nuty stoi. */
    cells[0] = (Cell){0, 0, 0, 1, 428, 0, 0};
    cells[1] = (Cell){0, 1, 0, 0, 214, 0x03, 1};
    cells[2] = (Cell){0, 2, 0, 0, 856, 0, 0};
    cells[3] = (Cell){0, 3, 0, 0, 0, 0x03, 200};
    b = build(&n, "M.K.", 1, 0, NULL, &inst, 1, cells, 4);
    pcm = render_opt(&d, b, n, 20u * TICK, "st25");
    expect_period(pcm, 19, 856);
    dec_close(&d); free(pcm); free(b);
    CHECK(d.bag.heap.live == 0);
}

/* Naglowek 15 instrumentow nie ma magika: bramka (glosnosc, kolejnosc, bajt 470) przepuszcza dane z samych spacji, jakie wypelniaja duze znaczniki ID3.  Taka sonda zleca hostowi
 * zmapowanie calego wejscia, wiec plik zaczynajacy sie poprawnym naglowkiem znanego kontenera audio nie moze byc uznany za modul; tytul modulu z literami "ID3" / "RIFF" nadal moze. */
static void test_probe_audio_containers(void) {
    uint8_t h[600];
    memset(h, 0x20, sizeof h);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, h, sizeof h, NULL) == 60);                      /* same spacje: nadal poprawny, luzny naglowek */
    memcpy(h, "ID3\4\0\0", 6u); memset(h + 6, 0, 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, h, sizeof h, NULL) == 0);                       /* ID3v2.4 */
    h[3] = 3; CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, h, sizeof h, NULL) == 0);              /* ID3v2.3 */
    h[3] = 2; CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, h, sizeof h, NULL) == 0);              /* ID3v2.2 */
    memset(h, 0x20, sizeof h); memcpy(h, "ID3 tune", 8u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, h, sizeof h, NULL) == 60);                      /* tytul tekstowy "ID3 tune": modul */
    memset(h, 0x20, sizeof h); memcpy(h, "RIFF", 4u); memcpy(h + 8, "WAVE", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, h, sizeof h, NULL) == 0);
    memset(h, 0x20, sizeof h); memcpy(h, "RIFF", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, h, sizeof h, NULL) == 60);                      /* "RIFF" w tytule, bez typu WAVE / AVI */
    memset(h, 0x20, sizeof h); memcpy(h, "FORM", 4u); memcpy(h + 8, "8SVX", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, h, sizeof h, NULL) == 0);
    memset(h, 0x20, sizeof h); memcpy(h, "OggS", 4u); h[4] = 0;
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, h, sizeof h, NULL) == 0);
    memset(h, 0x20, sizeof h); memcpy(h, "fLaC", 4u); h[4] = 0x80;
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, h, sizeof h, NULL) == 0);
    memset(h, 0x20, sizeof h); memcpy(h + 4, "ftyp", 4u);
    CHECK(maktabasauti_mod_desc.probe(&maktabasauti_mod_desc, h, sizeof h, NULL) == 0);
}

int main(void) {
    test_probe();
    test_probe_audio_containers();
    test_reject();
    test_play();
    test_timing();
    test_seek_and_sum();
    test_effects();
    test_variants();
    test_position();
    test_extended();
    test_restart();
    test_invert();
    test_sample_clear();
    test_clock();
    test_latch();
    test_coverage();
    test_mixer();
    test_tracker();
    test_st26();
    test_tone_target();
    if (g_failures) {
        fprintf(stderr, "%d niepowodzen\n", g_failures);
        return 1;
    }
    printf("test_decoder_mod: ok\n");
    return 0;
}
