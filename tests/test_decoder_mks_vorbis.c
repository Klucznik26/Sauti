/* Vorbis I. Wzorzec float to dekoder libvorbis (ffmpeg -c:a libvorbis przed -i).
 * Surowe f32le lezy w tests/data/vorbis. Priming nie jest wpisany na sztywno:
 * test bierze go z informacji o strumieniu i porownuje dlugosc z wzorcem. */
#include "maktabasauti/maktabasauti_codec.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const maktabasauti_codec_t maktabasauti_vorbis_desc;

static int g_failures;
#define CHECK(cond) do { if (!(cond)) { g_failures++; printf("FAIL %s:%d\n", __FILE__, __LINE__); } } while (0)

/* Ponizej tej roznicy traktujemy synteze jako zgodna z libvorbis.
 * Wartosc jest luzniejsza niz typowy blad float; skalowanie IMDCT ja przebija. */
static double TOL = 1e-4;          /* libvorbisfile (the reference decoder) agrees to 3e-8 on the real files and to a few 1e-6 on the synthetic ones whose samples reach 65 */

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
    return maktabasauti_vorbis_desc.open(&maktabasauti_vorbis_desc, h, NULL, info, st);
}

static maktabasauti_result_t read_all(void *st, float *dst, size_t cap, size_t step, unsigned ch, size_t *got) {
    size_t n = 0;
    while (n < cap) {
        size_t k = 0, ask = cap - n;
        maktabasauti_result_t r;
        if (ask > step) ask = step;
        r = maktabasauti_vorbis_desc.read(st, dst + n * ch, ask, &k);
        n += k;
        if (r != MAKTABASAUTI_OK) { *got = n; return r; }
    }
    *got = n;
    return MAKTABASAUTI_OK;
}

/* max |ours-ref| oraz ref/ours w probce o najwiekszym |ref|. */
static void diff_stats(const float *ours, const float *ref, size_t n, double *max_abs, double *ratio, size_t *at) {
    size_t i, ip = 0;
    double m = 0.0, peak = -1.0;
    for (i = 0; i < n; i++) {
        double d = fabs((double)ours[i] - (double)ref[i]);
        double ar = fabs((double)ref[i]);
        if (d > m) m = d;
        if (ar > peak) { peak = ar; ip = i; }
    }
    *max_abs = m;
    *at = ip;
    if (n && fabs((double)ours[ip]) > 1e-20) *ratio = (double)ref[ip] / (double)ours[ip];
    else *ratio = 0.0;
}

static void dump_pairs(const char *stem, const float *ours, const float *ref, size_t n, size_t at) {
    size_t i, shown = 0, a = at > 4 ? at - 4 : 0;
    printf("  %s worst=%zu\n", stem, at);
    for (i = a; i < n && i < a + 8u; i++)
        printf("  [%zu] ours=%.8g ref=%.8g\n", i, (double)ours[i], (double)ref[i]);
    for (i = 0; i < n && shown < 6u; i++) {
        if (fabs((double)ref[i]) < 1e-3) continue;
        printf("  loud[%zu] ours=%.8g ref=%.8g div=%.8g\n", i, (double)ours[i], (double)ref[i],
               fabs((double)ours[i]) > 1e-20 ? (double)ref[i] / (double)ours[i] : 0.0);
        shown++;
    }
}

static void check_file(const char *stem, const char *ogg_path, const char *f32_path, size_t step, int expect_tag) {
    uint8_t *file = NULL, *refb = NULL;
    size_t fn = 0, rn = 0, got = 0, cmp, at = 0, ref_frames;
    float *pcm = NULL, *ref = NULL;
    void *st = NULL;
    Mem mem;
    maktabasauti_host_t h;
    maktabasauti_stream_info_t info;
    maktabasauti_result_t r;
    uint64_t actual = 0, seek_at;
    unsigned ch;
    double max_abs = 0.0, ratio = 0.0;
    char tag[64];
    size_t needed = 0;

    file = load(ogg_path, &fn);
    refb = load(f32_path, &rn);
    CHECK(file && refb && fn > 27 && (rn % sizeof(float)) == 0);
    if (!file || !refb) { free(file); free(refb); return; }
    CHECK(maktabasauti_vorbis_desc.probe(&maktabasauti_vorbis_desc, file, fn < 64 ? fn : 64, NULL) == 90);
    r = do_open(&mem, file, fn, 0, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK && st);
    if (r != MAKTABASAUTI_OK || !st) {
        printf("%s open %d\n", stem, (int)r);
        free(file); free(refb);
        CHECK(mem.live == 0 && mem.bad == 0);
        return;
    }
    ch = info.channels;
    ref = (float *)refb;
    ref_frames = ch ? rn / (sizeof(float) * (size_t)ch) : 0;
    printf("%s rate=%u ch=%u frames=%llu ref_frames=%zu priming=%llu tail=%llu flags=0x%x mask=0x%llx\n",
           stem, info.sample_rate, ch, (unsigned long long)info.total_frames, ref_frames,
           (unsigned long long)info.priming_frames, (unsigned long long)info.end_padding_frames,
           info.flags, (unsigned long long)info.channel_mask);
    CHECK(ch >= 1u && ch <= 32u && info.sample_rate != 0);
    CHECK(info.native_format == MAKTABASAUTI_FORMAT_F32 && info.bits_per_sample == 0);
    CHECK((info.flags & MAKTABASAUTI_STREAM_LOSSLESS) == 0u);
    CHECK((info.flags & MAKTABASAUTI_STREAM_VBR) != 0u);
    CHECK((info.flags & MAKTABASAUTI_STREAM_SEEKABLE) != 0u);
    CHECK((info.flags & MAKTABASAUTI_STREAM_GAPLESS_TRIMMED) != 0u);
    if (ch == 1u) CHECK(info.channel_mask == MAKTABASAUTI_LAYOUT_MONO);
    else if (ch == 2u) CHECK(info.channel_mask == MAKTABASAUTI_LAYOUT_STEREO);
    else CHECK(info.channel_mask == 0u);
    CHECK(strcmp(info.codec_name, "vorbis") == 0);
    CHECK(strcmp(info.codec_profile, "Vorbis I") == 0);
    CHECK(strcmp(info.container_name, "ogg") == 0);
    CHECK(info.total_frames == ref_frames);
    pcm = (float *)calloc((ref_frames > (size_t)info.total_frames ? ref_frames : (size_t)info.total_frames) * (size_t)ch + 8u, sizeof(float));
    CHECK(pcm);
    if (!pcm) { maktabasauti_vorbis_desc.close(st); free(file); free(refb); return; }
    r = read_all(st, pcm, (size_t)info.total_frames, step, ch, &got);
    CHECK(r == MAKTABASAUTI_OK && got == (size_t)info.total_frames);
    cmp = got < ref_frames ? got : ref_frames;
    diff_stats(pcm, ref, cmp * (size_t)ch, &max_abs, &ratio, &at);
    printf("%s max_abs=%.8g ratio=%.8g at=%zu\n", stem, max_abs, ratio, at);
    if (max_abs > TOL) dump_pairs(stem, pcm, ref, cmp * (size_t)ch, at);
    CHECK(max_abs <= TOL);
    if (info.total_frames != ref_frames || got != (size_t)info.total_frames) {
        maktabasauti_vorbis_desc.close(st);
        free(pcm); free(file); free(refb);
        CHECK(mem.live == 0 && mem.bad == 0);
        return;
    }
    r = maktabasauti_vorbis_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_EOF && got == 0);
    CHECK(maktabasauti_vorbis_desc.seek(st, info.total_frames + 1u, 0, &actual) == MAKTABASAUTI_E_OUT_OF_RANGE);
    r = maktabasauti_vorbis_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_EOF && got == 0);
    CHECK(maktabasauti_vorbis_desc.seek(st, info.total_frames, 0, &actual) == MAKTABASAUTI_OK && actual == info.total_frames);
    r = maktabasauti_vorbis_desc.read(st, pcm, 1, &got);
    CHECK(r == MAKTABASAUTI_EOF && got == 0);
    seek_at = info.total_frames / 3u;
    CHECK(maktabasauti_vorbis_desc.seek(st, seek_at, 0, &actual) == MAKTABASAUTI_OK && actual == seek_at);
    r = read_all(st, pcm, (size_t)(info.total_frames - seek_at), step, ch, &got);
    CHECK(r == MAKTABASAUTI_OK && got == (size_t)(info.total_frames - seek_at));
    diff_stats(pcm, ref + seek_at * ch, got * (size_t)ch, &max_abs, &ratio, &at);
    printf("%s seek max_abs=%.8g ratio=%.8g\n", stem, max_abs, ratio);
    CHECK(max_abs <= TOL);
    CHECK(maktabasauti_vorbis_desc.seek(st, 0, 0, &actual) == MAKTABASAUTI_OK && actual == 0);
    CHECK(maktabasauti_vorbis_desc.reset(st) == MAKTABASAUTI_OK);
    r = read_all(st, pcm, 16, 3, ch, &got);
    CHECK(r == MAKTABASAUTI_OK && got == 16);
    diff_stats(pcm, ref, 16u * (size_t)ch, &max_abs, &ratio, &at);
    CHECK(max_abs <= TOL);
    needed = 0;
    r = maktabasauti_vorbis_desc.get_tag(st, "vendor", tag, 1, &needed);
    CHECK(r == MAKTABASAUTI_E_BUFFER_TOO_SMALL && needed > 1u);
    r = maktabasauti_vorbis_desc.get_tag(st, "vendor", tag, sizeof tag, &needed);
    CHECK(r == MAKTABASAUTI_OK && needed > 1u && tag[0] != 0);
    printf("%s vendor=%s\n", stem, tag);
    CHECK(maktabasauti_vorbis_desc.get_tag(st, "nosuch", tag, sizeof tag, &needed) == MAKTABASAUTI_E_UNSUPPORTED_FEATURE);
    if (expect_tag) {
        CHECK(maktabasauti_vorbis_desc.get_tag(st, "TiTlE", tag, sizeof tag, &needed) == MAKTABASAUTI_OK);
        CHECK(strcmp(tag, "Sauti") == 0);
        CHECK(maktabasauti_vorbis_desc.get_tag(st, "artist", tag, sizeof tag, &needed) == MAKTABASAUTI_OK);
        CHECK(strcmp(tag, "Probe") == 0);
    }
    maktabasauti_vorbis_desc.close(st);
    free(pcm); free(file); free(refb);
    CHECK(mem.live == 0 && mem.bad == 0);
}

/* Po wyczerpaniu linku nastepny odczyt zwraca 0 ramek i FORMAT_CHANGED. */
static void check_chain(const char *path, const char *a_f32, const char *b_f32, int same) {
    uint8_t *file, *ab, *bb;
    size_t fn = 0, an = 0, bn = 0, got = 0, at = 0;
    void *st = NULL;
    Mem mem;
    maktabasauti_host_t h;
    maktabasauti_stream_info_t info, info2;
    maktabasauti_result_t r;
    float *pcm;
    unsigned ch;
    double max_abs = 0.0, ratio = 0.0;
    file = load(path, &fn); ab = load(a_f32, &an); bb = load(b_f32, &bn);
    CHECK(file && ab && bb);
    if (!file || !ab || !bb) { free(file); free(ab); free(bb); return; }
    r = do_open(&mem, file, fn, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_OK && st);
    if (r != MAKTABASAUTI_OK) { free(file); free(ab); free(bb); CHECK(mem.live == 0); return; }
    ch = info.channels;
    pcm = (float *)calloc((an > bn ? an : bn) / sizeof(float) + 8u, sizeof(float));
    CHECK(pcm);
    if (pcm && ch) {
        size_t af = an / (sizeof(float) * (size_t)ch);
        CHECK(info.total_frames == af);
        r = read_all(st, pcm, af, 100, ch, &got);
        CHECK(r == MAKTABASAUTI_OK && got == af);
        diff_stats(pcm, (float *)ab, got * (size_t)ch, &max_abs, &ratio, &at);
        printf("chain-a max_abs=%.8g\n", max_abs);
        CHECK(max_abs <= TOL);
        r = maktabasauti_vorbis_desc.read(st, pcm, 8, &got);
        CHECK(r == MAKTABASAUTI_E_FORMAT_CHANGED && got == 0);
        CHECK(maktabasauti_vorbis_desc.get_info(st, &info2) == MAKTABASAUTI_OK);
        printf("chain-b rate=%u ch=%u frames=%llu\n", info2.sample_rate, info2.channels,
               (unsigned long long)info2.total_frames);
        if (same) CHECK(info2.sample_rate == info.sample_rate && info2.channels == info.channels);
        else CHECK(info2.channels != info.channels);
        ch = info2.channels;
        if (ch) {
            size_t bf = bn / (sizeof(float) * (size_t)ch);
            CHECK(info2.total_frames == bf);
            r = read_all(st, pcm, bf, 64, ch, &got);
            CHECK(r == MAKTABASAUTI_OK && got == bf);
            diff_stats(pcm, (float *)bb, got * (size_t)ch, &max_abs, &ratio, &at);
            printf("chain-b max_abs=%.8g\n", max_abs);
            CHECK(max_abs <= TOL);
            r = maktabasauti_vorbis_desc.read(st, pcm, 1, &got);
            CHECK(r == MAKTABASAUTI_EOF && got == 0);
        }
        CHECK(maktabasauti_vorbis_desc.reset(st) == MAKTABASAUTI_OK);
        CHECK(maktabasauti_vorbis_desc.get_info(st, &info2) == MAKTABASAUTI_OK);
        CHECK(info2.channels == info.channels && info2.sample_rate == info.sample_rate);
        CHECK(info2.total_frames == info.total_frames);
    }
    maktabasauti_vorbis_desc.close(st);
    free(pcm); free(file); free(ab); free(bb);
    CHECK(mem.live == 0 && mem.bad == 0);
}

static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Strona identyfikacyjna z wyzerowana suma CRC, jak syn.ogg w audycie sondy. */
static size_t make_bad_crc_page(uint8_t *p) {
    memset(p, 0, 58);
    memcpy(p, "OggS", 4);
    p[5] = 2;
    put_le32(p + 14, 1);
    p[26] = 1;
    p[27] = 30;
    p[28] = 1;
    memcpy(p + 29, "vorbis", 6);
    p[39] = 1;
    put_le32(p + 40, 44100);
    put_le32(p + 48, 64000);
    p[56] = 0xB8;
    p[57] = 1;
    return 58;
}


/* ---- damaged and multiplexed files (the behaviour of libogg / libvorbisfile: bad pages are lost, other streams are ignored, a cut file plays to its last whole page) ------------------------ */
static uint32_t ogg_crc(const uint8_t *p, size_t n) {
    uint32_t c = 0, t, i; size_t k;
    for (k = 0; k < n; k++) {
        t = (c >> 24) ^ p[k];
        for (i = 0; i < 8; i++) t = (t & 0x80u) ? (t << 1) ^ 0x04c11db7u : (t << 1);
        c = (c << 8) ^ t;
    }
    return c;
}
static void seal_page(uint8_t *pg, size_t n) { memset(pg + 22, 0, 4); put_le32(pg + 22, ogg_crc(pg, n)); }
/* one page with `body` bytes in a single packet of at most 254 bytes */
static size_t make_page(uint8_t *out, uint32_t serial, uint32_t seq, int flags, int64_t granule, const uint8_t *body, size_t n) {
    size_t i;
    memset(out, 0, 28 + n);
    memcpy(out, "OggS", 4); out[5] = (uint8_t)flags;
    for (i = 0; i < 8; i++) out[6 + i] = (uint8_t)((uint64_t)granule >> (8 * i));
    put_le32(out + 14, serial); put_le32(out + 18, seq);
    out[26] = 1; out[27] = (uint8_t)n; memcpy(out + 28, body, n);
    seal_page(out, 28 + n);
    return 28 + n;
}
typedef struct { size_t off, len; } PageAt;
static size_t split_pages(const uint8_t *d, size_t n, PageAt *pg, size_t max) {
    size_t at = 0, k = 0;
    while (at + 27 <= n && k < max) {
        size_t segs = d[at + 26], body = 0, i;
        for (i = 0; i < segs; i++) body += d[at + 27 + i];
        pg[k].off = at; pg[k].len = 27 + segs + body; at += pg[k].len; k++;
    }
    return k;
}
static float *decode_whole(const uint8_t *file, size_t fn, size_t *frames, unsigned *ch, maktabasauti_result_t *final) {
    Mem mem; maktabasauti_host_t h; maktabasauti_stream_info_t info; void *st = NULL; float *pcm; size_t got = 0;
    maktabasauti_result_t r = do_open(&mem, file, fn, 1, &st, &info, &h);
    *frames = 0; *ch = 0; *final = r;
    if (r != MAKTABASAUTI_OK) return NULL;
    *ch = info.channels;
    pcm = (float *)calloc((size_t)info.total_frames * info.channels + 8u, sizeof(float));
    if (pcm) { *final = read_all(st, pcm, (size_t)info.total_frames + 64u, 512, info.channels, &got); *frames = got; }
    maktabasauti_vorbis_desc.close(st);
    CHECK(mem.live == 0 && mem.bad == 0);
    return pcm;
}
static void test_damage(void) {
    size_t fn = 0, full_n, trunc_n, k; unsigned ch, ch2; maktabasauti_result_t fin;
    uint8_t *file = load("tests/data/vorbis/pages.ogg", &fn), *mod = NULL;
    PageAt pg[64]; size_t np;
    float *full, *part;
    CHECK(file != NULL);
    if (!file) return;
    np = split_pages(file, fn, pg, 64);
    CHECK(np >= 20u);
    full = decode_whole(file, fn, &full_n, &ch, &fin);
    CHECK(full && fin == MAKTABASAUTI_EOF && full_n > 10000u && ch == 2u);
    mod = (uint8_t *)malloc(fn + 4096u);
    if (!full || !mod || np < 20u) { free(full); free(mod); free(file); return; }

    /* a file that ends inside a page (and exactly between pages): plays up to the last whole page, then reports the truncation; what was played equals the start of the whole file */
    for (k = 0; k < 3; k++) {
        size_t cut = k == 0 ? pg[np / 2].off + pg[np / 2].len / 2u : k == 1 ? pg[np - 4].off : fn - 1u;
        part = decode_whole(file, cut, &trunc_n, &ch2, &fin);
        CHECK(part && fin == MAKTABASAUTI_E_TRUNCATED && ch2 == 2u && trunc_n > 1000u && trunc_n < full_n);
        CHECK(part && memcmp(part, full, trunc_n * 2u * sizeof(float)) == 0);
        free(part);
    }
    /* pages of another logical stream in between (a Theora-like BOS, its data pages and EOS), a serial that repeats: ignored */
    {
        uint8_t other[256], *w = mod; size_t i, o = 0; static const uint8_t theora[] = "\x80theora-----";
        o += make_page(w + o, 4242u, 0, 2, 0, theora, sizeof theora - 1u);                    /* BOS of the foreign stream first, as in grouped streams */
        for (i = 0; i < np; i++) {
            memcpy(w + o, file + pg[i].off, pg[i].len); o += pg[i].len;
            if (i >= 2u && i % 5u == 0u) { memset(other, 0x55, sizeof other); o += make_page(w + o, 4242u, (uint32_t)i, 0, 100, other, 100); }
        }
        o += make_page(w + o, 4242u, 99u, 4, 100, theora, 4);
        part = decode_whole(w, o, &trunc_n, &ch2, &fin);
        CHECK(part && fin == MAKTABASAUTI_EOF && trunc_n == full_n && memcmp(part, full, full_n * 2u * sizeof(float)) == 0);
        free(part);
    }
    /* junk before the first page of a later link, after the last page, and between pages: skipped */
    {
        size_t o = 0, i;
        for (i = 0; i < np; i++) {
            memcpy(mod + o, file + pg[i].off, pg[i].len); o += pg[i].len;
            if (i == 6u) { memset(mod + o, 0x7e, 37); o += 37; }
        }
        memset(mod + o, 0x11, 600); o += 600;                                                  /* a tag after the end of the stream */
        part = decode_whole(mod, o, &trunc_n, &ch2, &fin);
        CHECK(part && trunc_n >= full_n - 3000u && trunc_n <= full_n);                         /* the junk cut one packet: a hole, never a failure */
        CHECK(part && memcmp(part, full, 2000u * 2u * sizeof(float)) == 0);                    /* the audio before the junk is intact */
        free(part);
    }
    /* a lost page and a page with a wrong CRC: a hole, the rest plays, the start is intact */
    for (k = 0; k < 2; k++) {
        size_t o = 0, i;
        for (i = 0; i < np; i++) {
            if (i == 12u && k == 0u) continue;
            memcpy(mod + o, file + pg[i].off, pg[i].len);
            if (i == 12u && k == 1u) mod[o + 30] ^= 0x40;
            o += pg[i].len;
        }
        part = decode_whole(mod, o, &trunc_n, &ch2, &fin);
        CHECK(part && trunc_n > full_n / 2u && trunc_n <= full_n && ch2 == 2u && fin == MAKTABASAUTI_EOF);
        CHECK(part && memcmp(part, full, 4000u * 2u * sizeof(float)) == 0);
        free(part);
    }
    /* the identification page lost: nothing to decode */
    {
        size_t o = 0, i;
        for (i = 1; i < np; i++) { memcpy(mod + o, file + pg[i].off, pg[i].len); o += pg[i].len; }
        part = decode_whole(mod, o, &trunc_n, &ch2, &fin);
        CHECK(part == NULL && fin == MAKTABASAUTI_E_UNSUPPORTED_FORMAT);
    }

    /* Packets that span pages: every page of cont.ogg in turn is lost.  The number of frames that play equals what libvorbisfile plays for the same damaged file (a hole drops the packet that
     * was being assembled and the continuation of it on the next page); the audio before the hole is untouched. */
    {
        static const size_t want[8] = { 0, 0, 3712, 3328, 3328, 3328, 3584, 4608 };
        size_t cn = 0, cf, o, i, j, cnp; unsigned cch; maktabasauti_result_t cfin;
        uint8_t *cfile = load("tests/data/vorbis/cont.ogg", &cn), *cm = (uint8_t *)malloc(cn + 64u);
        PageAt cpg[16]; float *cfull;
        CHECK(cfile && cm);
        if (cfile && cm) {
            cnp = split_pages(cfile, cn, cpg, 16);
            CHECK(cnp == 8u);
            cfull = decode_whole(cfile, cn, &cf, &cch, &cfin);
            CHECK(cfull && cf == 4851u && cfin == MAKTABASAUTI_EOF);
            for (j = 2; j < cnp && cfull; j++) {
                float *cp; size_t cgot;
                for (i = 0, o = 0; i < cnp; i++) { if (i == j) continue; memcpy(cm + o, cfile + cpg[i].off, cpg[i].len); o += cpg[i].len; }
                cp = decode_whole(cm, o, &cgot, &cch, &cfin);
                CHECK(cp && cgot == want[j]);
                CHECK(cp && cfin == (j == 7u ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_EOF));
                if (j >= 3u) CHECK(cp && memcmp(cp, cfull, 200u * 2u * sizeof(float)) == 0);
                free(cp);
            }
            free(cfull);
        }
        free(cm); free(cfile);
    }
    /* extreme floor 0 (clustered line spectrum pairs, a huge amplitude offset): libvorbis returns 2880 NaN samples for this file, the output here stays finite */
    {
        size_t en = 0, ef, q; unsigned ech; maktabasauti_result_t efin; int finite = 1;
        uint8_t *ef_file = load("tests/data/vorbis/syn_extreme.ogg", &en);
        float *ep = ef_file ? decode_whole(ef_file, en, &ef, &ech, &efin) : NULL;
        CHECK(ep && ef == 4736u && ech == 2u);
        for (q = 0; ep && q < ef * 2u; q++) if (!(ep[q] - ep[q] == 0.f)) finite = 0;
        CHECK(finite);
        free(ep); free(ef_file);
    }
    free(full); free(mod); free(file);
}

int main(void) {
    uint8_t *file = NULL, *wrap = NULL, badpage[64], opus[40];
    setvbuf(stdout, NULL, _IOLBF, 0);
    size_t fn = 0, got = 0;
    void *st = NULL;
    Mem mem;
    maktabasauti_host_t h;
    maktabasauti_stream_info_t info;
    maktabasauti_result_t r;
    float one = 0.f;
    static const uint8_t other[] = "FORM\0\0\0\08SVX";

    CHECK(maktabasauti_vorbis_desc.probe(&maktabasauti_vorbis_desc, other, sizeof other - 1, NULL) == 0);
    CHECK(maktabasauti_vorbis_desc.probe(&maktabasauti_vorbis_desc, (const uint8_t *)"fLaC", 4, NULL) == 0);
    CHECK(maktabasauti_vorbis_desc.probe(&maktabasauti_vorbis_desc, (const uint8_t *)"RIFF", 4, NULL) == 0);
    CHECK(maktabasauti_vorbis_desc.probe(&maktabasauti_vorbis_desc, (const uint8_t *)"ID3\x03", 4, NULL) == 0);
    memset(opus, 0, sizeof opus);
    memcpy(opus, "OggS", 4);
    opus[26] = 1;
    opus[27] = 8;
    memcpy(opus + 28, "OpusHead", 8);
    CHECK(maktabasauti_vorbis_desc.probe(&maktabasauti_vorbis_desc, opus, sizeof opus, NULL) == 0);

    make_bad_crc_page(badpage);
    CHECK(maktabasauti_vorbis_desc.probe(&maktabasauti_vorbis_desc, badpage, 58, NULL) == 90);
    r = do_open(&mem, badpage, 58, 1, &st, &info, &h);
    CHECK(r == MAKTABASAUTI_E_UNSUPPORTED_FORMAT && st == NULL);            /* the only page is lost (wrong CRC): there is no Vorbis stream */
    CHECK(mem.live == 0 && mem.bad == 0);

    check_file("sine", "tests/data/vorbis/sine.ogg", "tests/data/vorbis/sine.f32", 64, 1);
    check_file("mono", "tests/data/vorbis/mono.ogg", "tests/data/vorbis/mono.f32", 1, 0);
    check_file("click", "tests/data/vorbis/click.ogg", "tests/data/vorbis/click.f32", 17, 0);
    check_file("low", "tests/data/vorbis/low.ogg", "tests/data/vorbis/low.f32", 100, 0);
    check_file("probe_real", "tests/data/probe_real.ogg", "tests/data/vorbis/probe_real.f32", 50, 0);
    check_file("ch3", "tests/data/vorbis/ch3.ogg", "tests/data/vorbis/ch3.f32", 33, 0);
    check_file("ch6", "tests/data/vorbis/ch6.ogg", "tests/data/vorbis/ch6.f32", 40, 0);       /* libvorbis writes a floor1 without partitions here (the LFE channel) */
    check_file("ch8", "tests/data/vorbis/ch8.ogg", "tests/data/vorbis/ch8.f32", 41, 0);
    /* hand-made setups (residue types 0 and 2, coupling, three and six channels, long and short blocks) written by the test generator of the audit, decoded by libvorbisfile for the references */
    check_file("syn_r0_ch2c", "tests/data/vorbis/syn_r0_ch2c.ogg", "tests/data/vorbis/syn_r0_ch2c.f32", 29, 0);
    check_file("syn_r2_ch3", "tests/data/vorbis/syn_r2_ch3.ogg", "tests/data/vorbis/syn_r2_ch3.f32", 31, 0);
    check_file("syn_r0_ch6", "tests/data/vorbis/syn_r0_ch6.ogg", "tests/data/vorbis/syn_r0_ch6.f32", 37, 0);
    check_file("syn_r2_ch2c", "tests/data/vorbis/syn_r2_ch2c.ogg", "tests/data/vorbis/syn_r2_ch2c.f32", 1, 0);
    /* floor type 0 (LSP, spec 6.2): libvorbis evaluates the curve with table lookups, so it agrees with the formula of the specification to about 1e-6 relative; the samples reach 7000 */
    TOL = 2e-3;
    /* packets that span pages (continued pages), written by re-paging a libvorbis stream with six segments per page */
    check_file("cont", "tests/data/vorbis/cont.ogg", "tests/data/vorbis/cont.f32", 77, 0);
    check_file("syn_floor0", "tests/data/vorbis/syn_floor0.ogg", "tests/data/vorbis/syn_floor0.f32", 53, 0);
    TOL = 1e-4;

    file = load("tests/data/vorbis/sine.ogg", &fn);
    CHECK(file && fn > 40);
    if (file) {
        uint8_t *cut = (uint8_t *)malloc(fn);
        CHECK(cut);
        if (cut) {
            memcpy(cut, file, fn);
            cut[22] ^= 0xff;
            r = do_open(&mem, cut, fn, 1, &st, &info, &h);
            CHECK(r == MAKTABASAUTI_E_UNSUPPORTED_FORMAT && st == NULL);          /* the identification page is lost: no Vorbis stream (the later pages belong to nobody) */
            CHECK(mem.live == 0 && mem.bad == 0);
            r = do_open(&mem, file, 20, 1, &st, &info, &h);
            CHECK(r == MAKTABASAUTI_E_TRUNCATED && st == NULL);
            CHECK(mem.live == 0 && mem.bad == 0);
            r = do_open(&mem, file, fn / 2u, 1, &st, &info, &h);
            CHECK(r != MAKTABASAUTI_OK && st == NULL);
            CHECK(mem.live == 0 && mem.bad == 0);
            wrap = (uint8_t *)malloc(10u + fn);
            CHECK(wrap);
            if (wrap) {
                memset(wrap, 0, 10);
                memcpy(wrap, "ID3", 3);
                wrap[3] = 3;
                memcpy(wrap + 10, file, fn);
                CHECK(maktabasauti_vorbis_desc.probe(&maktabasauti_vorbis_desc, wrap, 16, NULL) == 0);
                r = do_open(&mem, wrap, 10u + fn, 1, &st, &info, &h);
                CHECK(r == MAKTABASAUTI_E_UNSUPPORTED_FORMAT && st == NULL);
                CHECK(mem.live == 0 && mem.bad == 0);
            }
            free(wrap);
        }
        free(cut);
        r = do_open(&mem, file, 0, 1, &st, &info, &h);
        CHECK(r == MAKTABASAUTI_E_TRUNCATED && st == NULL);
        CHECK(mem.live == 0);
    }
    free(file);

    check_chain("tests/data/vorbis/chain.ogg", "tests/data/vorbis/mono.f32", "tests/data/vorbis/sine.f32", 0);
    check_chain("tests/data/vorbis/chain-same.ogg", "tests/data/vorbis/sine.f32", "tests/data/vorbis/sine.f32", 1);

    /* Wejscie nieprzewijalne: plik i tak jest w pamieci, seek strumienia dziala. */
    file = load("tests/data/vorbis/mono.ogg", &fn);
    if (file) {
        r = do_open(&mem, file, fn, 0, &st, &info, &h);
        CHECK(r == MAKTABASAUTI_OK && (info.flags & MAKTABASAUTI_STREAM_SEEKABLE));
        if (r == MAKTABASAUTI_OK) {
            uint64_t actual = 0;
            CHECK(maktabasauti_vorbis_desc.seek(st, 0, 0, &actual) == MAKTABASAUTI_OK);
            r = maktabasauti_vorbis_desc.read(st, &one, 1, &got);
            CHECK(r == MAKTABASAUTI_OK && got == 1);
            maktabasauti_vorbis_desc.close(st);
        }
        CHECK(mem.live == 0 && mem.bad == 0);
    }
    free(file);

    test_damage();

    if (g_failures) printf("test_decoder_mks_vorbis: %d failures\n", g_failures);
    else printf("test_decoder_mks_vorbis: ok\n");
    return g_failures ? 1 : 0;
}
