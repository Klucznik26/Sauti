/* AAC in Matroska / WebM and in MPEG-2 transport streams (maktabasauti_aac_mkv.c, maktabasauti_aac_ts.c).  The containers are BUILT HERE from the access units of tests/data/aac_tone_st.aac (ADTS, a
 * synthetic tone made for this repository): Matroska with an unknown-size Segment and Cluster, SimpleBlock and BlockGroup / Block, Xiph, EBML and fixed-size lacing, without CodecPrivate
 * (ASC made from the CodecID); TS with 188-, 192- (M2TS) and 204-byte packets, a PMT that spans two packets, a stuffed adaptation field.  The PCM of every container must equal the PCM of the ADTS
 * file bit for bit, seeking must agree with sequential decoding, and damaged / unsupported containers must be refused. */
#include "maktabasauti/maktabasauti.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
#define CHECK(c, ...) do { if (!(c)) { g_failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef struct { uint8_t *p; size_t n, cap; } Buf;
static void put(Buf *b, const void *d, size_t n) { if (b->n + n > b->cap) { b->cap = (b->n + n) * 2u + 64u; b->p = (uint8_t *)realloc(b->p, b->cap); } if (n) memcpy(b->p + b->n, d, n); b->n += n; }
static void put8(Buf *b, unsigned v) { uint8_t c = (uint8_t)v; put(b, &c, 1); }
static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); uint8_t *b; long len;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET); b = (uint8_t *)malloc((size_t)len + 1u);
    if (!b || fread(b, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(b); return NULL; }
    fclose(f); *n = (size_t)len; return b;
}
static maktabasauti_decoder_t *open_mem(const uint8_t *data, size_t n, maktabasauti_stream_info_t *info, maktabasauti_result_t *res) {
    maktabasauti_open_params_t p; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r;
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION;
    if (maktabasauti_io_from_memory(data, n, NULL, &p.io) != MAKTABASAUTI_OK) return NULL;
    r = maktabasauti_decoder_open(&p, &d); if (res) *res = r;
    if (r != MAKTABASAUTI_OK) { p.io.close(p.io.user); return NULL; }
    if (info) { info->struct_size = sizeof *info; (void)maktabasauti_decoder_get_info(d, info); }
    return d;
}
static float *decode_all(const uint8_t *data, size_t n, size_t *frames, const char **container) {
    maktabasauti_stream_info_t info; maktabasauti_decoder_t *d = open_mem(data, n, &info, NULL); float *pcm = NULL, buf[2 * 1024]; size_t total = 0, got, cap = 0; maktabasauti_result_t r;
    static char cname[32];
    *frames = 0; if (container) *container = NULL;
    if (!d) return NULL;
    if (container) { strncpy(cname, info.container_name, sizeof cname - 1u); *container = cname; }
    CHECK(info.channels == 2u && info.sample_rate == 44100u, "stream parameters %u ch %u Hz", (unsigned)info.channels, (unsigned)info.sample_rate);
    for (;;) {
        r = maktabasauti_decoder_read(d, buf, 1024, MAKTABASAUTI_FORMAT_F32, &got);
        if (got) { if (total + got > cap) { cap = (total + got) * 2u; pcm = (float *)realloc(pcm, cap * 2u * sizeof(float)); } memcpy(pcm + total * 2u, buf, got * 2u * sizeof(float)); total += got; }
        if (r == MAKTABASAUTI_EOF) break;
        if (r != MAKTABASAUTI_OK) { CHECK(0, "read -> %d", (int)r); break; }
    }
    maktabasauti_decoder_close(d); *frames = total; return pcm;
}

static float *decode_loas(const uint8_t *data, size_t n, size_t *frames) {
    maktabasauti_stream_info_t info; maktabasauti_decoder_t *d = open_mem(data, n, &info, NULL); float *pcm = NULL, buf[2 * 1024]; size_t total = 0, got, cap = 0; maktabasauti_result_t r;
    *frames = 0;
    if (!d || info.channels != 2u) { if (d) maktabasauti_decoder_close(d); return NULL; }
    for (;;) {
        r = maktabasauti_decoder_read(d, buf, 1024, MAKTABASAUTI_FORMAT_F32, &got);
        if (got) { if (total + got > cap) { cap = (total + got) * 2u; pcm = (float *)realloc(pcm, cap * 2u * sizeof(float)); } memcpy(pcm + total * 2u, buf, got * 2u * sizeof(float)); total += got; }
        if (r != MAKTABASAUTI_OK) break;
    }
    maktabasauti_decoder_close(d); *frames = total; return pcm;
}
/* ---- the access units of the ADTS file -------------------------------------------------------------------------------------------------------------------------------------------------- */
typedef struct { const uint8_t *p; size_t n; } Au;
static size_t split_adts(const uint8_t *d, size_t n, Au *aus, size_t max) {
    size_t at = 0, k = 0;
    while (at + 7u <= n && k < max) {
        const size_t fl = (((size_t)d[at + 3] & 3u) << 11) | ((size_t)d[at + 4] << 3) | (d[at + 5] >> 5);
        if (d[at] != 0xffu || (d[at + 1] & 0xf0u) != 0xf0u || fl < 7u || at + fl > n) break;
        aus[k].p = d + at + 7u; aus[k].n = fl - 7u; k++; at += fl;
    }
    return k;
}
/* ---- Matroska builder ------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
static void ebml_id(Buf *b, uint32_t id) { if (id > 0xffffffu) put8(b, id >> 24); if (id > 0xffffu) put8(b, id >> 16); if (id > 0xffu) put8(b, id >> 8); put8(b, id); }
static void ebml_size(Buf *b, uint64_t s) { int i; put8(b, 0x01); for (i = 6; i >= 0; i--) put8(b, (unsigned)(s >> (8 * i))); }        /* 8-byte form */
static void ebml_unknown(Buf *b) { put8(b, 0x01); for (int i = 0; i < 7; i++) put8(b, 0xff); }
static void el(Buf *b, uint32_t id, const void *d, size_t n) { ebml_id(b, id); ebml_size(b, n); put(b, d, n); }
static void el_uint(Buf *b, uint32_t id, uint64_t v) { uint8_t t[8]; int i; for (i = 0; i < 8; i++) t[i] = (uint8_t)(v >> (56 - 8 * i)); el(b, id, t, 8); }
static void el_float(Buf *b, uint32_t id, double v) { uint8_t t[8]; uint64_t u; int i; memcpy(&u, &v, 8); for (i = 0; i < 8; i++) t[i] = (uint8_t)(u >> (56 - 8 * i)); el(b, id, t, 8); }
enum { LACE_NONE, LACE_XIPH, LACE_EBML, LACE_FIXED };
typedef struct { int lacing, group, with_private, unknown_sizes, other_track, tracks_after_cluster; } MkvOpt;
static void block(Buf *out, int group, unsigned track, const Au *aus, size_t n, int lacing) {
    Buf b = { 0 }; size_t i; put8(&b, 0x80u | track); put8(&b, 0); put8(&b, 0);
    if (lacing == LACE_NONE) { put8(&b, 0); put(&b, aus[0].p, aus[0].n); }
    else {
        put8(&b, lacing == LACE_XIPH ? 2 : lacing == LACE_EBML ? 6 : 4); put8(&b, (unsigned)n - 1u);
        if (lacing == LACE_XIPH) for (i = 0; i + 1u < n; i++) { size_t s = aus[i].n; while (s >= 255u) { put8(&b, 255); s -= 255u; } put8(&b, (unsigned)s); }
        if (lacing == LACE_EBML) {
            int64_t prev = 0;
            for (i = 0; i + 1u < n; i++) {
                if (i == 0) { put8(&b, 0x40u | (unsigned)(aus[0].n >> 8)); put8(&b, (unsigned)(aus[0].n & 0xffu)); prev = (int64_t)aus[0].n; }          /* 2-byte unsigned */
                else { const int64_t diff = (int64_t)aus[i].n - prev; const unsigned v = (unsigned)(diff + 8191); put8(&b, 0x40u | (v >> 8)); put8(&b, v & 0xffu); prev = (int64_t)aus[i].n; }
            }
        }
        for (i = 0; i < n; i++) put(&b, aus[i].p, aus[i].n);
    }
    if (group) { Buf g = { 0 }; el(&g, 0xA1, b.p, b.n); el(out, 0xA0, g.p, g.n); free(g.p); } else el(out, 0xA3, b.p, b.n);
    free(b.p);
}
static Buf build_mkv(const Au *aus, size_t n, const MkvOpt *o) {
    Buf f = { 0 }, seg = { 0 }, tr = { 0 }, te = { 0 }, au = { 0 }, cl = { 0 }; size_t i;
    static const uint8_t asc[2] = { 0x12, 0x10 };                                   /* LC, 44100, stereo */
    el(&f, 0x1A45DFA3, "\x42\x82\x88matroska", 11);                                 /* EBML header with DocType */
    /* Tracks */
    el_uint(&te, 0xD7, 1); el_uint(&te, 0x83, 2); el(&te, 0x86, o->with_private ? "A_AAC" : "A_AAC/MPEG4/LC", o->with_private ? 5 : 14);
    if (o->with_private) el(&te, 0x63A2, asc, 2);
    el_float(&au, 0xB5, 44100.0); el_uint(&au, 0x9F, 2); el(&te, 0xE1, au.p, au.n);
    { Buf t0 = { 0 }; el_uint(&t0, 0xD7, 2); el_uint(&t0, 0x83, 1); el(&t0, 0x86, "V_UNCOMPRESSED", 14); el(&tr, 0xAE, t0.p, t0.n); free(t0.p); }
    el(&tr, 0xAE, te.p, te.n);
    if (!o->tracks_after_cluster) el(&seg, 0x1654AE6B, tr.p, tr.n);
    /* cluster */
    el_uint(&cl, 0xE7, 0);
    if (o->other_track) { const uint8_t junk[40] = { 0 }; Au v = { junk, sizeof junk }; block(&cl, 0, 2, &v, 1, LACE_NONE); }
    if (o->lacing == LACE_NONE) for (i = 0; i < n; i++) block(&cl, o->group, 1, &aus[i], 1, LACE_NONE);
    else if (o->lacing == LACE_FIXED) for (i = 0; i < n; i++) {                       /* the same access unit twice: equal sizes, valid AAC */
        Au two[2]; two[0] = aus[i]; two[1] = aus[i]; block(&cl, 0, 1, two, 2, LACE_FIXED);
    }
    else { for (i = 0; i + 2u < n; i += 3u) block(&cl, o->group, 1, &aus[i], 3, o->lacing); for (; i < n; i++) block(&cl, o->group, 1, &aus[i], 1, LACE_NONE); }
    ebml_id(&seg, 0x1F43B675); if (o->unknown_sizes) ebml_unknown(&seg); else ebml_size(&seg, cl.n); put(&seg, cl.p, cl.n);
    if (o->tracks_after_cluster) el(&seg, 0x1654AE6B, tr.p, tr.n);
    ebml_id(&f, 0x18538067); if (o->unknown_sizes) ebml_unknown(&f); else ebml_size(&f, seg.n); put(&f, seg.p, seg.n);
    free(seg.p); free(tr.p); free(te.p); free(au.p); free(cl.p);
    return f;
}
/* ---- TS builder ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
static void ts_packet(Buf *out, unsigned psz, unsigned pid, int pusi, const uint8_t *pl, size_t n, unsigned *cc, int stuff_af) {
    uint8_t pk[204]; size_t off = 4, room;
    memset(pk, 0xff, sizeof pk);
    if (psz == 192u) { memset(pk, 0, 4); off += 4u; }
    pk[psz == 192u ? 4 : 0] = 0x47; { uint8_t *h = pk + (psz == 192u ? 4 : 0); h[1] = (uint8_t)((pusi ? 0x40 : 0) | (pid >> 8)); h[2] = (uint8_t)pid; }
    room = 188u - 4u;
    { uint8_t *h = pk + (psz == 192u ? 4 : 0);
      if (n < room || stuff_af) {                                              /* adaptation field with stuffing so that the payload fits exactly */
          size_t afl = room - n; size_t i;
          h[3] = (uint8_t)(0x30u | (*cc & 15u));
          if (afl == 1u) h[4] = 0; else { h[4] = (uint8_t)(afl - 1u); h[5] = 0; for (i = 2; i < afl; i++) h[4 + i] = 0xff; }
          memcpy(h + 4 + afl, pl, n);
      } else { h[3] = (uint8_t)(0x10u | (*cc & 15u)); memcpy(h + 4, pl, n); }
    }
    (*cc)++;
    (void)off; put(out, pk, psz);
}
static uint32_t crc32_mpeg(const uint8_t *p, size_t n) { uint32_t c = 0xffffffffu; size_t i; int k; for (i = 0; i < n; i++) { c ^= (uint32_t)p[i] << 24; for (k = 0; k < 8; k++) c = (c & 0x80000000u) ? (c << 1) ^ 0x04c11db7u : c << 1; } return c; }
static void section(Buf *out, unsigned psz, unsigned pid, const uint8_t *sec, size_t n, unsigned *cc, size_t split) {
    uint8_t pl[400]; size_t len = 0;
    pl[len++] = 0; memcpy(pl + len, sec, n); len += n;                          /* pointer_field 0 */
    if (!split || split >= len) { ts_packet(out, psz, pid, 1, pl, len, cc, 0); return; }
    ts_packet(out, psz, pid, 1, pl, split, cc, 0); ts_packet(out, psz, pid, 0, pl + split, len - split, cc, 0);
}
typedef struct { unsigned psz; int split_pmt, es_type; } TsOpt;
static Buf build_ts(const uint8_t *adts, size_t n, const TsOpt *o) {
    Buf out = { 0 }; uint8_t sec[400]; size_t len; uint32_t crc; unsigned ccp = 0, ccm = 0, cca = 0; size_t at = 0, pes_left; Buf pes = { 0 };
    len = 0; sec[len++] = 0; sec[len++] = 0xB0; sec[len++] = 13; sec[len++] = 0; sec[len++] = 1; sec[len++] = 0xC1; sec[len++] = 0; sec[len++] = 0; sec[len++] = 0; sec[len++] = 1; sec[len++] = 0xE0; sec[len++] = 0x20;   /* PAT: program 1 -> PMT PID 0x20 */
    crc = crc32_mpeg(sec, len); sec[len++] = (uint8_t)(crc >> 24); sec[len++] = (uint8_t)(crc >> 16); sec[len++] = (uint8_t)(crc >> 8); sec[len++] = (uint8_t)crc;
    section(&out, o->psz, 0, sec, len, &ccp, 0);
    len = 0; sec[len++] = 2; sec[len++] = 0xB0; sec[len++] = 0; sec[len++] = 0; sec[len++] = 1; sec[len++] = 0xC1; sec[len++] = 0; sec[len++] = 0; sec[len++] = 0xE0; sec[len++] = 0x31; sec[len++] = 0xF0; sec[len++] = 0;
    sec[len++] = 0x1B; sec[len++] = 0xE0; sec[len++] = 0x30; sec[len++] = 0xF0; sec[len++] = 3; sec[len++] = 0x52; sec[len++] = 1; sec[len++] = 0; /* a video stream with a 3-byte descriptor */
    sec[len++] = (uint8_t)o->es_type; sec[len++] = 0xE0; sec[len++] = 0x31; sec[len++] = 0xF0; sec[len++] = 0;
    sec[2] = (uint8_t)(len - 3u + 4u);
    crc = crc32_mpeg(sec, len); sec[len++] = (uint8_t)(crc >> 24); sec[len++] = (uint8_t)(crc >> 16); sec[len++] = (uint8_t)(crc >> 8); sec[len++] = (uint8_t)crc;
    section(&out, o->psz, 0x20, sec, len, &ccm, o->split_pmt ? 20 : 0);
    /* audio PES packets of ~10 frames, the video PID gets a few packets in between */
    while (at < n) {
        size_t chunk = 0, frames = 0; uint8_t hdr[14]; size_t k;
        if (o->es_type == 0x11) { chunk = n - at < 1000u ? n - at : 1000u; frames = 1; }
        else while (at + chunk + 7u <= n && frames < 10u) { const size_t fl = (((size_t)adts[at + chunk + 3] & 3u) << 11) | ((size_t)adts[at + chunk + 4] << 3) | (adts[at + chunk + 5] >> 5); if (at + chunk + fl > n) break; chunk += fl; frames++; }
        if (!chunk) break;
        pes.n = 0; hdr[0] = 0; hdr[1] = 0; hdr[2] = 1; hdr[3] = 0xC0; hdr[4] = 0; hdr[5] = 0; hdr[6] = 0x80; hdr[7] = 0x80; hdr[8] = 5; for (k = 9; k < 14; k++) hdr[k] = 0x21;     /* PES header with a PTS */
        put(&pes, hdr, 14); put(&pes, adts + at, chunk); at += chunk;
        for (k = 0, pes_left = pes.n; pes_left; ) {
            const size_t take = pes_left < 184u ? pes_left : 184u; uint8_t video[184]; unsigned ccv = 0;
            memset(video, 0x11, sizeof video);
            ts_packet(&out, o->psz, 0x31, k == 0, pes.p + k, take, &cca, 0);
            if (k == 0) { ts_packet(&out, o->psz, 0x30, 1, video, sizeof video, &ccv, 0); }
            k += take; pes_left -= take;
        }
    }
    free(pes.p);
    return out;
}

static int same_pcm(const float *a, size_t na, const float *b, size_t nb) { return a && b && na == nb && !memcmp(a, b, na * 2u * sizeof(float)); }
static void expect_equal(const char *what, const uint8_t *data, size_t n, const float *ref, size_t ref_n, const char *want_container) {
    size_t frames = 0; const char *cont; float *pcm = decode_all(data, n, &frames, &cont);
    CHECK(pcm != NULL, "%s: does not open", what);
    if (!pcm) return;
    CHECK(cont && !strcmp(cont, want_container), "%s: container name %s", what, cont ? cont : "(null)");
    CHECK(same_pcm(pcm, frames, ref, ref_n), "%s: PCM differs from the ADTS decode (%zu / %zu frames)", what, frames, ref_n);
    free(pcm);
}
static void expect_refused(const char *what, const uint8_t *data, size_t n) {
    maktabasauti_result_t r = MAKTABASAUTI_OK; maktabasauti_decoder_t *d = open_mem(data, n, NULL, &r);
    if (d) { CHECK(0, "%s: must be refused", what); maktabasauti_decoder_close(d); } else CHECK(r != MAKTABASAUTI_OK, "%s", what);
}

int main(void) {
    size_t n, ref_n; uint8_t *adts = slurp("tests/data/aac_tone_st.aac", &n); static Au aus[1024]; size_t nau; float *ref;
    if (!adts) { printf("cannot read tests/data/aac_tone_st.aac\n"); return 1; }
    nau = split_adts(adts, n, aus, 1024);
    CHECK(nau > 20u, "access units: %zu", nau);
    ref = decode_all(adts, n, &ref_n, NULL);
    CHECK(ref && ref_n >= nau * 1024u - 2048u, "reference decode %zu frames", ref_n);
    if (!ref) return 1;
    {   /* Matroska variants */
        static const MkvOpt opts[] = {
            { LACE_NONE, 0, 1, 0, 0, 0 }, { LACE_NONE, 1, 1, 0, 0, 0 }, { LACE_NONE, 0, 1, 1, 1, 0 }, { LACE_NONE, 0, 0, 0, 0, 0 },
            { LACE_XIPH, 0, 1, 0, 0, 0 }, { LACE_XIPH, 1, 1, 1, 0, 0 }, { LACE_EBML, 0, 1, 0, 0, 0 }, { LACE_EBML, 1, 0, 1, 1, 0 },
        };
        unsigned k;
        for (k = 0; k < sizeof opts / sizeof opts[0]; k++) {
            Buf b = build_mkv(aus, nau, &opts[k]); char name[64];
            snprintf(name, sizeof name, "matroska variant %u", k);
            expect_equal(name, b.p, b.n, ref, ref_n, "matroska");
            free(b.p);
        }
        {   /* fixed-size lacing carries equal sizes: the stream differs from the reference, so only the decode count is checked */
            MkvOpt o = { LACE_FIXED, 0, 1, 0, 0, 0 }; Buf b = build_mkv(aus, nau, &o); size_t frames; float *p = decode_all(b.p, b.n, &frames, NULL);
            CHECK(p != NULL && frames >= 2u * (nau - 2u) * 1024u - 4096u, "fixed lacing: every laced frame is decoded (%zu frames)", frames); free(p); free(b.p);
        }
        {   /* the Tracks element behind the clusters: blocks cannot be assigned, the file has no usable track for the blocks read before it -> refused or empty */
            MkvOpt o = { LACE_NONE, 0, 1, 0, 0, 1 }; Buf b = build_mkv(aus, nau, &o); maktabasauti_result_t r = MAKTABASAUTI_OK; maktabasauti_decoder_t *d = open_mem(b.p, b.n, NULL, &r);
            CHECK(d == NULL, "Tracks after the clusters is refused"); if (d) maktabasauti_decoder_close(d); free(b.p);
        }
        {   /* seeking to a frame agrees with sequential decoding; a truncated file is refused or ends early without a crash */
            MkvOpt o = { LACE_XIPH, 0, 1, 0, 0, 0 }; Buf b = build_mkv(aus, nau, &o); maktabasauti_stream_info_t info; maktabasauti_decoder_t *d = open_mem(b.p, b.n, &info, NULL); float a[2 * 500]; size_t got;
            CHECK(d != NULL && (info.flags & MAKTABASAUTI_STREAM_SEEKABLE), "matroska is seekable (the list is in memory)");
            if (d) {
                CHECK(maktabasauti_decoder_seek(d, 7000, MAKTABASAUTI_SEEK_EXACT, NULL) == MAKTABASAUTI_OK, "seek");
                CHECK(maktabasauti_decoder_read(d, a, 500, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 500u && !memcmp(a, ref + 7000u * 2u, sizeof a), "seek to 7000 differs from sequential decoding");
                maktabasauti_decoder_close(d);
            }
            { size_t cut; for (cut = b.n - 1u; cut > b.n / 2u; cut -= b.n / 7u) { maktabasauti_decoder_t *t = open_mem(b.p, cut, NULL, NULL); if (t) maktabasauti_decoder_close(t); } }
            free(b.p);
        }
    }
    {   /* TS variants */
        static const TsOpt opts[] = { { 188, 0, 0x0F }, { 188, 1, 0x0F }, { 192, 0, 0x0F }, { 204, 1, 0x0F } };
        unsigned k;
        for (k = 0; k < sizeof opts / sizeof opts[0]; k++) {
            Buf b = build_ts(adts, n, &opts[k]); char name[64];
            snprintf(name, sizeof name, "mpegts variant %u (%u-byte packets)", k, opts[k].psz);
            expect_equal(name, b.p, b.n, ref, ref_n, "mpegts");
            if (k == 0u) {
                maktabasauti_stream_info_t info; maktabasauti_decoder_t *d = open_mem(b.p, b.n, &info, NULL); float a[2 * 500]; size_t got;
                if (d) {
                    CHECK(maktabasauti_decoder_seek(d, 9000, MAKTABASAUTI_SEEK_EXACT, NULL) == MAKTABASAUTI_OK, "ts seek");
                    CHECK(maktabasauti_decoder_read(d, a, 500, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 500u && !memcmp(a, ref + 9000u * 2u, sizeof a), "ts seek to 9000 differs");
                    maktabasauti_decoder_close(d);
                } else CHECK(0, "ts open for seek");
                { size_t cut; for (cut = b.n - 1u; cut > b.n / 2u; cut -= b.n / 9u) { maktabasauti_decoder_t *t = open_mem(b.p, cut - cut % 188u + 5u, NULL, NULL); if (t) maktabasauti_decoder_close(t); } }
            }
            free(b.p);
        }
        {   /* LATM / LOAS (stream_type 0x11): the LOAS fixture of the repository in a TS must decode like the LOAS file itself */
            size_t ln, lframes, tframes; uint8_t *loas = slurp("tests/data/eld_st.loas", &ln); float *lr, *tr; TsOpt o = { 188, 1, 0x11 }; Buf b;
            CHECK(loas != NULL, "tests/data/eld_st.loas");
            if (loas) {
                maktabasauti_stream_info_t li; maktabasauti_decoder_t *probe = open_mem(loas, ln, &li, NULL); unsigned rate = 0, ch = 0; if (probe) { rate = li.sample_rate; ch = li.channels; maktabasauti_decoder_close(probe); }
                CHECK(ch == 2u, "the LOAS fixture is stereo (%u)", ch); (void)rate;
                lr = decode_loas(loas, ln, &lframes); b = build_ts(loas, ln, &o); tr = decode_loas(b.p, b.n, &tframes);
                CHECK(lr && tr && lframes == tframes && lframes > 0u && !memcmp(lr, tr, lframes * 2u * sizeof(float)), "LOAS in TS differs from the LOAS file (%zu / %zu frames)", tframes, lframes);
                free(lr); free(tr); free(b.p); free(loas);
            }
        }
        {   TsOpt o = { 188, 0, 0x03 }; Buf b = build_ts(adts, n, &o); expect_refused("a program without AAC", b.p, b.n); free(b.p); }
    }
    {   /* garbage that looks like neither */
        uint8_t junk[3000]; size_t i; for (i = 0; i < sizeof junk; i++) junk[i] = (uint8_t)(i * 7u + 3u);
        expect_refused("noise", junk, sizeof junk);
        junk[0] = 0x1A; junk[1] = 0x45; junk[2] = 0xDF; junk[3] = 0xA3; memcpy(junk + 8, "matroska", 8);
        expect_refused("EBML header without tracks", junk, sizeof junk);
    }
    free(ref); free(adts);
    printf(g_failures ? "test_aac_containers: %d FAILURES\n" : "test_aac_containers: all passed\n", g_failures);
    return g_failures != 0;
}
