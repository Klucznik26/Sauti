/* Musepack SV7.  Wzorzec float to libmpcdec r475, pliki z mppenc 1.16.
 * Porownanie jest bitowe: surowe float stereo w tests/data/mpc, rozszerzenie f32. */
#include "maktabasauti/maktabasauti_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const maktabasauti_codec_t maktabasauti_mpc_desc;

static int g_failures;
#define CHECK(cond) do { if (!(cond)) { g_failures++; printf("FAIL %s:%d\n", __FILE__, __LINE__); } } while (0)

typedef struct {
    const uint8_t *p; size_t n, pos; int seekable;
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
static int64_t H_seek(void *user, maktabasauti_reader_t *rd, int64_t off, int whence) {
    Mem *m = user; (void)rd; (void)whence;
    if (!m->seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;
    if (off < 0 || (size_t)off > m->n) return MAKTABASAUTI_E_OUT_OF_RANGE;
    m->pos = (size_t)off; return off;
}
static void host_of(maktabasauti_host_t *h, Mem *m) {
    memset(h, 0, sizeof *h);
    h->struct_size = sizeof *h; h->user = m;
    h->alloc = H_alloc; h->realloc = H_realloc; h->free = H_free;
    h->read = H_read; h->seek = H_seek;
}

static uint8_t *load(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); long sz; uint8_t *b;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    sz = ftell(f); if (sz < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    b = (uint8_t *)malloc((size_t)sz ? (size_t)sz : 1u);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); fclose(f); return NULL; }
    fclose(f); *n = (size_t)sz; return b;
}

static maktabasauti_result_t do_open(Mem *mem, const uint8_t *p, size_t n, int seekable,
                                     void **st, maktabasauti_stream_info_t *info, maktabasauti_host_t *h) {
    memset(mem, 0, sizeof *mem); mem->p = p; mem->n = n; mem->seekable = seekable;
    host_of(h, mem);
    return maktabasauti_mpc_desc.open(&maktabasauti_mpc_desc, h, NULL, info, st);
}

static maktabasauti_result_t read_all(void *st, float *dst, size_t cap, size_t step, unsigned ch, size_t *got) {
    size_t n = 0;
    while (n < cap) {
        size_t k = 0, ask = cap - n;
        maktabasauti_result_t r;
        if (ask > step) ask = step;
        r = maktabasauti_mpc_desc.read(st, dst + n * ch, ask, &k);
        n += k;
        if (r != MAKTABASAUTI_OK) { *got = n; return r; }
    }
    *got = n;
    return MAKTABASAUTI_OK;
}

static void check_file(const char *stem, uint32_t rate, uint64_t frames, unsigned ch, const char *profile, size_t step, uint64_t seek_at) {
    char pa[96], pr[96];
    uint8_t *file = NULL, *refb = NULL; size_t fn = 0, rn = 0, got = 0;
    float *pcm = NULL, *ref = NULL; void *st = NULL; Mem mem; maktabasauti_host_t h;
    maktabasauti_stream_info_t info; maktabasauti_result_t r; uint64_t actual = 1;
    snprintf(pa, sizeof pa, "tests/data/mpc/%s.mpc", stem);
    snprintf(pr, sizeof pr, "tests/data/mpc/%s.f32", stem);
    file = load(pa, &fn); refb = load(pr, &rn);
    CHECK(file && refb && rn == frames * (size_t)ch * sizeof(float));
    if (!file || !refb) { free(file); free(refb); return; }
    ref = (float *)refb;
    pcm = (float *)calloc(frames * (size_t)ch + 4u, sizeof(float));
    CHECK(pcm);
    CHECK(maktabasauti_mpc_desc.probe(&maktabasauti_mpc_desc, file, fn < 64 ? fn : 64, NULL) == 90);
    r = do_open(&mem, file, fn, 0, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK && st);
    if (r != MAKTABASAUTI_OK) { free(pcm); free(file); free(refb); return; }
    CHECK(info.sample_rate == rate && info.channels == ch && info.total_frames == frames);
    CHECK(info.native_format == MAKTABASAUTI_FORMAT_F32 && info.bits_per_sample == 0);
    CHECK(info.channel_mask == (ch == 1u ? MAKTABASAUTI_LAYOUT_MONO : MAKTABASAUTI_LAYOUT_STEREO));
    CHECK((info.flags & MAKTABASAUTI_STREAM_LOSSLESS) == 0u);
    CHECK((info.flags & MAKTABASAUTI_STREAM_GAPLESS_TRIMMED) != 0u);
    CHECK(info.priming_frames == 481u && info.end_padding_frames == 0u);
    CHECK(strcmp(info.codec_name, "musepack") == 0 && strcmp(info.codec_profile, profile) == 0);
    CHECK(strcmp(info.container_name, "mpc") == 0);
    r = read_all(st, pcm, (size_t)frames, step, ch, &got);
    CHECK(r == MAKTABASAUTI_OK && got == (size_t)frames);
    CHECK(memcmp(pcm, ref, frames * (size_t)ch * sizeof(float)) == 0);
    r = maktabasauti_mpc_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_EOF && got == 0);
    CHECK(maktabasauti_mpc_desc.seek(st, frames + 1u, 0, &actual) == MAKTABASAUTI_E_OUT_OF_RANGE);
    r = maktabasauti_mpc_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_EOF && got == 0);
    CHECK(maktabasauti_mpc_desc.seek(st, frames, 0, &actual) == MAKTABASAUTI_OK && actual == frames);
    r = maktabasauti_mpc_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_EOF && got == 0);
    CHECK(maktabasauti_mpc_desc.seek(st, seek_at, 0, &actual) == MAKTABASAUTI_OK && actual == seek_at);
    r = read_all(st, pcm, (size_t)(frames - seek_at), step, ch, &got);
    CHECK(r == MAKTABASAUTI_OK && got == (size_t)(frames - seek_at));
    CHECK(memcmp(pcm, ref + seek_at * ch, (frames - seek_at) * (size_t)ch * sizeof(float)) == 0);
    CHECK(maktabasauti_mpc_desc.reset(st) == MAKTABASAUTI_OK);
    r = read_all(st, pcm, 16, 3, ch, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 16);
    CHECK(memcmp(pcm, ref, 16u * (size_t)ch * sizeof(float)) == 0);
    maktabasauti_mpc_desc.close(st);
    free(pcm); free(file); free(refb);
    CHECK(mem.live == 0 && mem.bad == 0);
}

/* tag_body bajtow tagu, potem plik. Rozmiar w naglowku to tylko tag, nie audio. */
static uint8_t *wrap_id3(const uint8_t *p, size_t n, size_t tag_body, size_t *out) {
    uint8_t *b = (uint8_t *)malloc(10u + tag_body + n);
    uint32_t sz = (uint32_t)tag_body;
    if (!b) return NULL;
    memset(b, 0, 10u + tag_body);
    memcpy(b, "ID3", 3); b[3] = 3; b[4] = 0; b[5] = 0;
    b[6] = (uint8_t)((sz >> 21) & 0x7f); b[7] = (uint8_t)((sz >> 14) & 0x7f);
    b[8] = (uint8_t)((sz >> 7) & 0x7f); b[9] = (uint8_t)(sz & 0x7f);
    memcpy(b + 10u + tag_body, p, n);
    *out = 10u + tag_body + n; return b;
}

int main(void) {
    uint8_t *file = NULL, *refb = NULL, *wrap = NULL, bad[32];
    size_t fn = 0, rn = 0, wn = 0, got = 0;
    void *st = NULL; Mem mem; maktabasauti_host_t h; maktabasauti_stream_info_t info;
    maktabasauti_result_t r; float pcm[8];
    static const uint8_t mpck[8] = {'M', 'P', 'C', 'K', 0, 0, 0, 0};
    static const uint8_t other[] = "FORM\0\0\0\08SVX";

    CHECK(maktabasauti_mpc_desc.probe(&maktabasauti_mpc_desc, other, sizeof other - 1, NULL) == 0);
    CHECK(maktabasauti_mpc_desc.probe(&maktabasauti_mpc_desc, (const uint8_t *)"fLaC", 4, NULL) == 0);
    CHECK(maktabasauti_mpc_desc.probe(&maktabasauti_mpc_desc, (const uint8_t *)"RIFF", 4, NULL) == 0);
    CHECK(maktabasauti_mpc_desc.probe(&maktabasauti_mpc_desc, mpck, sizeof mpck, NULL) == 90);
    memset(bad, 0, sizeof bad); memcpy(bad, "MP+", 3); bad[3] = 6;
    CHECK(maktabasauti_mpc_desc.probe(&maktabasauti_mpc_desc, bad, sizeof bad, NULL) == 0);
    r = do_open(&mem, bad, sizeof bad, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_E_UNSUPPORTED_FORMAT && st == NULL);
    CHECK(mem.live == 0);
    r = do_open(&mem, mpck, sizeof mpck, 1, &st, &info, &h);
    CHECK(r != MAKTABASAUTI_OK && st == NULL);
    CHECK(mem.live == 0);
    r = do_open(&mem, (const uint8_t *)"MP+\x07", 4, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_E_TRUNCATED && st == NULL);
    CHECK(mem.live == 0);

    check_file("sv7-short", 44100u, 882u, 2, "SV7", 64, 400u);
    check_file("sv7-tone", 44100u, 3528u, 2, "SV7", 1152, 1000u);
    check_file("sv7-r32", 32000u, 960u, 2, "SV7", 17, 700u);
    check_file("sv7-mono", 44100u, 2205u, 2, "SV7", 1, 10u);
    check_file("sv7-noise", 44100u, 5292u, 2, "SV7", 100, 2000u);
    check_file("sv8-tone", 44100u, 3528u, 2, "SV8", 64, 400u);
    check_file("sv8-mono", 44100u, 2205u, 1, "SV8", 17, 10u);
    check_file("sv8-r32", 32000u, 960u, 2, "SV8", 100, 700u);
    check_file("sv8-noise", 44100u, 5292u, 2, "SV8", 1152, 2000u);

    file = load("tests/data/mpc/sv7-short.mpc", &fn);
    refb = load("tests/data/mpc/sv7-short.f32", &rn);
    CHECK(file && refb && fn > 80);
    if (file && refb) {
        float *all = (float *)malloc(rn);
        uint64_t actual = 0;
        r = do_open(&mem, file, fn, 0, &st, &info, &h);
        CHECK(r == MAKTABASAUTI_OK);
        if (r == MAKTABASAUTI_OK && all) {
            CHECK(maktabasauti_mpc_desc.seek(st, 671, 0, &actual) == MAKTABASAUTI_OK && actual == 671u);
            r = read_all(st, all, 882u - 671u, 50, 2, &got);
            CHECK(r == MAKTABASAUTI_OK && got == 211u && memcmp(all, refb + 671u * 2u * sizeof(float), 211u * 2u * sizeof(float)) == 0);
            maktabasauti_mpc_desc.close(st); st = NULL;
        }
        CHECK(mem.live == 0);
        wrap = wrap_id3(file, fn, 4, &wn);
        CHECK(wrap && maktabasauti_mpc_desc.probe(&maktabasauti_mpc_desc, wrap, 12, NULL) == 0);
        CHECK(wrap && maktabasauti_mpc_desc.probe(&maktabasauti_mpc_desc, wrap, wn, NULL) == 90);
        r = do_open(&mem, wrap, wn, 1, &st, &info, &h);
        CHECK(r == MAKTABASAUTI_OK && info.total_frames == 882u);
        if (r == MAKTABASAUTI_OK && all) {
            r = read_all(st, all, 882, 200, 2, &got);
            CHECK(r == MAKTABASAUTI_OK && got == 882 && memcmp(all, refb, rn) == 0);
            maktabasauti_mpc_desc.close(st); st = NULL;
        }
        free(all);
        CHECK(mem.live == 0);
        wrap[5] = 0x01;
        r = do_open(&mem, wrap, wn, 1, &st, &info, &h);
        CHECK(r == MAKTABASAUTI_E_CORRUPT_DATA && st == NULL);
        CHECK(mem.live == 0);
        r = do_open(&mem, file, 80, 1, &st, &info, &h);
        CHECK(r == MAKTABASAUTI_OK);
        if (r == MAKTABASAUTI_OK) {
            float *tmp = (float *)calloc(882u * 2u, sizeof(float));
            r = read_all(st, tmp, 882, 256, 2, &got);
            CHECK(r == MAKTABASAUTI_E_CORRUPT_DATA && got < 882u);
            CHECK(maktabasauti_mpc_desc.last_error(st) != NULL);
            r = maktabasauti_mpc_desc.read(st, pcm, 1, &got);
            CHECK(r == MAKTABASAUTI_E_CORRUPT_DATA && got == 0);
            maktabasauti_mpc_desc.close(st); st = NULL;
            free(tmp);
        }
        CHECK(mem.live == 0 && mem.bad == 0);
    }
    free(wrap); free(file); free(refb);

    file = load("tests/data/mpc/sv8-tone.mpc", &fn);
    CHECK(file && fn > 80);
    if (file) {
        r = do_open(&mem, file, 80, 1, &st, &info, &h);
        CHECK(r != MAKTABASAUTI_OK && st == NULL);
        CHECK(mem.live == 0);
        file[16] ^= 0x01;
        r = do_open(&mem, file, fn, 1, &st, &info, &h);
        CHECK(r == MAKTABASAUTI_E_CORRUPT_DATA && st == NULL);
        CHECK(mem.live == 0 && mem.bad == 0);
    }
    free(file); file = NULL;

    {
        /* SH z kanalami = 3. CRC jest dobre, uklad kanalow nie jest okreslony. */
        static const uint8_t wide[] = {
            0x4d, 0x50, 0x43, 0x4b, 0x53, 0x48, 0x0c, 0x84, 0x3e, 0xfa, 0xca, 0x08, 0x10, 0x00, 0x01, 0x20
        };
        CHECK(maktabasauti_mpc_desc.probe(&maktabasauti_mpc_desc, wide, sizeof wide, NULL) == 90);
        r = do_open(&mem, wide, sizeof wide, 1, &st, &info, &h);
        CHECK(r == MAKTABASAUTI_E_UNSUPPORTED_FEATURE && st == NULL);
        CHECK(mem.live == 0 && mem.bad == 0);
    }

    if (g_failures) { printf("test_decoder_mpc: %d failures\n", g_failures); return 1; }
    printf("test_decoder_mpc: ok\n");
    return 0;
}
