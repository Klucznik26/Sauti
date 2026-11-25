/* IFF SAMP: liniowy PCM przy NumOfChans 0.  PlayMap, predkosc MIDI i petla nuty sa odmowa.
 * Inny Rate kolejnej fali to E_FORMAT_CHANGED, bez przeliczania probek. */
#include "maktabasauti/maktabasauti_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const maktabasauti_codec_t maktabasauti_samp_desc;

#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "FAIL %s:%d\n", __FILE__, __LINE__); return 1; } } while (0)

typedef struct {
    const uint8_t *p; size_t n, pos; int seekable; int known;
    size_t live; int bad;
} Mem;

static void *H_alloc(void *user, size_t n) { Mem *m = user; void *p = malloc(n ? n : 1); if (p) m->live++; return p; }
static void H_free(void *user, void *p) { Mem *m = user; if (!p) { m->bad++; return; } free(p); m->live--; }
static void *H_realloc(void *user, void *p, size_t n) { (void)user; return realloc(p, n ? n : 1); }
static int64_t H_read(void *user, maktabasauti_reader_t *rd, void *buf, size_t n) {
    Mem *m = user; size_t k; (void)rd;
    if (m->pos >= m->n) return 0;
    k = m->n - m->pos; if (k > n) k = n;
    memcpy(buf, m->p + m->pos, k); m->pos += k; return (int64_t)k;
}
static int64_t H_peek(void *user, maktabasauti_reader_t *rd, void *buf, size_t n) {
    Mem *m = user; size_t k; (void)rd;
    if (m->pos >= m->n) return 0;
    k = m->n - m->pos; if (k > n) k = n;
    memcpy(buf, m->p + m->pos, k); return (int64_t)k;
}
static int64_t H_seek(void *user, maktabasauti_reader_t *rd, int64_t off, int whence) {
    Mem *m = user; (void)rd; (void)whence;
    if (!m->seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;
    if (off < 0 || (size_t)off > m->n) return MAKTABASAUTI_E_OUT_OF_RANGE;
    m->pos = (size_t)off; return off;
}
static int64_t H_size(void *user, maktabasauti_reader_t *rd) { Mem *m = user; (void)rd; return m->known ? (int64_t)m->n : -1; }
static uint32_t H_flags(void *user, maktabasauti_reader_t *rd) {
    Mem *m = user; (void)rd;
    return (m->seekable ? MAKTABASAUTI_INPUT_SEEKABLE : 0u) | (m->known ? MAKTABASAUTI_INPUT_SIZE_KNOWN : 0u);
}
static void host_of(maktabasauti_host_t *h, Mem *m) {
    memset(h, 0, sizeof *h);
    h->struct_size = sizeof *h; h->user = m;
    h->alloc = H_alloc; h->realloc = H_realloc; h->free = H_free;
    h->read = H_read; h->peek = H_peek; h->seek = H_seek; h->size = H_size; h->input_flags = H_flags;
}
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

static void wave_hdr(uint8_t *p, uint32_t bytes, uint32_t rate, unsigned vel, uint32_t atak) {
    memset(p, 0, 80);
    put32(p, bytes); put32(p + 12, rate); p[25] = (uint8_t)vel; put32(p + 58, atak);
}

/* chans != 0 dokleja pusta PlayMap.  body_first: BODY przed MHDR. */
static uint8_t *build(size_t *out_n, unsigned format, unsigned waves, unsigned chans, unsigned playmode, unsigned flags,
                      const uint8_t *body, size_t body_n, int name_odd, int body_first) {
    const size_t mhdr = 6u + (size_t)chans * 128u;
    size_t n = 12u + (name_odd ? 10u : 0u) + 8u + mhdr + 8u + body_n + (body_n & 1u);
    uint8_t *b = (uint8_t *)calloc(1, n); size_t o = 12;
    if (!b) return NULL;
    memcpy(b, "FORM", 4); memcpy(b + 8, "SAMP", 4);
    if (name_odd) { memcpy(b + o, "NAME", 4); put32(b + o + 4, 1); b[o + 8] = 'A'; o += 10; }
    if (!body_first) {
        memcpy(b + o, "MHDR", 4); put32(b + o + 4, (uint32_t)mhdr);
        b[o + 8] = (uint8_t)waves; b[o + 9] = (uint8_t)format; b[o + 10] = (uint8_t)flags; b[o + 11] = (uint8_t)playmode; b[o + 12] = (uint8_t)chans;
        o += 8u + mhdr;
    }
    memcpy(b + o, "BODY", 4); put32(b + o + 4, (uint32_t)body_n);
    if (body_n) memcpy(b + o + 8, body, body_n);
    o += 8u + body_n + (body_n & 1u);
    if (body_first) {
        memcpy(b + o, "MHDR", 4); put32(b + o + 4, (uint32_t)mhdr);
        b[o + 8] = (uint8_t)waves; b[o + 9] = (uint8_t)format; b[o + 10] = (uint8_t)flags; b[o + 11] = (uint8_t)playmode; b[o + 12] = (uint8_t)chans;
    }
    put32(b + 4, (uint32_t)(n - 8u));
    *out_n = n; return b;
}
static maktabasauti_result_t do_open(Mem *mem, const uint8_t *p, size_t n, int seekable, void **st, maktabasauti_stream_info_t *info, maktabasauti_host_t *h) {
    memset(mem, 0, sizeof *mem); mem->p = p; mem->n = n; mem->seekable = seekable; mem->known = 1;
    host_of(h, mem);
    return maktabasauti_samp_desc.open(&maktabasauti_samp_desc, h, NULL, info, st);
}
static void close_st(void *st) { if (st) maktabasauti_samp_desc.close(st); }

int main(void) {
    uint8_t body[256], *b; size_t n = 0, got = 0; void *st = NULL; Mem mem; maktabasauti_host_t h; maktabasauti_stream_info_t info;
    float pcm[8]; uint64_t actual = 99; maktabasauti_result_t r;
    CHECK(maktabasauti_samp_desc.probe(&maktabasauti_samp_desc, (const uint8_t *)"FORM\0\0\0\0SAMP", 12, NULL) == 60);
    CHECK(maktabasauti_samp_desc.probe(&maktabasauti_samp_desc, (const uint8_t *)"FORM\0\0\0\08SVX", 12, NULL) == 0);

    /* Przyklad z opisu: bajty 100, -90, 80, -60.  Petla ustawiona, a i tak graja wszystkie cztery. */
    wave_hdr(body, 4, 10000u, 0, 0); put32(body + 16, 0); put32(body + 20, 2);
    body[80] = 100; body[81] = (uint8_t)-90; body[82] = 80; body[83] = (uint8_t)-60;
    b = build(&n, 8u, 1u, 0u, 2u, 1u, body, 84, 1, 0);
    CHECK(b);
    r = do_open(&mem, b, n, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK && st);
    CHECK(info.sample_rate == 10000u && info.channels == 1u && info.total_frames == 4u);
    CHECK(info.channel_mask == MAKTABASAUTI_LAYOUT_MONO && info.bits_per_sample == 8u);
    CHECK(strcmp(info.codec_name, "pcm") == 0);
    CHECK((info.flags & MAKTABASAUTI_STREAM_LOSSLESS) != 0);
    r = maktabasauti_samp_desc.read(st, pcm, 4, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 4);
    CHECK(pcm[0] == 100.0f / 128.0f && pcm[1] == -90.0f / 128.0f);
    CHECK(pcm[2] == 80.0f / 128.0f && pcm[3] == -60.0f / 128.0f);
    r = maktabasauti_samp_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_EOF && got == 0);
    CHECK(maktabasauti_samp_desc.reset(st) == MAKTABASAUTI_OK);
    r = maktabasauti_samp_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 1 && pcm[0] == 100.0f / 128.0f);
    CHECK(maktabasauti_samp_desc.seek(st, 9, 0, &actual) == MAKTABASAUTI_E_OUT_OF_RANGE);
    r = maktabasauti_samp_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 1 && pcm[0] == -90.0f / 128.0f);
    close_st(st); st = NULL; free(b); CHECK(mem.live == 0 && mem.bad == 0);

    /* Dwie fale, ten sam Rate.  Seek na granice.  Bez przewijania dlugosc nieznana, ale obie fale graja. */
    wave_hdr(body, 2, 8000u, 0, 0); body[80] = 1; body[81] = 2;
    wave_hdr(body + 82, 2, 8000u, 0, 0); body[162] = 3; body[163] = 4;
    b = build(&n, 8u, 2u, 0u, 0u, 0u, body, 164, 0, 0);
    r = do_open(&mem, b, n, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK && info.total_frames == 4u && info.sample_rate == 8000u);
    CHECK(maktabasauti_samp_desc.seek(st, 2, 0, &actual) == MAKTABASAUTI_OK && actual == 2u);
    r = maktabasauti_samp_desc.read(st, pcm, 2, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 2 && pcm[0] == 3.0f / 128.0f && pcm[1] == 4.0f / 128.0f);
    close_st(st); st = NULL;
    r = do_open(&mem, b, n, 0, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK && info.total_frames == MAKTABASAUTI_UNKNOWN_LENGTH);
    r = maktabasauti_samp_desc.read(st, pcm, 4, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 4 && pcm[2] == 3.0f / 128.0f);
    CHECK(maktabasauti_samp_desc.seek(st, 0, 0, &actual) == MAKTABASAUTI_E_NOT_SEEKABLE);
    CHECK(maktabasauti_samp_desc.reset(st) == MAKTABASAUTI_E_NOT_SEEKABLE);
    r = maktabasauti_samp_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_EOF && got == 0);
    close_st(st); st = NULL; free(b); CHECK(mem.live == 0);

    /* 12 bitow w slowie, dosuniete w lewo: 1 i -1.  Koperta ATAK jest pomijana. */
    wave_hdr(body, 4, 22050u, 0, 6); memset(body + 80, 0xff, 6);
    put16(body + 86, 0x1000); put16(body + 88, 0xFFF0);
    b = build(&n, 12u, 1u, 0u, 0u, 0u, body, 90, 0, 0);
    r = do_open(&mem, b, n, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK && info.bits_per_sample == 12u && info.total_frames == 2u);
    r = maktabasauti_samp_desc.read(st, pcm, 2, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 2);
    CHECK(pcm[0] == 4096.0f / 32768.0f && pcm[1] == -16.0f / 32768.0f);
    close_st(st); st = NULL; free(b);

    /* 24 bity w dlugim slowie: wartosc 1. */
    wave_hdr(body, 4, 48000u, 0, 0); put32(body + 80, 1);
    b = build(&n, 24u, 1u, 0u, 0u, 0u, body, 84, 0, 0);
    r = do_open(&mem, b, n, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK && info.bits_per_sample == 24u && info.sample_rate == 48000u);
    r = maktabasauti_samp_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 1 && pcm[0] == 1.0f / 2147483648.0f);
    close_st(st); st = NULL; free(b);

    /* 16 bit: 1 i -1. */
    wave_hdr(body, 4, 44100u, 0, 0); put16(body + 80, 1); put16(body + 82, (unsigned)-1);
    b = build(&n, 16u, 1u, 0u, 0u, 0u, body, 84, 0, 0);
    r = do_open(&mem, b, n, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK && info.total_frames == 2u);
    r = maktabasauti_samp_desc.read(st, pcm, 2, &got);
    CHECK(r == MAKTABASAUTI_OK && pcm[0] == 1.0f / 32768.0f && pcm[1] == -1.0f / 32768.0f);
    close_st(st); st = NULL; free(b);

    /* Urwany plik: cztery bajty zadeklarowane, dwa odciete. */
    wave_hdr(body, 4, 8000u, 0, 0); body[80] = 10; body[81] = 20; body[82] = 30; body[83] = 40;
    b = build(&n, 8u, 1u, 0u, 0u, 0u, body, 84, 0, 0);
    r = do_open(&mem, b, n - 2u, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK && info.total_frames == 2u);
    r = maktabasauti_samp_desc.read(st, pcm, 4, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 2 && pcm[0] == 10.0f / 128.0f && pcm[1] == 20.0f / 128.0f);
    r = maktabasauti_samp_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_E_TRUNCATED && got == 0);
    close_st(st); st = NULL; free(b);

    /* BODY przed MHDR. */
    wave_hdr(body, 2, 8000u, 0, 0); body[80] = 5; body[81] = 6;
    b = build(&n, 8u, 1u, 0u, 0u, 0u, body, 82, 0, 1);
    r = do_open(&mem, b, n, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK && info.total_frames == 2u);
    close_st(st); st = NULL;
    r = do_open(&mem, b, n, 0, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_E_NOT_SEEKABLE && st == NULL);
    free(b); CHECK(mem.live == 0);

    /* Druga fala ma wlasne Rate.  Odczyt konczy sie na granicy, potem E_FORMAT_CHANGED i get_info. */
    wave_hdr(body, 2, 8000u, 0, 0); body[80] = 7; body[81] = 9;
    wave_hdr(body + 82, 2, 16000u, 0, 0); body[162] = 8; body[163] = 10;
    b = build(&n, 8u, 2u, 0u, 0u, 0u, body, 164, 0, 0);
    r = do_open(&mem, b, n, 0, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK && info.sample_rate == 8000u && info.total_frames == MAKTABASAUTI_UNKNOWN_LENGTH);
    r = maktabasauti_samp_desc.read(st, pcm, 2, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 2 && pcm[0] == 7.0f / 128.0f && pcm[1] == 9.0f / 128.0f);
    r = maktabasauti_samp_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_E_FORMAT_CHANGED && got == 0);
    r = maktabasauti_samp_desc.get_info(st, &info);
    CHECK(r == MAKTABASAUTI_OK && info.sample_rate == 16000u && info.total_frames == MAKTABASAUTI_UNKNOWN_LENGTH);
    r = maktabasauti_samp_desc.read(st, pcm, 2, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 2 && pcm[0] == 8.0f / 128.0f && pcm[1] == 10.0f / 128.0f);
    r = maktabasauti_samp_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_EOF && got == 0);
    close_st(st); st = NULL;

    r = do_open(&mem, b, n, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK && info.sample_rate == 8000u && info.total_frames == 4u);
    r = maktabasauti_samp_desc.read(st, pcm, 4, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 2 && pcm[0] == 7.0f / 128.0f && pcm[1] == 9.0f / 128.0f);
    r = maktabasauti_samp_desc.get_info(st, &info);
    CHECK(r == MAKTABASAUTI_OK && info.sample_rate == 8000u);
    CHECK(maktabasauti_samp_desc.seek(st, 9, 0, &actual) == MAKTABASAUTI_E_OUT_OF_RANGE);
    r = maktabasauti_samp_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_E_FORMAT_CHANGED && got == 0);
    r = maktabasauti_samp_desc.get_info(st, &info);
    CHECK(r == MAKTABASAUTI_OK && info.sample_rate == 16000u && info.total_frames == 4u && info.channels == 1u);
    r = maktabasauti_samp_desc.read(st, pcm, 2, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 2 && pcm[0] == 8.0f / 128.0f && pcm[1] == 10.0f / 128.0f);
    CHECK(maktabasauti_samp_desc.reset(st) == MAKTABASAUTI_OK);
    r = maktabasauti_samp_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_E_FORMAT_CHANGED && got == 0);
    r = maktabasauti_samp_desc.get_info(st, &info);
    CHECK(r == MAKTABASAUTI_OK && info.sample_rate == 8000u);
    r = maktabasauti_samp_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 1 && pcm[0] == 7.0f / 128.0f);
    CHECK(maktabasauti_samp_desc.seek(st, 3, 0, &actual) == MAKTABASAUTI_OK && actual == 3u);
    r = maktabasauti_samp_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_E_FORMAT_CHANGED && got == 0);
    r = maktabasauti_samp_desc.get_info(st, &info);
    CHECK(r == MAKTABASAUTI_OK && info.sample_rate == 16000u);
    r = maktabasauti_samp_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 1 && pcm[0] == 10.0f / 128.0f);
    CHECK(maktabasauti_samp_desc.seek(st, 2, 0, &actual) == MAKTABASAUTI_OK && actual == 2u);
    r = maktabasauti_samp_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 1 && pcm[0] == 8.0f / 128.0f);
    close_st(st); st = NULL; free(b);

    wave_hdr(body, 2, 8000u, 0, 0); body[80] = 1; body[81] = 2;
    b = build(&n, 7u, 1u, 0u, 0u, 0u, body, 82, 0, 0);
    r = do_open(&mem, b, n, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_E_UNSUPPORTED_FORMAT && st == NULL); free(b);

    b = build(&n, 8u, 1u, 1u, 0u, 0u, body, 82, 0, 0);
    r = do_open(&mem, b, n, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_E_UNSUPPORTED_FEATURE && st == NULL); free(b);

    wave_hdr(body, 2, 8000u, 64u, 0); body[80] = 1; body[81] = 2;
    b = build(&n, 8u, 1u, 0u, 0u, 0u, body, 82, 0, 0);
    r = do_open(&mem, b, n, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_E_UNSUPPORTED_FEATURE && st == NULL); free(b);

    b = build(&n, 8u, 1u, 0u, 0u, 0u, body, 0, 0, 0);
    r = do_open(&mem, b, n, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_E_UNSUPPORTED_FORMAT && st == NULL); free(b);

    CHECK(mem.live == 0 && mem.bad == 0);
    printf("test_decoder_samp: ok\n");
    return 0;
}
