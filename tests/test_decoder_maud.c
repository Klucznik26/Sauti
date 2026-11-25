/* IFF MAUD: PCM upakowany MSB-first, A-law i mu-law.  Kompresje bez ukladu bitow sa odmowa. */
#include "maktabasauti/maktabasauti_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const maktabasauti_codec_t maktabasauti_maud_desc;

static int g_failures;
#define CHECK(cond) do { if (!(cond)) { g_failures++; printf("FAIL %s:%d\n", __FILE__, __LINE__); } } while (0)

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

/* mdat_first: MDAT przed MHDR.  name_odd: NAME o nieparzystej dlugosci.  lie_samples zapisuje 1 w mhdr_Samples. */
static uint8_t *build(size_t *out_n, uint32_t samples, uint32_t rate_src, unsigned rate_div, unsigned bits,
                      unsigned chinfo, unsigned channels, unsigned comp, const uint8_t *body, size_t body_n,
                      int name_odd, int mdat_first) {
    size_t n = 12u + (name_odd ? 10u : 0u) + 8u + 32u + 8u + body_n + (body_n & 1u);
    uint8_t *b = (uint8_t *)calloc(1, n); size_t o = 12;
    if (!b) return NULL;
    memcpy(b, "FORM", 4); memcpy(b + 8, "MAUD", 4);
    if (name_odd) { memcpy(b + o, "NAME", 4); put32(b + o + 4, 1); b[o + 8] = 'A'; o += 10; }
    if (!mdat_first) {
        memcpy(b + o, "MHDR", 4); put32(b + o + 4, 32); put32(b + o + 8, samples); put16(b + o + 12, bits); put16(b + o + 14, bits);
        put32(b + o + 16, rate_src); put16(b + o + 20, rate_div); put16(b + o + 22, chinfo); put16(b + o + 24, channels); put16(b + o + 26, comp);
        o += 40;
    }
    memcpy(b + o, "MDAT", 4); put32(b + o + 4, (uint32_t)body_n);
    if (body_n) memcpy(b + o + 8, body, body_n);
    o += 8u + body_n + (body_n & 1u);
    if (mdat_first) {
        memcpy(b + o, "MHDR", 4); put32(b + o + 4, 32); put32(b + o + 8, samples); put16(b + o + 12, bits); put16(b + o + 14, bits);
        put32(b + o + 16, rate_src); put16(b + o + 20, rate_div); put16(b + o + 22, chinfo); put16(b + o + 24, channels); put16(b + o + 26, comp);
    }
    put32(b + 4, (uint32_t)(n - 8u));
    *out_n = n; return b;
}
static maktabasauti_result_t do_open(Mem *mem, const uint8_t *p, size_t n, int seekable, void **st, maktabasauti_stream_info_t *info, maktabasauti_host_t *h) {
    memset(mem, 0, sizeof *mem); mem->p = p; mem->n = n; mem->seekable = seekable; mem->known = 1;
    host_of(h, mem);
    return maktabasauti_maud_desc.open(&maktabasauti_maud_desc, h, NULL, info, st);
}
static void close_st(maktabasauti_host_t *h, void *st) { if (st) maktabasauti_maud_desc.close(st); (void)h; }

int main(void) {
    uint8_t body[16], *b; size_t n = 0, got = 0; void *st = NULL; Mem mem; maktabasauti_host_t h; maktabasauti_stream_info_t info;
    float pcm[16]; uint64_t actual = 99; maktabasauti_result_t r;
    CHECK(maktabasauti_maud_desc.probe(&maktabasauti_maud_desc, (const uint8_t *)"FORM\0\0\0\0MAUD", 12, NULL) == 60);
    CHECK(maktabasauti_maud_desc.probe(&maktabasauti_maud_desc, (const uint8_t *)"FORM\0\0\0\08SVX", 12, NULL) == 0);

    body[0] = 128; body[1] = 255; body[2] = 0;
    b = build(&n, 1u, 8000u, 1u, 8u, 0u, 1u, 0u, body, 3, 1, 0);
    CHECK(b);
    r = do_open(&mem, b, n, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK && st);
    CHECK(info.sample_rate == 8000u && info.channels == 1u && info.total_frames == 3u);
    CHECK(info.channel_mask == MAKTABASAUTI_LAYOUT_MONO);
    CHECK(strcmp(info.codec_name, "pcm") == 0);
    r = maktabasauti_maud_desc.read(st, pcm, 3, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 3);
    CHECK(pcm[0] == 0.0f && pcm[1] == 127.0f / 128.0f && pcm[2] == -1.0f);
    r = maktabasauti_maud_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_EOF && got == 0);
    CHECK(maktabasauti_maud_desc.reset(st) == MAKTABASAUTI_OK);
    r = maktabasauti_maud_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 1 && pcm[0] == 0.0f);
    close_st(&h, st); st = NULL; free(b); CHECK(mem.live == 0 && mem.bad == 0);

    /* 16 bit stereo, przeplatane, bez przewijania.  mhdr_Samples klamie (1), MDAT ma dwie ramki. */
    put16(body, 1); put16(body + 2, (unsigned)-1); put16(body + 4, 2); put16(body + 6, (unsigned)-2);
    b = build(&n, 1u, 24576000u, 768u, 16u, 1u, 2u, 0u, body, 8, 0, 0);
    r = do_open(&mem, b, n, 0, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK && info.sample_rate == 32000u && info.channels == 2u && info.total_frames == 2u);
    CHECK(info.channel_mask == MAKTABASAUTI_LAYOUT_STEREO);
    r = maktabasauti_maud_desc.read(st, pcm, 2, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 2);
    CHECK(pcm[0] == 1.0f / 32768.0f && pcm[1] == -1.0f / 32768.0f);
    CHECK(pcm[2] == 2.0f / 32768.0f && pcm[3] == -2.0f / 32768.0f);
    CHECK(maktabasauti_maud_desc.seek(st, 0, 0, &actual) == MAKTABASAUTI_E_NOT_SEEKABLE);
    CHECK(maktabasauti_maud_desc.reset(st) == MAKTABASAUTI_E_NOT_SEEKABLE);
    close_st(&h, st); st = NULL; free(b); CHECK(mem.live == 0);

    /* Ta sama stereo z przewijaniem: ramka 1, potem poza zakres bez ruszania pozycji. */
    b = build(&n, 2u, 44100u, 1u, 16u, 1u, 2u, 0u, body, 8, 0, 0);
    r = do_open(&mem, b, n, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK);
    CHECK(maktabasauti_maud_desc.seek(st, 1, 0, &actual) == MAKTABASAUTI_OK && actual == 1u);
    r = maktabasauti_maud_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 1 && pcm[0] == 2.0f / 32768.0f && pcm[1] == -2.0f / 32768.0f);
    CHECK(maktabasauti_maud_desc.seek(st, 9, 0, &actual) == MAKTABASAUTI_E_OUT_OF_RANGE);
    r = maktabasauti_maud_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_EOF && got == 0);
    close_st(&h, st); st = NULL; free(b);

    /* Przyklad upakowania: 3 bity, 2 kanaly, cztery ramki.  Bajty E4 38 8E. */
    body[0] = 0xE4; body[1] = 0x38; body[2] = 0x8E;
    b = build(&n, 4u, 8000u, 1u, 3u, 1u, 2u, 0u, body, 3, 0, 0);
    r = do_open(&mem, b, n, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK && info.total_frames == 4u && info.bits_per_sample == 3u);
    r = maktabasauti_maud_desc.read(st, pcm, 4, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 4);
    CHECK(pcm[0] == -0.25f && pcm[1] == 0.25f && pcm[2] == 0.0f && pcm[3] == 0.75f);
    CHECK(pcm[4] == -1.0f && pcm[5] == 0.5f && pcm[6] == 0.25f && pcm[7] == -0.5f);
    CHECK(maktabasauti_maud_desc.seek(st, 2, 0, &actual) == MAKTABASAUTI_OK);
    r = maktabasauti_maud_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 1 && pcm[0] == -1.0f && pcm[1] == 0.5f);
    close_st(&h, st); st = NULL; free(b); CHECK(mem.live == 0);

    /* 24 bit, jedna probka 1. */
    body[0] = 0; body[1] = 0; body[2] = 1;
    b = build(&n, 1u, 48000u, 1u, 24u, 0u, 1u, 0u, body, 3, 0, 0);
    r = do_open(&mem, b, n, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK && info.total_frames == 1u);
    r = maktabasauti_maud_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_OK && pcm[0] == 1.0f / 8388608.0f);
    close_st(&h, st); st = NULL; free(b);

    /* Cztery kanaly, MCI_MULTISTEREO, 8 bit bez znaku. */
    body[0] = 128; body[1] = 128; body[2] = 255; body[3] = 0;
    b = build(&n, 1u, 8000u, 1u, 8u, 3u, 4u, 0u, body, 4, 0, 0);
    r = do_open(&mem, b, n, 0, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK && info.channels == 4u && info.channel_mask == 0u && info.total_frames == 1u);
    r = maktabasauti_maud_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_OK && pcm[0] == 0.0f && pcm[1] == 0.0f && pcm[2] == 127.0f / 128.0f && pcm[3] == -1.0f);
    close_st(&h, st); st = NULL; free(b);

    /* A-law 0xD5 -> +8, mu-law 0xFF -> 0. */
    body[0] = 0xD5;
    b = build(&n, 1u, 8000u, 1u, 8u, 0u, 1u, 2u, body, 1, 0, 0);
    r = do_open(&mem, b, n, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK && strcmp(info.codec_name, "g711-alaw") == 0 && info.bits_per_sample == 16u);
    CHECK((info.flags & MAKTABASAUTI_STREAM_LOSSLESS) == 0u);
    r = maktabasauti_maud_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_OK && pcm[0] == 8.0f / 32768.0f);
    close_st(&h, st); st = NULL; free(b);
    body[0] = 0xFF;
    b = build(&n, 1u, 8000u, 1u, 8u, 0u, 1u, 3u, body, 1, 0, 0);
    r = do_open(&mem, b, n, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK && strcmp(info.codec_name, "g711-ulaw") == 0);
    r = maktabasauti_maud_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_OK && pcm[0] == 0.0f);
    close_st(&h, st); st = NULL; free(b);

    /* MDAT przed MHDR: z przewijaniem gra, bez przewijania odmowa. */
    body[0] = 128;
    b = build(&n, 1u, 8000u, 1u, 8u, 0u, 1u, 0u, body, 1, 0, 1);
    r = do_open(&mem, b, n, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK && info.total_frames == 1u);
    close_st(&h, st); st = NULL;
    r = do_open(&mem, b, n, 0, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_E_NOT_SEEKABLE && st == NULL);
    free(b); CHECK(mem.live == 0);

    /* Urwane MDAT: deklaracja 4 probek 16 bit, w pliku sa 2. */
    put16(body, 100); put16(body + 2, 200); put16(body + 4, 300); put16(body + 6, 400);
    b = build(&n, 4u, 8000u, 1u, 16u, 0u, 1u, 0u, body, 8, 0, 0);
    r = do_open(&mem, b, n - 4u, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK && info.total_frames == 2u);
    r = maktabasauti_maud_desc.read(st, pcm, 4, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 2);
    r = maktabasauti_maud_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_E_TRUNCATED && got == 0);
    close_st(&h, st); st = NULL; free(b);

    b = build(&n, 1u, 100u, 3u, 8u, 0u, 1u, 0u, body, 1, 0, 0);
    CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_E_UNSUPPORTED_FEATURE && st == NULL);
    free(b);
    b = build(&n, 1u, 8000u, 0u, 8u, 0u, 1u, 0u, body, 1, 0, 0);
    CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_E_UNSUPPORTED_FORMAT && st == NULL);
    free(b);
    b = build(&n, 1u, 8000u, 1u, 8u, 0u, 2u, 0u, body, 2, 0, 0);
    CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_E_UNSUPPORTED_FORMAT && st == NULL);
    free(b);
    b = build(&n, 1u, 8000u, 1u, 8u, 4u, 2u, 0u, body, 2, 0, 0);
    CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_E_UNSUPPORTED_FEATURE && st == NULL);
    free(b);
    b = build(&n, 1u, 8000u, 1u, 8u, 3u, 3u, 0u, body, 3, 0, 0);
    CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_E_UNSUPPORTED_FORMAT && st == NULL);
    free(b);
    b = build(&n, 1u, 8000u, 1u, 8u, 0u, 1u, 1u, body, 1, 0, 0);
    CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_E_UNSUPPORTED_FEATURE && st == NULL);
    free(b);
    b = build(&n, 1u, 8000u, 1u, 16u, 0u, 1u, 2u, body, 2, 0, 0);
    CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_E_UNSUPPORTED_FEATURE && st == NULL);
    free(b);
    b = build(&n, 0u, 8000u, 1u, 8u, 0u, 1u, 0u, body, 0, 0, 0);
    CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_E_UNSUPPORTED_FORMAT && st == NULL);
    free(b);

    /* Drugi MHDR. */
    b = build(&n, 1u, 8000u, 1u, 8u, 0u, 1u, 0u, body, 1, 0, 0);
    {
        uint8_t *w = (uint8_t *)realloc(b, n + 40u);
        CHECK(w); b = w; memset(b + n, 0, 40u); memcpy(b + n, "MHDR", 4); put32(b + n + 4, 32);
        put32(b + 4, (uint32_t)(n + 40u - 8u)); n += 40u;
    }
    CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_E_UNSUPPORTED_FORMAT && st == NULL);
    free(b);

    if (g_failures) { printf("test_decoder_maud: %d failures\n", g_failures); return 1; }
    printf("test_decoder_maud: ok\n");
    return 0;
}
