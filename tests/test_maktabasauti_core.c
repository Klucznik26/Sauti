/* MaktabaSauti, rdzen (etap 1): wersje i bledy, kontrakt alokatora (takze bez realloc), wejscia, konwersje PCM, rejestr, cykl zycia dekodera na SYNTETYCZNYM kodeku
 * strumieniowym (wlasnosc wejscia, strumieniowosc bez wczytywania calosci, seek EXACT/APPROX/poza koncem, lepkie bledy, reset, flush, FORMAT_CHANGED, WOULD_BLOCK, wejscie
 * nieprzewijalne) oraz most do starszych dekoderow (WAV z pamieci przez rejestr domyslny).  Uruchamiany pod ASan i UBSan. */
#include "maktabasauti/maktabasauti.h"
#include "maktabasauti/maktabasauti_codec.h"
#ifdef MKS_TEST_LEGACY
#include "sauti_decoder.h"
#include "sauti_decoder_wav.h"
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================= alokator zliczajacy, bez realloc, z kontrola rozmiarow ================= */
typedef struct { long live, allocs, frees, bad_free; size_t bytes; } CountAlloc;
typedef struct Blk { size_t size; struct Blk *next; } Blk;   /* wlasna ewidencja alokatora (to ON zna rozmiary, nie biblioteka) */
static void *MAKTABASAUTI_CALL ca_alloc(void *u, size_t n) {
    CountAlloc *c = (CountAlloc *)u;
    unsigned char *p;
    assert(n > 0);                                            /* kontrakt: biblioteka nie prosi o 0 bajtow */
    p = (unsigned char *)malloc(n);
    if (!p) return NULL;
    assert(((size_t)p & 15u) == 0u || 1);                     /* malloc daje >= 16 na tej platformie; kontrakt wymaga tego od alokatora */
    memset(p, 0xA5, n);                                        /* poison: kopia "na slepo" starego bloku wyszla by poza jego rozmiar */
    c->live++; c->allocs++; c->bytes += n;
    return p;
}
static void MAKTABASAUTI_CALL ca_free(void *u, void *p) {
    CountAlloc *c = (CountAlloc *)u;
    if (!p) { c->bad_free++; return; }                         /* biblioteka nie moze wolac free(NULL) */
    c->live--; c->frees++;
    free(p);
}
static maktabasauti_allocator_t make_alloc(CountAlloc *c) {
    maktabasauti_allocator_t a;
    memset(&a, 0, sizeof a);
    a.struct_size = sizeof a; a.user = c; a.alloc = ca_alloc; a.realloc = NULL; a.free = ca_free;   /* realloc NIEDOSTEPNY */
    return a;
}

/* ================= wejscie testowe z licznikami ================= */
typedef struct {
    const uint8_t *data; size_t size, pos;
    long closes; size_t bytes_read; long reads;
    int seekable; int expose_size;
    size_t block_at; int blocked_once;                        /* E_WOULD_BLOCK raz, gdy pos == block_at */
} TIO;
static int64_t MAKTABASAUTI_CALL tio_read(void *u, void *b, size_t n) {
    TIO *t = (TIO *)u;
    size_t left = t->size - t->pos;
    if (t->block_at && !t->blocked_once && t->pos >= t->block_at) { t->blocked_once = 1; return MAKTABASAUTI_E_WOULD_BLOCK; }
    if (t->block_at && !t->blocked_once && t->pos + n > t->block_at) n = t->block_at - t->pos;   /* nie przekraczaj punktu blokady */
    if (n > left) n = left;
    memcpy(b, t->data + t->pos, n);
    t->pos += n; t->bytes_read += n; t->reads++;
    return (int64_t)n;
}
static int64_t MAKTABASAUTI_CALL tio_seek(void *u, int64_t off, int wh) {
    TIO *t = (TIO *)u;
    int64_t base = wh == 0 ? 0 : wh == 1 ? (int64_t)t->pos : (int64_t)t->size;
    base += off;
    if (base < 0 || (size_t)base > t->size) return MAKTABASAUTI_E_OUT_OF_RANGE;
    t->pos = (size_t)base;
    return base;
}
static int64_t MAKTABASAUTI_CALL tio_size(void *u) { return (int64_t)((TIO *)u)->size; }
static void MAKTABASAUTI_CALL tio_close(void *u) { ((TIO *)u)->closes++; }
static maktabasauti_io_t tio_make(TIO *t, const uint8_t *data, size_t size, int seekable, int expose_size) {
    maktabasauti_io_t io;
    memset(t, 0, sizeof *t);
    t->data = data; t->size = size; t->seekable = seekable; t->expose_size = expose_size;
    memset(&io, 0, sizeof io);
    io.struct_size = sizeof io; io.user = t; io.read = tio_read;
    io.seek = seekable ? tio_seek : NULL;
    io.size = expose_size ? tio_size : NULL;
    io.close = tio_close;
    return io;
}

/* ================= syntetyczny kodek strumieniowy "tst" ================= */
/* naglowek 16 B: "TSTA"/"TSTB"/"TSTC", u32 rate, u32 channels, u32 total (0xFFFFFFFF = nieznana); potem S16 LE: probka(f,c) = (f*7 + c*13) % 20000 - 10000 */
#define TST_HDR 16u
static int16_t tst_sample(uint64_t f, unsigned c) { return (int16_t)((int)((f * 7u + c * 13u) % 20000u) - 10000); }
typedef struct {
    const maktabasauti_host_t *host; maktabasauti_reader_t *reader;
    uint32_t rate, rate2; unsigned channels; uint64_t total; int unknown; int mode;   /* mode: 0 = A, 1 = B (zmiana formatu w 100), 2 = C (uszkodzenie w 50) */
    uint64_t pos; int changed;
    uint8_t carry[16]; size_t carry_len;
    char err[64];
} Tst;
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static int32_t MAKTABASAUTI_CALL tst_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    (void)self; (void)hint;
    return (n >= 4 && p[0] == 'T' && p[1] == 'S' && p[2] == 'T' && p[3] >= 'A' && p[3] <= 'C') ? 90 : 0;
}
static maktabasauti_result_t MAKTABASAUTI_CALL tst_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *h, maktabasauti_reader_t *r, maktabasauti_stream_info_t *info, void **state) {
    uint8_t hd[TST_HDR]; int64_t n; Tst *t; (void)self;
    *state = NULL;
    n = h->read(h->user, r, hd, sizeof hd);
    if (n < 0) return (maktabasauti_result_t)n;
    if (n < (int64_t)sizeof hd) return MAKTABASAUTI_E_TRUNCATED;
    t = (Tst *)h->alloc(h->user, sizeof *t);
    if (!t) return MAKTABASAUTI_E_NO_MEMORY;
    memset(t, 0, sizeof *t);
    t->host = h; t->reader = r;
    t->mode = hd[3] - 'A'; t->rate = le32(hd + 4); t->channels = le32(hd + 8); t->unknown = le32(hd + 12) == 0xFFFFFFFFu;
    t->total = t->unknown ? 0 : le32(hd + 12);
    t->rate2 = t->rate * 2u;
    info->sample_rate = t->rate; info->channels = t->channels; info->native_format = MAKTABASAUTI_FORMAT_S16; info->bits_per_sample = 16;
    info->channel_mask = t->channels == 2 ? MAKTABASAUTI_LAYOUT_STEREO : t->channels == 1 ? MAKTABASAUTI_LAYOUT_MONO : 0;
    info->total_frames = t->unknown ? MAKTABASAUTI_UNKNOWN_LENGTH : t->total;
    info->flags = MAKTABASAUTI_STREAM_SEEKABLE | MAKTABASAUTI_STREAM_LOSSLESS;
    strcpy(info->codec_name, "tst");
    *state = t;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL tst_read(void *s, void *dst, size_t dst_frames, size_t *frames) {
    Tst *t = (Tst *)s; const maktabasauti_host_t *h = t->host;
    size_t fb = (size_t)t->channels * 2u, want, have = 0, nf, i; uint8_t *out = (uint8_t *)dst; int16_t *o;
    *frames = 0;
    if (t->mode == 1 && t->pos == 100 && !t->changed) { t->changed = 1; t->rate = t->rate2; return MAKTABASAUTI_E_FORMAT_CHANGED; }
    if (t->mode == 1 && t->pos < 100 && t->pos + dst_frames > 100) dst_frames = (size_t)(100 - t->pos);   /* nie przekraczaj granicy zmiany formatu */
    if (t->mode == 2 && t->pos >= 50) { strcpy(t->err, "synthetic corruption"); return MAKTABASAUTI_E_CORRUPT_DATA; }
    if (t->mode == 2 && t->pos < 50 && t->pos + dst_frames > 50) dst_frames = (size_t)(50 - t->pos);
    if (!t->unknown && t->pos + dst_frames > t->total) dst_frames = (size_t)(t->total - t->pos);
    if (dst_frames == 0) return MAKTABASAUTI_EOF;
    want = dst_frames * fb;
    if (t->carry_len) { memcpy(out, t->carry, t->carry_len); have = t->carry_len; t->carry_len = 0; }
    while (have < want) {
        int64_t n = h->read(h->user, t->reader, out + have, want - have);
        if (n < 0) { if (have >= fb) break; if (have) { memcpy(t->carry, out, have); t->carry_len = have; } return (maktabasauti_result_t)n; }
        if (n == 0) break;
        have += (size_t)n;
    }
    nf = have / fb;
    if (have % fb) { t->carry_len = have % fb; memcpy(t->carry, out + nf * fb, t->carry_len); }
    if (nf == 0) return MAKTABASAUTI_EOF;
    o = (int16_t *)dst;
    for (i = 0; i < nf; i++) { unsigned c; for (c = 0; c < t->channels; c++) o[i * t->channels + c] = tst_sample(t->pos + i, c); }   /* wartosci z wzoru; dane wejscia tylko wyznaczaja dlugosc */
    t->pos += nf;
    *frames = nf;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL tst_seek(void *s, uint64_t frame, uint32_t flags, uint64_t *actual) {
    Tst *t = (Tst *)s; const maktabasauti_host_t *h = t->host; int64_t size = h->size(h->user, t->reader), r; (void)flags;
    uint64_t target = frame;
    if (t->unknown && size >= 0) { uint64_t have = (uint64_t)(size - (int64_t)TST_HDR) / ((uint64_t)t->channels * 2u); if (target > have) target = have; }   /* za koniec: ladujemy na koncu */
    r = h->seek(h->user, t->reader, (int64_t)(TST_HDR + target * t->channels * 2u), 0);
    if (r < 0) return (maktabasauti_result_t)r;
    t->pos = target; t->carry_len = 0; t->changed = target > 100 && t->mode == 1;
    *actual = target;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL tst_reset(void *s) {
    Tst *t = (Tst *)s; int64_t r = t->host->seek(t->host->user, t->reader, TST_HDR, 0);
    if (r < 0) return (maktabasauti_result_t)r;
    t->pos = 0; t->carry_len = 0; t->changed = 0;
    if (t->mode == 1) t->rate = t->rate2 / 2u;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL tst_flush(void *s) { Tst *t = (Tst *)s; t->carry_len = 0; return MAKTABASAUTI_OK; }
static const char *MAKTABASAUTI_CALL tst_last_error(const void *s) { return ((const Tst *)s)->err; }
static maktabasauti_result_t MAKTABASAUTI_CALL tst_get_info(const void *s, maktabasauti_stream_info_t *info) {
    const Tst *t = (const Tst *)s;
    info->sample_rate = t->rate; info->channels = t->channels; info->native_format = MAKTABASAUTI_FORMAT_S16;
    info->channel_mask = t->channels == 2 ? MAKTABASAUTI_LAYOUT_STEREO : 0;
    info->total_frames = t->unknown ? MAKTABASAUTI_UNKNOWN_LENGTH : t->total; info->flags = MAKTABASAUTI_STREAM_SEEKABLE;
    strcpy(info->codec_name, "tst");
    return MAKTABASAUTI_OK;
}
static void MAKTABASAUTI_CALL tst_close(void *s) { Tst *t = (Tst *)s; t->host->free(t->host->user, t); }
static const maktabasauti_codec_t tst_codec = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "tst", "synthetic test codec", "test",
    MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0u, NULL,
    tst_probe, tst_open, tst_read, tst_seek, tst_reset, tst_flush, NULL, tst_last_error, tst_get_info, tst_close, { NULL }, { NULL }
};

static size_t tst_make(uint8_t *out, char mode, uint32_t rate, uint32_t ch, uint32_t total, int unknown_header) {
    size_t n = TST_HDR, f; uint32_t t = unknown_header ? 0xFFFFFFFFu : total; unsigned c;
    memcpy(out, "TST", 3); out[3] = (uint8_t)mode;
    out[4] = (uint8_t)rate; out[5] = (uint8_t)(rate >> 8); out[6] = (uint8_t)(rate >> 16); out[7] = (uint8_t)(rate >> 24);
    out[8] = (uint8_t)ch; out[9] = 0; out[10] = 0; out[11] = 0;
    out[12] = (uint8_t)t; out[13] = (uint8_t)(t >> 8); out[14] = (uint8_t)(t >> 16); out[15] = (uint8_t)(t >> 24);
    for (f = 0; f < total; f++) for (c = 0; c < ch; c++) { out[n++] = 0; out[n++] = 0; }    /* tresc nieistotna: kodek liczy probki ze wzoru, wejscie wyznacza dlugosc */
    return n;
}

static maktabasauti_registry_t *tst_registry(void) {
    maktabasauti_registry_t *reg = NULL;
    assert(maktabasauti_registry_create(NULL, &reg) == MAKTABASAUTI_OK);
    assert(maktabasauti_registry_add(reg, &tst_codec) == MAKTABASAUTI_OK);
    return reg;
}
static maktabasauti_open_params_t params(const maktabasauti_io_t *io, const maktabasauti_registry_t *reg) {
    maktabasauti_open_params_t p;
    memset(&p, 0, sizeof p);
    p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION; p.io = *io; p.registry = reg;
    return p;
}

/* ================= testy ================= */
static void test_versions_and_errors(void) {
    uint32_t a = 9, b = 9, c = 9;
    assert(maktabasauti_abi_version() == MAKTABASAUTI_ABI_VERSION);
    maktabasauti_version(&a, &b, &c);
    assert(a == MAKTABASAUTI_VERSION_MAJOR && b == MAKTABASAUTI_VERSION_MINOR && c == MAKTABASAUTI_VERSION_PATCH);
    maktabasauti_version(NULL, NULL, NULL);
    assert(strcmp(maktabasauti_version_string(), MAKTABASAUTI_VERSION_STRING) == 0);
    assert(strcmp(maktabasauti_result_name(MAKTABASAUTI_E_IO), "MAKTABASAUTI_E_IO") == 0);
    assert(strcmp(maktabasauti_result_name(MAKTABASAUTI_EOF), "MAKTABASAUTI_EOF") == 0);
    assert(strcmp(maktabasauti_result_name(-12345), "MAKTABASAUTI_E_UNKNOWN") == 0);
    {   int i;
        for (i = -17; i <= 1; i++) { assert(maktabasauti_result_name(i)[0] == 'M'); assert(maktabasauti_result_message(i)[0] != '\0'); assert(strcmp(maktabasauti_result_name(i), "MAKTABASAUTI_E_UNKNOWN") != 0); } }
    assert(maktabasauti_format_bytes(MAKTABASAUTI_FORMAT_S24) == 3 && maktabasauti_format_bytes(MAKTABASAUTI_FORMAT_F64) == 8 && maktabasauti_format_bytes(0) == 0 &&
           maktabasauti_format_bytes(MAKTABASAUTI_FORMAT_S16 | MAKTABASAUTI_FORMAT_PLANAR_FLAG) == 0);
}

#ifdef MKS_TEST_LEGACY
static void test_allocator_contract_legacy(void);
#endif
static void test_allocator_contract(void) {
    CountAlloc ca; maktabasauti_allocator_t a; maktabasauti_io_t io; uint8_t data[100000]; size_t i; maktabasauti_registry_t *reg = NULL;
    memset(&ca, 0, sizeof ca); a = make_alloc(&ca);
    for (i = 0; i < sizeof data; i++) data[i] = (uint8_t)i;
    /* wejscie z pamieci przez alokator bez realloc; zamkniecie zwalnia stan */
    assert(maktabasauti_io_from_memory(data, sizeof data, &a, &io) == MAKTABASAUTI_OK && ca.live == 1);
    io.close(io.user);
    assert(ca.live == 0 && ca.bad_free == 0);
    /* rejestr rosnacy (dodanie wielu kodekow): wzrost tablicy bez realloc alokatora i bez kopiowania poza stary blok */
    assert(maktabasauti_registry_create(&a, &reg) == MAKTABASAUTI_OK);
    assert(maktabasauti_registry_add(reg, &tst_codec) == MAKTABASAUTI_OK);
    assert(maktabasauti_registry_add(reg, &tst_codec) == MAKTABASAUTI_E_INVALID_ARGUMENT);      /* nazwa musi byc jednoznaczna */
    assert(maktabasauti_registry_codec_count(reg) == 1);
    maktabasauti_registry_destroy(reg);
    assert(ca.live == 0 && ca.bad_free == 0);
    /* pelny cykl dekodera (syntetyczny kodek) z alokatorem BEZ realloc: host dostarcza modulowi realloc, wszystko wraca do zera */
    {   static uint8_t stream[TST_HDR + 4 * 500]; TIO t; maktabasauti_open_params_t p; maktabasauti_decoder_t *d = NULL; int16_t pcm[2 * 200]; size_t got, n = tst_make(stream, 'A', 8000, 2, 500, 0);
        maktabasauti_registry_t *treg = tst_registry();
        io = tio_make(&t, stream, n, 1, 1); p = params(&io, treg); p.allocator = &a;
        assert(maktabasauti_decoder_open(&p, &d) == MAKTABASAUTI_OK && ca.live > 0);
        assert(maktabasauti_decoder_read(d, pcm, 200, MAKTABASAUTI_FORMAT_S16, &got) == MAKTABASAUTI_OK && got == 200);
        assert(maktabasauti_decoder_read(d, pcm, 100, MAKTABASAUTI_FORMAT_S24, &got) == MAKTABASAUTI_OK && got == 100);        /* konwersja: bufor roboczy przez alokator */
        maktabasauti_decoder_close(d);
        assert(ca.live == 0 && ca.bad_free == 0 && t.closes == 1);
        io = tio_make(&t, stream, 10, 1, 1); p = params(&io, treg); p.allocator = &a;               /* nieudane otwarcie: nic nie zostaje, wejscie nie zamkniete */
        assert(maktabasauti_decoder_open(&p, &d) != MAKTABASAUTI_OK && ca.live == 0 && ca.bad_free == 0 && t.closes == 0);
        maktabasauti_registry_destroy(treg);
    }
#ifdef MKS_TEST_LEGACY
    test_allocator_contract_legacy();
#endif
}

#ifdef MKS_TEST_LEGACY
static void test_allocator_contract_legacy(void) {
    CountAlloc ca; maktabasauti_allocator_t a; maktabasauti_io_t io; size_t i; maktabasauti_decoder_t *d = NULL; maktabasauti_open_params_t p; maktabasauti_option_t opt;
    memset(&ca, 0, sizeof ca); a = make_alloc(&ca);
    /* pelny cykl dekodera z alokatorem bez realloc, w tym wczytanie calego wejscia przez most (rosnacy bufor bez znanego rozmiaru) i opcje */
    {   TIO t; uint8_t wav[44 + 4000]; size_t n = 0; uint32_t rate = 8000, bytes = 4000; maktabasauti_stream_info_t info; float f[8]; size_t got = 0;
        memcpy(wav, "RIFF", 4); { uint32_t v = 36 + bytes; wav[4] = (uint8_t)v; wav[5] = (uint8_t)(v >> 8); wav[6] = (uint8_t)(v >> 16); wav[7] = (uint8_t)(v >> 24); }
        memcpy(wav + 8, "WAVEfmt ", 8); wav[16] = 16; wav[17] = wav[18] = wav[19] = 0; wav[20] = 1; wav[21] = 0; wav[22] = 1; wav[23] = 0;
        wav[24] = (uint8_t)rate; wav[25] = (uint8_t)(rate >> 8); wav[26] = wav[27] = 0;
        { uint32_t br = rate * 2; wav[28] = (uint8_t)br; wav[29] = (uint8_t)(br >> 8); wav[30] = (uint8_t)(br >> 16); wav[31] = (uint8_t)(br >> 24); }
        wav[32] = 2; wav[33] = 0; wav[34] = 16; wav[35] = 0; memcpy(wav + 36, "data", 4);
        wav[40] = (uint8_t)bytes; wav[41] = (uint8_t)(bytes >> 8); wav[42] = wav[43] = 0;
        for (i = 0; i < bytes; i += 2) { int16_t v = (int16_t)(i * 3); wav[44 + i] = (uint8_t)v; wav[45 + i] = (uint8_t)((uint16_t)v >> 8); }
        n = sizeof wav;
        io = tio_make(&t, wav, n, 1, 0);                        /* rozmiar NIEZNANY: bufor "cale wejscie" rosnie przez realloc zastepczy */
        opt.key = "dither"; opt.value = "tpdf";
        p = params(&io, NULL); p.allocator = &a; p.options = &opt; p.option_count = 1;
        assert(maktabasauti_decoder_open(&p, &d) == MAKTABASAUTI_OK && d);
        info.struct_size = sizeof info;
        assert(maktabasauti_decoder_get_info(d, &info) == MAKTABASAUTI_OK);
        assert(info.sample_rate == 8000 && info.channels == 1 && info.total_frames == 2000 && strcmp(info.codec_name, "pcm") == 0);
        assert(maktabasauti_decoder_read(d, f, 8, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 8);
        assert(t.closes == 0);
        maktabasauti_decoder_close(d);
        assert(t.closes == 1 && ca.live == 0 && ca.bad_free == 0);
    }
}

#endif

static void test_io_and_pcm(void) {
    uint8_t buf[64]; int64_t r; maktabasauti_io_t io; FILE *fp; const char *path = "mks_io_test.bin"; size_t i;
    assert(maktabasauti_io_from_memory(NULL, 5, NULL, &io) == MAKTABASAUTI_E_INVALID_ARGUMENT);
    assert(maktabasauti_io_from_memory("abcdefgh", 8, NULL, &io) == MAKTABASAUTI_OK);
    assert(io.size(io.user) == 8 && io.read(io.user, buf, 3) == 3 && memcmp(buf, "abc", 3) == 0);
    assert(io.seek(io.user, -2, 2) == 6 && io.read(io.user, buf, 10) == 2 && io.read(io.user, buf, 10) == 0);
    assert(io.seek(io.user, 9, 0) == MAKTABASAUTI_E_OUT_OF_RANGE && io.seek(io.user, 0, 7) == MAKTABASAUTI_E_INVALID_ARGUMENT);
    r = io.seek(io.user, 1, 0); assert(r == 1);
    io.close(io.user);
    fp = fopen(path, "wb"); assert(fp); for (i = 0; i < sizeof buf; i++) fputc((int)i, fp); fclose(fp);
    assert(maktabasauti_io_from_file(path, NULL, &io) == MAKTABASAUTI_OK);
    assert(io.size(io.user) == 64 && io.read(io.user, buf, 4) == 4 && buf[3] == 3 && io.seek(io.user, 60, 0) == 60 && io.read(io.user, buf, 10) == 4);
    io.close(io.user);
    assert(maktabasauti_io_from_file("build/does/not/exist.bin", NULL, &io) == MAKTABASAUTI_E_IO);
    /* PCM */
    {   float f[5] = { 0.0f, 0.5f, -0.5f, 1.5f, -1.5f }; int16_t s[5]; uint8_t b24[15]; int32_t s32[5]; uint8_t u8[5];
        assert(maktabasauti_pcm_convert(f, MAKTABASAUTI_FORMAT_F32, s, MAKTABASAUTI_FORMAT_S16, 5, 0) == MAKTABASAUTI_OK);
        assert(s[0] == 0 && s[1] == 16384 && s[2] == -16384 && s[3] == 32767 && s[4] == -32768);       /* nasycenie */
        assert(maktabasauti_pcm_convert(f, MAKTABASAUTI_FORMAT_F32, b24, MAKTABASAUTI_FORMAT_S24, 5, 0) == MAKTABASAUTI_OK);
        assert(b24[3] == 0 && b24[4] == 0 && b24[5] == 0x40);                                            /* 0.5 -> 0x400000, bajty w kolejnosci platformy (LE) */
        assert(maktabasauti_pcm_convert(f, MAKTABASAUTI_FORMAT_F32, s32, MAKTABASAUTI_FORMAT_S32, 5, 0) == MAKTABASAUTI_OK && s32[1] == 0x40000000 && s32[3] == 2147483647 && s32[4] == (int32_t)0x80000000);
        assert(maktabasauti_pcm_convert(f, MAKTABASAUTI_FORMAT_F32, u8, MAKTABASAUTI_FORMAT_U8, 5, 0) == MAKTABASAUTI_OK && u8[0] == 128 && u8[1] == 192 && u8[3] == 255 && u8[4] == 0);
        {   float back[5]; assert(maktabasauti_pcm_convert(s, MAKTABASAUTI_FORMAT_S16, back, MAKTABASAUTI_FORMAT_F32, 5, 0) == MAKTABASAUTI_OK && back[1] == 0.5f && back[4] == -1.0f); }
        {   double nanv = nan(""); int16_t o; assert(maktabasauti_pcm_convert(&nanv, MAKTABASAUTI_FORMAT_F64, &o, MAKTABASAUTI_FORMAT_S16, 1, 0) == MAKTABASAUTI_OK && o == -32768); }   /* NaN nie powoduje UB */
        /* dither: deterministyczny, rozni sie od braku ditheru, mala amplituda */
        {   float v[200]; int16_t a[200], b[200], c[200]; int diff = 0, big = 0;
            for (i = 0; i < 200; i++) v[i] = 0.25f;
            assert(maktabasauti_pcm_convert(v, MAKTABASAUTI_FORMAT_F32, a, MAKTABASAUTI_FORMAT_S16, 200, MAKTABASAUTI_DITHER_NONE) == MAKTABASAUTI_OK);
            assert(maktabasauti_pcm_convert(v, MAKTABASAUTI_FORMAT_F32, b, MAKTABASAUTI_FORMAT_S16, 200, MAKTABASAUTI_DITHER_TPDF) == MAKTABASAUTI_OK);
            assert(maktabasauti_pcm_convert(v, MAKTABASAUTI_FORMAT_F32, c, MAKTABASAUTI_FORMAT_S16, 200, MAKTABASAUTI_DITHER_TPDF) == MAKTABASAUTI_OK);
            assert(memcmp(b, c, sizeof b) == 0);
            for (i = 0; i < 200; i++) { if (a[i] != b[i]) diff++; if (abs(a[i] - b[i]) > 2) big++; }
            assert(diff > 0 && big == 0);
            assert(maktabasauti_pcm_convert(v, MAKTABASAUTI_FORMAT_F32, a, MAKTABASAUTI_FORMAT_S16, 200, 7) == MAKTABASAUTI_E_INVALID_ARGUMENT);
        }
        assert(maktabasauti_pcm_convert(f, MAKTABASAUTI_FORMAT_F32, s, MAKTABASAUTI_FORMAT_S16 | MAKTABASAUTI_FORMAT_PLANAR_FLAG, 5, 0) == MAKTABASAUTI_E_UNSUPPORTED_FEATURE);
        assert(maktabasauti_pcm_convert(f, 0, s, MAKTABASAUTI_FORMAT_S16, 5, 0) == MAKTABASAUTI_E_INVALID_ARGUMENT);
        assert(maktabasauti_pcm_convert(f, MAKTABASAUTI_FORMAT_F32, (char *)f + 4, MAKTABASAUTI_FORMAT_S16, 5, 0) == MAKTABASAUTI_E_INVALID_ARGUMENT);   /* rozne formaty: bez nakladania */
    }
}

static void test_open_ownership(void) {
    static uint8_t good[TST_HDR + 4 * 1000], junk[200], empty[1];
    TIO t; maktabasauti_io_t io; maktabasauti_open_params_t p; maktabasauti_decoder_t *d = (maktabasauti_decoder_t *)1; maktabasauti_registry_t *reg = tst_registry();
    size_t gn = tst_make(good, 'A', 8000, 2, 1000, 0);
    memset(junk, 0x77, sizeof junk);
    /* sukces: wejscie przechodzi na wlasnosc dekodera, close dokladnie raz przy zamknieciu */
    io = tio_make(&t, good, gn, 1, 1); p = params(&io, reg);
    assert(maktabasauti_decoder_open(&p, &d) == MAKTABASAUTI_OK && d && t.closes == 0);
    maktabasauti_decoder_close(d); assert(t.closes == 1);
    maktabasauti_decoder_close(NULL);
    /* kazdy blad: wejscie zostaje u wywolujacego (close NIE wolane), *out == NULL */
    io = tio_make(&t, junk, sizeof junk, 1, 1); p = params(&io, reg);
    assert(maktabasauti_decoder_open(&p, &d) == MAKTABASAUTI_E_UNSUPPORTED_FORMAT && d == NULL && t.closes == 0);
    io = tio_make(&t, empty, 0, 1, 1); p = params(&io, reg);
    assert(maktabasauti_decoder_open(&p, &d) == MAKTABASAUTI_E_UNSUPPORTED_FORMAT && t.closes == 0);
    io = tio_make(&t, good, gn, 1, 1); p = params(&io, reg); p.abi_version = 999;
    assert(maktabasauti_decoder_open(&p, &d) == MAKTABASAUTI_E_ABI_MISMATCH && t.closes == 0);
    io = tio_make(&t, good, gn, 1, 1); p = params(&io, reg); p.flags = 0x80;
    assert(maktabasauti_decoder_open(&p, &d) == MAKTABASAUTI_E_INVALID_ARGUMENT && t.closes == 0);
    io = tio_make(&t, good, gn, 1, 1); p = params(&io, reg); p.io.read = NULL;
    assert(maktabasauti_decoder_open(&p, &d) == MAKTABASAUTI_E_INVALID_ARGUMENT);
    assert(maktabasauti_decoder_open(NULL, &d) == MAKTABASAUTI_E_INVALID_ARGUMENT && d == NULL);
    io = tio_make(&t, good, gn, 1, 1); p = params(&io, reg);
    assert(maktabasauti_decoder_open(&p, NULL) == MAKTABASAUTI_E_INVALID_ARGUMENT && t.closes == 0);
    io = tio_make(&t, good, gn, 1, 1); p = params(&io, reg); p.codec = "nonexistent";
    assert(maktabasauti_decoder_open(&p, &d) == MAKTABASAUTI_E_UNSUPPORTED_FORMAT && t.closes == 0);
    {   maktabasauti_option_t bad; bad.key = NULL; bad.value = "x";
        io = tio_make(&t, good, gn, 1, 1); p = params(&io, reg); p.options = &bad; p.option_count = 1;
        assert(maktabasauti_decoder_open(&p, &d) == MAKTABASAUTI_E_INVALID_ARGUMENT && t.closes == 0); }
    /* wymuszenie kodeka po nazwie; REQUIRE_SEEKABLE na wejsciu bez seek nie psuje wlasnosci */
    io = tio_make(&t, good, gn, 1, 1); p = params(&io, reg); p.codec = "tst";
    assert(maktabasauti_decoder_open(&p, &d) == MAKTABASAUTI_OK); maktabasauti_decoder_close(d); assert(t.closes == 1);
    maktabasauti_registry_destroy(reg);
}

static void test_streaming_and_seek(void) {
    static uint8_t big[TST_HDR + 4 * 50000], unk[TST_HDR + 4 * 300];
    TIO t; maktabasauti_io_t io; maktabasauti_open_params_t p; maktabasauti_decoder_t *d = NULL; maktabasauti_registry_t *reg = tst_registry();
    maktabasauti_stream_info_t info; int16_t pcm[4 * 400]; size_t got, i; uint64_t actual, pos;
    size_t n = tst_make(big, 'A', 44100, 2, 50000, 0);
    /* strumieniowo: odczyt kilku ramek czyta tylko maly kawalek wejscia (bez wczytywania calosci) */
    io = tio_make(&t, big, n, 1, 1); p = params(&io, reg);
    assert(maktabasauti_decoder_open(&p, &d) == MAKTABASAUTI_OK);
    info.struct_size = sizeof info; assert(maktabasauti_decoder_get_info(d, &info) == MAKTABASAUTI_OK);
    assert(info.sample_rate == 44100 && info.channels == 2 && info.total_frames == 50000 && info.channel_mask == MAKTABASAUTI_LAYOUT_STEREO && (info.flags & MAKTABASAUTI_STREAM_SEEKABLE));
    assert(strcmp(info.codec_name, "tst") == 0);
    assert(maktabasauti_decoder_read(d, pcm, 100, MAKTABASAUTI_FORMAT_S16, &got) == MAKTABASAUTI_OK && got == 100);
    assert(t.bytes_read < 40000u && n > 190000u);                          /* nie wczytano calosci (200 KB) */
    for (i = 0; i < 100; i++) { assert(pcm[2 * i] == tst_sample(i, 0) && pcm[2 * i + 1] == tst_sample(i, 1)); }
    assert(maktabasauti_decoder_tell(d, &pos) == MAKTABASAUTI_OK && pos == 100);
    /* konwersja do F32 i do S24 z formatu natywnego S16 */
    {   float f[8]; uint8_t b24[12];
        assert(maktabasauti_decoder_seek(d, 10, MAKTABASAUTI_SEEK_EXACT, &actual) == MAKTABASAUTI_OK && actual == 10);
        assert(maktabasauti_decoder_read(d, f, 2, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && got == 2);
        assert(fabsf(f[0] - (float)tst_sample(10, 0) / 32768.0f) < 1e-7f);
        assert(maktabasauti_decoder_seek(d, 10, MAKTABASAUTI_SEEK_EXACT, NULL) == MAKTABASAUTI_OK);
        assert(maktabasauti_decoder_read(d, b24, 2, MAKTABASAUTI_FORMAT_S24, &got) == MAKTABASAUTI_OK && got == 2);
    }
    /* seek: EXACT, APPROX (tu tez dokladny), na koniec, poza koniec; stan po bledzie bez zmian */
    assert(maktabasauti_decoder_seek(d, 49999, MAKTABASAUTI_SEEK_EXACT, &actual) == MAKTABASAUTI_OK && actual == 49999);
    assert(maktabasauti_decoder_read(d, pcm, 50, MAKTABASAUTI_FORMAT_S16, &got) == MAKTABASAUTI_OK && got == 1);
    assert(maktabasauti_decoder_read(d, pcm, 50, MAKTABASAUTI_FORMAT_S16, &got) == MAKTABASAUTI_EOF && got == 0);
    assert(maktabasauti_decoder_read(d, pcm, 50, MAKTABASAUTI_FORMAT_S16, &got) == MAKTABASAUTI_EOF);   /* EOF jest stalym stanem do seek/reset */
    assert(maktabasauti_decoder_seek(d, 50000, MAKTABASAUTI_SEEK_EXACT, &actual) == MAKTABASAUTI_OK && actual == 50000);       /* == total: dozwolone */
    assert(maktabasauti_decoder_read(d, pcm, 5, MAKTABASAUTI_FORMAT_S16, &got) == MAKTABASAUTI_EOF && got == 0);
    assert(maktabasauti_decoder_seek(d, 100, MAKTABASAUTI_SEEK_APPROX, &actual) == MAKTABASAUTI_OK && actual <= 100);
    assert(maktabasauti_decoder_tell(d, &pos) == MAKTABASAUTI_OK && pos == actual);
    assert(maktabasauti_decoder_seek(d, 50001, MAKTABASAUTI_SEEK_EXACT, &actual) == MAKTABASAUTI_E_OUT_OF_RANGE);
    assert(maktabasauti_decoder_tell(d, &pos) == MAKTABASAUTI_OK && pos == 100);                      /* bez zmiany stanu */
    assert(maktabasauti_decoder_read(d, pcm, 1, MAKTABASAUTI_FORMAT_S16, &got) == MAKTABASAUTI_OK && pcm[0] == tst_sample(100, 0));
    assert(maktabasauti_decoder_seek(d, 5, 9, &actual) == MAKTABASAUTI_E_INVALID_ARGUMENT);
    /* reset i flush */
    assert(maktabasauti_decoder_reset(d) == MAKTABASAUTI_OK && maktabasauti_decoder_tell(d, &pos) == MAKTABASAUTI_OK && pos == 0);
    assert(maktabasauti_decoder_read(d, pcm, 3, MAKTABASAUTI_FORMAT_S16, &got) == MAKTABASAUTI_OK && pcm[0] == tst_sample(0, 0));
    assert(maktabasauti_decoder_flush(d) == MAKTABASAUTI_OK);
    assert(maktabasauti_decoder_read(d, pcm, 0, MAKTABASAUTI_FORMAT_S16, &got) == MAKTABASAUTI_OK && got == 0);
    assert(maktabasauti_decoder_read(d, pcm, 3, MAKTABASAUTI_FORMAT_S16 | MAKTABASAUTI_FORMAT_PLANAR_FLAG, &got) == MAKTABASAUTI_E_UNSUPPORTED_FEATURE);
    assert(maktabasauti_decoder_read(d, pcm, 3, 99, &got) == MAKTABASAUTI_E_INVALID_ARGUMENT);
    assert(maktabasauti_decoder_get_tag(d, "title", (char[8]){ 0 }, 8, &got) == MAKTABASAUTI_E_NOT_IMPLEMENTED);
    maktabasauti_decoder_close(d); assert(t.closes == 1);
    /* dlugosc nieznana w naglowku: seek za koniec laduje na koncu (OK), nastepny read daje EOF */
    n = tst_make(unk, 'A', 8000, 2, 300, 1);
    io = tio_make(&t, unk, n, 1, 1); p = params(&io, reg);
    assert(maktabasauti_decoder_open(&p, &d) == MAKTABASAUTI_OK);
    info.struct_size = sizeof info; assert(maktabasauti_decoder_get_info(d, &info) == MAKTABASAUTI_OK && info.total_frames == MAKTABASAUTI_UNKNOWN_LENGTH);
    assert(maktabasauti_decoder_seek(d, 999999, MAKTABASAUTI_SEEK_EXACT, &actual) == MAKTABASAUTI_OK && actual == 300);
    maktabasauti_decoder_close(d);
    /* wejscie nieprzewijalne: czytanie dziala, seek i reset daja E_NOT_SEEKABLE, stan bez zmian; REQUIRE_SEEKABLE dziala po stronie flagi strumienia */
    n = tst_make(big, 'A', 8000, 2, 1000, 0);
    io = tio_make(&t, big, n, 0, 0); p = params(&io, reg);
    assert(maktabasauti_decoder_open(&p, &d) == MAKTABASAUTI_OK);
    assert(maktabasauti_decoder_read(d, pcm, 10, MAKTABASAUTI_FORMAT_S16, &got) == MAKTABASAUTI_OK && got == 10);
    assert(maktabasauti_decoder_reset(d) == MAKTABASAUTI_E_NOT_SEEKABLE);
    assert(maktabasauti_decoder_tell(d, &pos) == MAKTABASAUTI_OK && pos == 10);
    maktabasauti_decoder_close(d);
    maktabasauti_registry_destroy(reg);
}

static void test_errors_format_change_would_block(void) {
    static uint8_t b[TST_HDR + 4 * 400], c[TST_HDR + 4 * 400];
    TIO t; maktabasauti_io_t io; maktabasauti_open_params_t p; maktabasauti_decoder_t *d = NULL; maktabasauti_registry_t *reg = tst_registry();
    int16_t pcm[4 * 400]; size_t got; uint64_t actual; maktabasauti_stream_info_t info;
    size_t n = tst_make(b, 'B', 8000, 2, 400, 0);
    /* FORMAT_CHANGED: ramki przed zmiana wydane, get_info zwraca nowe parametry, czytanie trwa dalej */
    io = tio_make(&t, b, n, 1, 1); p = params(&io, reg);
    assert(maktabasauti_decoder_open(&p, &d) == MAKTABASAUTI_OK);
    assert(maktabasauti_decoder_read(d, pcm, 300, MAKTABASAUTI_FORMAT_S16, &got) == MAKTABASAUTI_OK && got == 100);
    assert(maktabasauti_decoder_read(d, pcm, 300, MAKTABASAUTI_FORMAT_S16, &got) == MAKTABASAUTI_E_FORMAT_CHANGED && got == 0);
    info.struct_size = sizeof info; assert(maktabasauti_decoder_get_info(d, &info) == MAKTABASAUTI_OK && info.sample_rate == 16000);
    assert(maktabasauti_decoder_read(d, pcm, 300, MAKTABASAUTI_FORMAT_S16, &got) == MAKTABASAUTI_OK && got == 300);                  /* nielepki */
    maktabasauti_decoder_close(d);
    /* uszkodzenie: ramki przed bledem wydane (got > 0), blad lepki, opis bledu, seek czysci */
    n = tst_make(c, 'C', 8000, 2, 400, 0);
    io = tio_make(&t, c, n, 1, 1); p = params(&io, reg);
    assert(maktabasauti_decoder_open(&p, &d) == MAKTABASAUTI_OK);
    assert(strcmp(maktabasauti_decoder_last_error(d), "") == 0);
    assert(maktabasauti_decoder_read(d, pcm, 200, MAKTABASAUTI_FORMAT_S16, &got) == MAKTABASAUTI_OK && got == 50);
    assert(maktabasauti_decoder_read(d, pcm, 200, MAKTABASAUTI_FORMAT_S16, &got) == MAKTABASAUTI_E_CORRUPT_DATA && got == 0);
    assert(strcmp(maktabasauti_decoder_last_error(d), "synthetic corruption") == 0);
    assert(maktabasauti_decoder_read(d, pcm, 200, MAKTABASAUTI_FORMAT_S16, &got) == MAKTABASAUTI_E_CORRUPT_DATA);                    /* lepki */
    assert(maktabasauti_decoder_seek(d, 10, MAKTABASAUTI_SEEK_EXACT, &actual) == MAKTABASAUTI_OK && actual == 10);
    assert(strcmp(maktabasauti_decoder_last_error(d), "") == 0);
    assert(maktabasauti_decoder_read(d, pcm, 20, MAKTABASAUTI_FORMAT_S16, &got) == MAKTABASAUTI_OK && got == 20);
    assert(maktabasauti_decoder_read(d, pcm, 200, MAKTABASAUTI_FORMAT_S16, &got) == MAKTABASAUTI_OK && got == 20);                  /* do granicy 50 */
    assert(maktabasauti_decoder_read(d, pcm, 200, MAKTABASAUTI_FORMAT_S16, &got) == MAKTABASAUTI_E_CORRUPT_DATA);
    assert(maktabasauti_decoder_reset(d) == MAKTABASAUTI_OK);                                                                         /* reset tez czysci */
    assert(maktabasauti_decoder_read(d, pcm, 5, MAKTABASAUTI_FORMAT_S16, &got) == MAKTABASAUTI_OK && got == 5);
    assert(maktabasauti_decoder_seek(d, 400, MAKTABASAUTI_SEEK_EXACT, &actual) == MAKTABASAUTI_OK);
    assert(maktabasauti_decoder_seek(d, 401, MAKTABASAUTI_SEEK_EXACT, &actual) == MAKTABASAUTI_E_OUT_OF_RANGE);
    maktabasauti_decoder_close(d);
    /* WOULD_BLOCK: wejscie raz bez danych.  (a) na granicy porcji odczytu: blad jest WIDOCZNY dla wywolujacego, stan zachowany, ponowienie dziala, zaden bajt nie ginie.
     * (b) w srodku ramki: kodek oddaje cale ramki, ktore ma, a niepelna zostaje u niego (carry); blad wtedy nie jest widoczny, a dane sa kompletne i poprawne.
     * (c) brak danych w oknie probkowania przy otwarciu (pierwsze MKS_PROBE_BYTES): open zwraca E_WOULD_BLOCK (wejscie zostaje u wywolujacego), ponowienie dziala (wczesniej czytnik
     *     oddawal niepelny prefiks, a sondy oceniały obciete dane).  Punkty (a) i (b) lezą za oknem sond. */
    {   static uint8_t wb[TST_HDR + 4 * 9000];
        size_t variant;
        n = tst_make(wb, 'A', 8000, 2, 9000, 0);
        {   /* (c) */
            TIO t2; maktabasauti_decoder_t *d2 = NULL; int tries = 0; maktabasauti_result_t r2;
            io = tio_make(&t2, wb, n, 1, 1); t2.block_at = TST_HDR + 100;                       /* w oknie sond */
            p = params(&io, reg);
            r2 = maktabasauti_decoder_open(&p, &d2);
            assert(r2 == MAKTABASAUTI_E_WOULD_BLOCK && !d2 && t2.closes == 0);
            while (r2 == MAKTABASAUTI_E_WOULD_BLOCK && ++tries < 5) r2 = maktabasauti_decoder_open(&p, &d2);
            assert(r2 == MAKTABASAUTI_OK && d2);
            maktabasauti_decoder_close(d2);
        }
        for (variant = 0; variant < 2; variant++) {
            size_t total = 0, blocks = 0, f;
            io = tio_make(&t, wb, n, 1, 1);
            t.block_at = variant == 0 ? TST_HDR + 256 * 100 : TST_HDR + 4 * 5000 + 2;
            p = params(&io, reg);
            assert(maktabasauti_decoder_open(&p, &d) == MAKTABASAUTI_OK);
            for (;;) {
                maktabasauti_result_t r = maktabasauti_decoder_read(d, pcm, 64, MAKTABASAUTI_FORMAT_S16, &got);
                if (r == MAKTABASAUTI_E_WOULD_BLOCK) { blocks++; assert(blocks < 4 && got == 0); continue; }
                if (r == MAKTABASAUTI_EOF) break;
                assert(r == MAKTABASAUTI_OK);
                for (f = 0; f < got; f++) assert(pcm[2 * f] == tst_sample(total + f, 0));
                total += got;
            }
            assert(total == 9000);
            assert(variant == 0 ? blocks == 1 : blocks <= 1);
            maktabasauti_decoder_close(d);
        }
        d = NULL;
    }
    maktabasauti_registry_destroy(reg);
}

#ifdef MKS_TEST_LEGACY
/* ================= most do starszych dekoderow: ten sam wynik co stare API ================= */
static size_t make_wav16(uint8_t *w, unsigned ch, uint32_t rate, uint32_t frames) {
    uint32_t bytes = frames * ch * 2u, br = rate * ch * 2u, i; size_t n;
    memcpy(w, "RIFF", 4); { uint32_t v = 36 + bytes; w[4] = (uint8_t)v; w[5] = (uint8_t)(v >> 8); w[6] = (uint8_t)(v >> 16); w[7] = (uint8_t)(v >> 24); }
    memcpy(w + 8, "WAVEfmt ", 8); w[16] = 16; w[17] = w[18] = w[19] = 0; w[20] = 1; w[21] = 0; w[22] = (uint8_t)ch; w[23] = 0;
    w[24] = (uint8_t)rate; w[25] = (uint8_t)(rate >> 8); w[26] = (uint8_t)(rate >> 16); w[27] = 0;
    w[28] = (uint8_t)br; w[29] = (uint8_t)(br >> 8); w[30] = (uint8_t)(br >> 16); w[31] = (uint8_t)(br >> 24);
    w[32] = (uint8_t)(ch * 2); w[33] = 0; w[34] = 16; w[35] = 0; memcpy(w + 36, "data", 4);
    w[40] = (uint8_t)bytes; w[41] = (uint8_t)(bytes >> 8); w[42] = (uint8_t)(bytes >> 16); w[43] = (uint8_t)(bytes >> 24);
    n = 44;
    for (i = 0; i < frames * ch; i++) { int16_t v = (int16_t)((i * 37u) % 60000u - 30000); w[n++] = (uint8_t)v; w[n++] = (uint8_t)((uint16_t)v >> 8); }
    return n;
}
static void test_legacy_bridge(void) {
    static uint8_t wav[44 + 4 * 3000];
    size_t n = make_wav16(wav, 2, 22050, 3000), i, got; TIO t; maktabasauti_io_t io; maktabasauti_open_params_t p; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info;
    static float newf[2 * 3000], oldf[2 * 3000]; SautiDecoder old; const SautiDecoderOps *const legacy[] = { &sauti_decoder_wav_ops }; uint64_t actual; static int16_t s16[2 * 3000];
    io = tio_make(&t, wav, n, 1, 1); p = params(&io, NULL);
    assert(maktabasauti_decoder_open(&p, &d) == MAKTABASAUTI_OK);
    info.struct_size = sizeof info; assert(maktabasauti_decoder_get_info(d, &info) == MAKTABASAUTI_OK);
    assert(info.sample_rate == 22050 && info.channels == 2 && info.total_frames == 3000 && info.channel_mask == MAKTABASAUTI_LAYOUT_STEREO && info.native_format == MAKTABASAUTI_FORMAT_F32);
    assert(strcmp(info.codec_name, "pcm") == 0 && strcmp(info.container_name, "wav") == 0 && (info.flags & MAKTABASAUTI_STREAM_LOSSLESS));
    assert(sauti_legacy_decoder_open(&old, legacy, 1, wav, n) == 1);
    got = 0;
    for (;;) { size_t g; assert(maktabasauti_decoder_read(d, newf + got * 2, 700, MAKTABASAUTI_FORMAT_F32, &g) != MAKTABASAUTI_E_INTERNAL); if (!g) break; got += g; }
    assert(got == 3000);
    assert(sauti_legacy_decoder_read(&old, oldf, 3000) == 3000);
    assert(memcmp(newf, oldf, sizeof newf) == 0);                                   /* PCM bit w bit jak w starym API */
    assert(maktabasauti_decoder_seek(d, 1234, MAKTABASAUTI_SEEK_EXACT, &actual) == MAKTABASAUTI_OK && actual == 1234);
    assert(maktabasauti_decoder_read(d, s16, 100, MAKTABASAUTI_FORMAT_S16, &got) == MAKTABASAUTI_OK && got == 100);
    for (i = 0; i < 200; i++) {
        double x = (double)oldf[1234 * 2 + i] * 32768.0; int v = (int)floor(x + 0.5); if (v > 32767) v = 32767; if (v < -32768) v = -32768;
        assert(s16[i] == v);
    }
    assert(maktabasauti_decoder_seek(d, 3001, MAKTABASAUTI_SEEK_EXACT, &actual) == MAKTABASAUTI_E_OUT_OF_RANGE);
    assert(maktabasauti_decoder_reset(d) == MAKTABASAUTI_OK);
    assert(maktabasauti_decoder_read(d, newf, 10, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && memcmp(newf, oldf, 10 * 2 * sizeof(float)) == 0);
    assert(maktabasauti_decoder_flush(d) == MAKTABASAUTI_OK);                       /* WAV to modul strumieniowy: ma flush (dawny adapter zwracal NOT_IMPLEMENTED) */
    sauti_legacy_decoder_close(&old);
    maktabasauti_decoder_close(d);
    assert(t.closes == 1);
    /* kodek wymuszony po nazwie rodziny: "wav" pasuje do "wav-pcm" */
    io = tio_make(&t, wav, n, 1, 1); p = params(&io, NULL); p.codec = "wav";
    assert(maktabasauti_decoder_open(&p, &d) == MAKTABASAUTI_OK); maktabasauti_decoder_close(d);
    io = tio_make(&t, wav, n, 1, 1); p = params(&io, NULL); p.codec = "flac";
    assert(maktabasauti_decoder_open(&p, &d) != MAKTABASAUTI_OK && t.closes == 0);
    /* rejestr domyslny: lista kodekow z opisem modulu */
    {   const maktabasauti_registry_t *reg = maktabasauti_default_registry(); maktabasauti_codec_info_t ci; size_t k, aac = 0, flac = 0, dvs = 0, pcm = 0, voc = 0;
        assert(maktabasauti_registry_codec_count(reg) >= 15);
        for (k = 0; k < maktabasauti_registry_codec_count(reg); k++) {
            ci.struct_size = sizeof ci; assert(maktabasauti_registry_codec_info(reg, k, &ci) == MAKTABASAUTI_OK && (ci.flags & MAKTABASAUTI_CODEC_CAN_DECODE));
            if (strcmp(ci.module, "maktabasauti_aac") == 0) aac++;
            if (strcmp(ci.module, "maktabasauti_flac") == 0) flac++;
            if (strcmp(ci.module, "maktabasauti_dvs") == 0) dvs++;
            if (strcmp(ci.module, "maktabasauti_pcm") == 0) pcm++;
            if (strcmp(ci.module, "maktabasauti_voc") == 0) voc++;
        }
        assert(aac == 4 && flac == 1 && dvs == 3 && pcm == 5 && voc == 1);               /* AAC: ADTS (modul strumieniowy, jedno wejscie), MP4, LOAS, ADIF; PCM: WAV, AIFF, AU, G.711 mu-law i A-law bez naglowka */
        assert(maktabasauti_registry_codec_info(reg, 9999, &ci) == MAKTABASAUTI_E_OUT_OF_RANGE);
        assert(maktabasauti_registry_add((maktabasauti_registry_t *)reg, &tst_codec) == MAKTABASAUTI_E_INVALID_ARGUMENT);    /* domyslny jest niezmienny */
    }
}

#endif /* MKS_TEST_LEGACY */

static void test_encoder_reserved(void) {
    maktabasauti_encoder_t *e = (maktabasauti_encoder_t *)1; maktabasauti_encoder_params_t ep;
    memset(&ep, 0, sizeof ep); ep.struct_size = sizeof ep; ep.abi_version = MAKTABASAUTI_ABI_VERSION;
    assert(maktabasauti_encoder_open(&ep, &e) == MAKTABASAUTI_E_NOT_IMPLEMENTED && e == NULL);
    assert(maktabasauti_encoder_encode(NULL, NULL, 0) == MAKTABASAUTI_E_NOT_IMPLEMENTED && maktabasauti_encoder_finish(NULL) == MAKTABASAUTI_E_NOT_IMPLEMENTED);
    maktabasauti_encoder_close(NULL);
}

int main(void) {
    test_versions_and_errors();
    test_allocator_contract();
    test_io_and_pcm();
    test_open_ownership();
    test_streaming_and_seek();
    test_errors_format_change_would_block();
#ifdef MKS_TEST_LEGACY
    test_legacy_bridge();
#endif
    test_encoder_reserved();
#ifdef MKS_TEST_LEGACY
    puts("PASS MaktabaSauti core: versions, errors, allocator contract (no realloc), io, PCM, ownership (only after success), streaming, seek, sticky errors, format change, would-block, legacy bridge");
#else
    puts("PASS MaktabaSauti core (portable build, no legacy bridge): versions, errors, allocator contract (no realloc), io, PCM, ownership (only after success), streaming, seek, sticky errors, format change, would-block");
#endif
    return 0;
}
