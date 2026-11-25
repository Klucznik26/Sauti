/* IFF 16SV: PCM 16-bit ze znakiem, big-endian, kanaly jeden po drugim.  Kompresja i CHAN spoza 2/4/6 sa odmowa. */
#include "maktabasauti/maktabasauti_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const maktabasauti_codec_t maktabasauti_16sv_desc;

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

/* probki sa juz big-endian.  name_odd dokleja NAME o nieparzystej dlugosci przed VHDR. */
static uint8_t *build(size_t *out_n, unsigned rate, unsigned octave, unsigned comp, unsigned chan_val, int chan_on,
                      uint32_t one_shot, uint32_t repeat, const uint8_t *body, size_t body_n, int name_odd) {
    size_t n = 12u + (name_odd ? 8u + 2u : 0u) + 8u + 20u + (chan_on ? 8u + 4u : 0u) + 8u + body_n + (body_n & 1u);
    uint8_t *b = (uint8_t *)calloc(1, n); size_t o = 12; uint32_t form;
    if (!b) return NULL;
    memcpy(b, "FORM", 4); memcpy(b + 8, "16SV", 4);
    if (name_odd) { memcpy(b + o, "NAME", 4); put32(b + o + 4, 1); b[o + 8] = 'A'; o += 10; }
    memcpy(b + o, "VHDR", 4); put32(b + o + 4, 20);
    put32(b + o + 8, one_shot); put32(b + o + 12, repeat); put16(b + o + 20, rate);
    b[o + 22] = (uint8_t)octave; b[o + 23] = (uint8_t)comp; put32(b + o + 24, 0x10000u); o += 28;
    if (chan_on) { memcpy(b + o, "CHAN", 4); put32(b + o + 4, 4); put32(b + o + 8, chan_val); o += 12; }
    memcpy(b + o, "BODY", 4); put32(b + o + 4, (uint32_t)body_n);
    if (body_n) memcpy(b + o + 8, body, body_n);
    form = (uint32_t)(n - 8u); put32(b + 4, form);
    *out_n = n; return b;
}
static void be_s16(uint8_t *p, int v) { p[0] = (uint8_t)((unsigned)v >> 8); p[1] = (uint8_t)v; }

static maktabasauti_result_t do_open(Mem *mem, const uint8_t *p, size_t n, int seekable, void **st, maktabasauti_stream_info_t *info, maktabasauti_host_t *h) {
    memset(mem, 0, sizeof *mem); mem->p = p; mem->n = n; mem->seekable = seekable; mem->known = 1;
    host_of(h, mem);
    return maktabasauti_16sv_desc.open(&maktabasauti_16sv_desc, h, NULL, info, st);
}

int main(void) {
    uint8_t body[16], *b; size_t n = 0, got = 0; void *st = NULL; Mem mem; maktabasauti_host_t h; maktabasauti_stream_info_t info;
    float pcm[8]; uint64_t actual = 99;
    const float s100 = 100.0f / 32768.0f, s200 = 200.0f / 32768.0f, s32767 = 32767.0f / 32768.0f;

    memset(body, 0, sizeof body);
    be_s16(body + 0, 0); be_s16(body + 2, 32767); be_s16(body + 4, -32768); be_s16(body + 6, 1);
    b = build(&n, 44100, 1, 0, 0, 0, 4, 0, body, 8, 1);
    CHECK(maktabasauti_16sv_desc.probe(&maktabasauti_16sv_desc, b, n, NULL) == 60);
    CHECK(maktabasauti_16sv_desc.probe(&maktabasauti_16sv_desc, b, 11, NULL) == 0);
    memcpy(b + 8, "8SVX", 4);
    CHECK(maktabasauti_16sv_desc.probe(&maktabasauti_16sv_desc, b, n, NULL) == 0);
    memcpy(b + 8, "16SV", 4);
    memset(&info, 0, sizeof info);
    CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_OK);
    CHECK(info.sample_rate == 44100 && info.channels == 1 && info.bits_per_sample == 16);
    CHECK(info.total_frames == 4 && (info.flags & MAKTABASAUTI_STREAM_SEEKABLE) && (info.flags & MAKTABASAUTI_STREAM_LOSSLESS));
    CHECK(strcmp(info.codec_name, "pcm") == 0 && strcmp(info.container_name, "iff") == 0);
    CHECK(maktabasauti_16sv_desc.read(st, pcm, 4, &got) == MAKTABASAUTI_OK && got == 4);
    CHECK(pcm[0] == 0.0f && pcm[1] == s32767 && pcm[2] == -1.0f && pcm[3] == 1.0f / 32768.0f);
    CHECK(maktabasauti_16sv_desc.read(st, pcm, 1, &got) == MAKTABASAUTI_EOF && got == 0);
    actual = 0;
    CHECK(maktabasauti_16sv_desc.seek(st, 2, 0, &actual) == MAKTABASAUTI_OK && actual == 2);
    CHECK(maktabasauti_16sv_desc.read(st, pcm, 2, &got) == MAKTABASAUTI_OK && got == 2);
    CHECK(pcm[0] == -1.0f && pcm[1] == 1.0f / 32768.0f);
    CHECK(maktabasauti_16sv_desc.seek(st, 5, 0, &actual) == MAKTABASAUTI_E_OUT_OF_RANGE);
    maktabasauti_16sv_desc.close(st); st = NULL;
    CHECK(mem.live == 0 && mem.bad == 0);
    free(b);

    /* Stereo: lewy 100,200 i prawy -100,-300.  CHAN 4 zostaje jednym kanalem. */
    be_s16(body + 0, 100); be_s16(body + 2, 200); be_s16(body + 4, -100); be_s16(body + 6, -300);
    b = build(&n, 22050, 1, 0, 6, 1, 2, 0, body, 8, 0);
    CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_OK);
    CHECK(info.channels == 2 && info.total_frames == 2 && info.channel_mask == MAKTABASAUTI_LAYOUT_STEREO);
    CHECK(maktabasauti_16sv_desc.read(st, pcm, 2, &got) == MAKTABASAUTI_OK && got == 2);
    CHECK(pcm[0] == s100 && pcm[1] == -s100 && pcm[2] == s200 && pcm[3] == -300.0f / 32768.0f);
    CHECK(maktabasauti_16sv_desc.seek(st, 1, 0, &actual) == MAKTABASAUTI_OK);
    CHECK(maktabasauti_16sv_desc.read(st, pcm, 1, &got) == MAKTABASAUTI_OK && got == 1);
    CHECK(pcm[0] == s200 && pcm[1] == -300.0f / 32768.0f);
    maktabasauti_16sv_desc.close(st); free(b);
    CHECK(mem.live == 0);

    b = build(&n, 22050, 1, 0, 4, 1, 2, 0, body, 4, 0);
    CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_OK);
    CHECK(info.channels == 1 && info.total_frames == 2);
    maktabasauti_16sv_desc.close(st); free(b);

    /* Bez przewijania mono czyta, stereo odmawia. */
    be_s16(body + 0, 100); be_s16(body + 2, 200);
    b = build(&n, 16000, 0, 0, 0, 0, 0, 0, body, 4, 0);
    CHECK(do_open(&mem, b, n, 0, &st, &info, &h) == MAKTABASAUTI_OK);
    CHECK(info.channels == 1 && info.total_frames == 2 && (info.flags & MAKTABASAUTI_STREAM_SEEKABLE) == 0);
    CHECK(maktabasauti_16sv_desc.read(st, pcm, 2, &got) == MAKTABASAUTI_OK && got == 2 && pcm[0] == s100 && pcm[1] == s200);
    CHECK(maktabasauti_16sv_desc.seek(st, 0, 0, &actual) == MAKTABASAUTI_E_NOT_SEEKABLE);
    maktabasauti_16sv_desc.close(st); free(b);
    b = build(&n, 16000, 1, 0, 6, 1, 1, 0, body, 4, 0);
    CHECK(do_open(&mem, b, n, 0, &st, &info, &h) == MAKTABASAUTI_E_NOT_SEEKABLE && st == NULL);
    free(b);

    /* ctOctave 2: najwyzsza ma 2 probki i jest pierwsza, najnizsza ma 4.  Gramy te cztery. */
    be_s16(body + 0, 10); be_s16(body + 2, 20); be_s16(body + 4, 30); be_s16(body + 6, 40); be_s16(body + 8, 50); be_s16(body + 10, 60);
    b = build(&n, 8000, 2, 0, 0, 0, 2, 0, body, 12, 0);
    CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_OK);
    CHECK(info.total_frames == 4);
    CHECK(maktabasauti_16sv_desc.read(st, pcm, 4, &got) == MAKTABASAUTI_OK && got == 4);
    CHECK(pcm[0] == 30.0f / 32768.0f && pcm[1] == 40.0f / 32768.0f && pcm[2] == 50.0f / 32768.0f && pcm[3] == 60.0f / 32768.0f);
    CHECK(maktabasauti_16sv_desc.seek(st, 2, 0, &actual) == MAKTABASAUTI_OK && actual == 2);
    CHECK(maktabasauti_16sv_desc.read(st, pcm, 2, &got) == MAKTABASAUTI_OK && got == 2);
    CHECK(pcm[0] == 50.0f / 32768.0f && pcm[1] == 60.0f / 32768.0f);
    CHECK(maktabasauti_16sv_desc.read(st, pcm, 1, &got) == MAKTABASAUTI_EOF);
    maktabasauti_16sv_desc.close(st); free(b);
    /* Cale BODY krotsze niz wzor: zostaje jedna probka najnizszej oktawy, potem EOF. */
    be_s16(body + 0, 100); be_s16(body + 2, 200); be_s16(body + 4, 32767);
    b = build(&n, 8000, 2, 0, 0, 0, 2, 0, body, 6, 0);
    CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_OK);
    CHECK(info.total_frames == 1);
    CHECK(maktabasauti_16sv_desc.read(st, pcm, 4, &got) == MAKTABASAUTI_OK && got == 1 && pcm[0] == s32767);
    CHECK(maktabasauti_16sv_desc.read(st, pcm, 1, &got) == MAKTABASAUTI_EOF);
    maktabasauti_16sv_desc.close(st); free(b);
    /* Sama najwyzsza oktawa: najnizszej nie ma w calym BODY. */
    b = build(&n, 8000, 2, 0, 0, 0, 2, 0, body, 4, 0);
    CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_E_UNSUPPORTED_FEATURE && st == NULL);
    free(b);
    /* ctOctave 1 nie obcina polami petli. */
    be_s16(body + 0, 100); be_s16(body + 2, 200); be_s16(body + 4, 1);
    b = build(&n, 8000, 1, 0, 0, 0, 1, 0, body, 6, 0);
    CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_OK && info.total_frames == 3);
    maktabasauti_16sv_desc.close(st); free(b);
    b = build(&n, 8000, 2, 0, 0, 0, 0, 0, body, 6, 0);
    CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_E_UNSUPPORTED_FEATURE && st == NULL);
    free(b);

    /* Kompresje SoundFX i obce CHAN: rozpoznajemy plik, otwarcie odmawia. */
    b = build(&n, 8000, 1, 1, 0, 0, 2, 0, body, 4, 0);
    CHECK(maktabasauti_16sv_desc.probe(&maktabasauti_16sv_desc, b, n, NULL) == 60);
    CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_E_UNSUPPORTED_FEATURE && st == NULL);
    free(b);
    b = build(&n, 8000, 1, 2, 0, 0, 2, 0, body, 4, 0);
    CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_E_UNSUPPORTED_FEATURE && st == NULL);
    free(b);
    b = build(&n, 8000, 1, 0, 8, 1, 2, 0, body, 4, 0);
    CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_E_UNSUPPORTED_FEATURE && st == NULL);
    free(b);

    /* Urwane BODY: dwie cale probki, potem lepki E_TRUNCATED. */
    be_s16(body + 0, 100); be_s16(body + 2, 200); be_s16(body + 4, 1); be_s16(body + 6, 1);
    b = build(&n, 8000, 1, 0, 0, 0, 4, 0, body, 8, 0);
    CHECK(n > 4);
    CHECK(do_open(&mem, b, n - 4, 1, &st, &info, &h) == MAKTABASAUTI_OK);
    CHECK(info.total_frames == 2);
    CHECK(maktabasauti_16sv_desc.read(st, pcm, 4, &got) == MAKTABASAUTI_OK && got == 2 && pcm[0] == s100 && pcm[1] == s200);
    CHECK(maktabasauti_16sv_desc.read(st, pcm, 1, &got) == MAKTABASAUTI_E_TRUNCATED && got == 0);
    CHECK(maktabasauti_16sv_desc.read(st, pcm, 1, &got) == MAKTABASAUTI_E_TRUNCATED);
    CHECK(maktabasauti_16sv_desc.flush(st) == MAKTABASAUTI_OK);
    CHECK(maktabasauti_16sv_desc.read(st, pcm, 1, &got) == MAKTABASAUTI_E_TRUNCATED);
    maktabasauti_16sv_desc.close(st); free(b);

    /* Ponad jedno wypelnienie bufora: mono i stereo musza dojsc za 4096 bajt. */
    {
        enum { NFR = 5000 };
        uint8_t *raw = (uint8_t *)malloc((size_t)NFR * 2u);
        float *out = (float *)malloc((size_t)NFR * sizeof *out);
        unsigned i;
        CHECK(raw && out);
        for (i = 0; i < NFR; i++) be_s16(raw + (size_t)i * 2u, (int)i - 2500);
        b = build(&n, 44100, 1, 0, 0, 0, NFR, 0, raw, (size_t)NFR * 2u, 0);
        CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_OK);
        CHECK(info.total_frames == NFR);
        CHECK(maktabasauti_16sv_desc.read(st, out, NFR, &got) == MAKTABASAUTI_OK && got == NFR);
        CHECK(out[0] == -2500.0f / 32768.0f && out[2048] == (2048.0f - 2500.0f) / 32768.0f && out[NFR - 1] == (NFR - 1 - 2500.0f) / 32768.0f);
        maktabasauti_16sv_desc.close(st); free(b); free(raw); free(out);
        CHECK(mem.live == 0);
    }
    {
        enum { NFR = 3000 };
        uint8_t *raw = (uint8_t *)malloc((size_t)NFR * 4u);
        float *out = (float *)malloc((size_t)NFR * 2u * sizeof *out);
        unsigned i;
        CHECK(raw && out);
        for (i = 0; i < NFR; i++) { be_s16(raw + (size_t)i * 2u, (int)i + 1); be_s16(raw + (size_t)NFR * 2u + (size_t)i * 2u, -((int)i + 1)); }
        b = build(&n, 44100, 1, 0, 6, 1, NFR, 0, raw, (size_t)NFR * 4u, 0);
        CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_OK);
        CHECK(info.channels == 2 && info.total_frames == NFR);
        CHECK(maktabasauti_16sv_desc.read(st, out, NFR, &got) == MAKTABASAUTI_OK && got == NFR);
        CHECK(out[0] == 1.0f / 32768.0f && out[1] == -1.0f / 32768.0f);
        CHECK(out[2048 * 2] == 2049.0f / 32768.0f && out[2048 * 2 + 1] == -2049.0f / 32768.0f);
        CHECK(out[(NFR - 1) * 2] == (float)NFR / 32768.0f && out[(NFR - 1) * 2 + 1] == -(float)NFR / 32768.0f);
        maktabasauti_16sv_desc.close(st); free(b); free(raw); free(out);
        CHECK(mem.live == 0);
    }

    b = build(&n, 0, 1, 0, 0, 0, 1, 0, body, 2, 0);
    CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_E_UNSUPPORTED_FORMAT && st == NULL);
    free(b);
    b = build(&n, 8000, 1, 0, 0, 0, 1, 0, body, 0, 0);
    CHECK(do_open(&mem, b, n, 1, &st, &info, &h) == MAKTABASAUTI_E_UNSUPPORTED_FORMAT && st == NULL);
    free(b);

    if (g_failures) { printf("test_decoder_16sv: %d FAILURES\n", g_failures); return 1; }
    printf("test_decoder_16sv: ok\n");
    return 0;
}
