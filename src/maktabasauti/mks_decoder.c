/* MaktabaSauti: dekoder.  Jeden uchwyt = jeden niezalezny stan (kodek, czytnik, bufory, opcje, alokator); brak stanu globalnego.
 * WLASNOSC WEJSCIA: maktabasauti_decoder_open przejmuje wejscie (callback close wolany przez maktabasauti_decoder_close) WYLACZNIE po zwrocie OK.  Kazda sciezka bledu
 * zwalnia tylko to, co sama zajela, i NIE wola io.close. */
#include "mks_internal.h"
#include <string.h>

struct maktabasauti_decoder {
    mks_alloc_t alloc;
    maktabasauti_io_t io;                       /* wlasnosc dekodera dopiero po udanym open */
    mks_reader_t reader;
    const maktabasauti_codec_t *codec;
    void *state;
    maktabasauti_host_t host;
    maktabasauti_stream_info_t info;
    uint64_t position;
    int eof;
    maktabasauti_result_t sticky;               /* 0 = brak bledu; lepki do seek/reset/flush */
    char error[160];
    maktabasauti_option_t *options;             /* gleboka kopia opcji (jeden blok z tekstami) */
    size_t option_count;
    uint32_t open_flags;
    maktabasauti_log_fn log;
    void *log_user;
    int32_t log_level;
    mks_dither_t dither;
    void *scratch;                              /* bufor konwersji formatu */
    size_t scratch_cap;
};

static void set_error(maktabasauti_decoder_t *d, maktabasauti_result_t r, const char *msg) {
    size_t n = msg ? strlen(msg) : 0u;
    if (n >= sizeof d->error) n = sizeof d->error - 1u;
    if (n) memcpy(d->error, msg, n);
    d->error[n] = '\0';
    (void)r;
}
static void dec_log(const maktabasauti_decoder_t *d, int level, const char *msg) {
    if (d->log && level <= d->log_level) d->log(d->log_user, level, msg);
}

/* ---- usługi hosta dla modułów ---- */
static void *MAKTABASAUTI_CALL h_alloc(void *u, size_t n) { return mks_alloc(&((maktabasauti_decoder_t *)u)->alloc, n); }
static void *MAKTABASAUTI_CALL h_realloc(void *u, void *p, size_t n) {
    maktabasauti_decoder_t *d = (maktabasauti_decoder_t *)u;
    return p ? mks_realloc(&d->alloc, p, n) : mks_alloc(&d->alloc, n);
}
static void MAKTABASAUTI_CALL h_free(void *u, void *p) { mks_free(&((maktabasauti_decoder_t *)u)->alloc, p); }
static int64_t MAKTABASAUTI_CALL h_read(void *u, maktabasauti_reader_t *r, void *b, size_t n) {
    maktabasauti_decoder_t *d = (maktabasauti_decoder_t *)u;
    return r == &d->reader ? mks_reader_read(r, b, n) : MAKTABASAUTI_E_INVALID_ARGUMENT;
}
static int64_t MAKTABASAUTI_CALL h_peek(void *u, maktabasauti_reader_t *r, void *b, size_t n) {
    maktabasauti_decoder_t *d = (maktabasauti_decoder_t *)u;
    return r == &d->reader ? mks_reader_peek(r, b, n) : MAKTABASAUTI_E_INVALID_ARGUMENT;
}
static int64_t MAKTABASAUTI_CALL h_seek(void *u, maktabasauti_reader_t *r, int64_t o, int w) {
    maktabasauti_decoder_t *d = (maktabasauti_decoder_t *)u;
    return r == &d->reader ? mks_reader_seek(r, o, w) : MAKTABASAUTI_E_INVALID_ARGUMENT;
}
static int64_t MAKTABASAUTI_CALL h_tell(void *u, maktabasauti_reader_t *r) {
    maktabasauti_decoder_t *d = (maktabasauti_decoder_t *)u;
    return r == &d->reader ? mks_reader_tell(r) : MAKTABASAUTI_E_INVALID_ARGUMENT;
}
static int64_t MAKTABASAUTI_CALL h_size(void *u, maktabasauti_reader_t *r) {
    maktabasauti_decoder_t *d = (maktabasauti_decoder_t *)u;
    return r == &d->reader ? mks_reader_size(r) : MAKTABASAUTI_E_INVALID_ARGUMENT;
}
static maktabasauti_result_t MAKTABASAUTI_CALL h_map(void *u, maktabasauti_reader_t *r, const uint8_t **data, size_t *size) {
    maktabasauti_decoder_t *d = (maktabasauti_decoder_t *)u;
    if (r != &d->reader || !data || !size) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    return mks_reader_map(r, data, size);
}
static uint32_t MAKTABASAUTI_CALL h_input_flags(void *u, maktabasauti_reader_t *r) {
    maktabasauti_decoder_t *d = (maktabasauti_decoder_t *)u;
    if (r != &d->reader) return 0u;
    return ((d->reader.mem || d->io.seek) ? MAKTABASAUTI_INPUT_SEEKABLE : 0u) | (mks_reader_size(r) >= 0 ? MAKTABASAUTI_INPUT_SIZE_KNOWN : 0u);
}
static const char *MAKTABASAUTI_CALL h_option(void *u, const char *key) {
    maktabasauti_decoder_t *d = (maktabasauti_decoder_t *)u;
    size_t i;
    if (!key) return NULL;
    for (i = 0; i < d->option_count; i++)
        if (strcmp(d->options[i].key, key) == 0) return d->options[i].value;
    if ((d->open_flags & MAKTABASAUTI_OPEN_NO_GAPLESS) && strcmp(key, "gapless") == 0) return "off";
    return NULL;
}
static void MAKTABASAUTI_CALL h_log(void *u, int level, const char *msg) { dec_log((maktabasauti_decoder_t *)u, level, msg); }

/* ---- pomocnicze ---- */
static int valid_native_format(maktabasauti_sample_format_t f) { return f >= MAKTABASAUTI_FORMAT_U8 && f <= MAKTABASAUTI_FORMAT_F64; }
static unsigned popcount64(uint64_t v) { unsigned n = 0; while (v) { n += (unsigned)(v & 1u); v >>= 1; } return n; }

static maktabasauti_result_t copy_options(maktabasauti_decoder_t *d, const maktabasauti_option_t *opts, size_t count) {
    size_t i, bytes = 0;
    char *pool;
    if (!opts || count == 0) return MAKTABASAUTI_OK;
    for (i = 0; i < count; i++) {
        if (!opts[i].key) return MAKTABASAUTI_E_INVALID_ARGUMENT;
        bytes += strlen(opts[i].key) + 1u + (opts[i].value ? strlen(opts[i].value) : 0u) + 1u;
    }
    if (count > ((size_t)-1) / sizeof *d->options) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    d->options = (maktabasauti_option_t *)mks_alloc(&d->alloc, count * sizeof *d->options + bytes);
    if (!d->options) return MAKTABASAUTI_E_NO_MEMORY;
    pool = (char *)(d->options + count);
    for (i = 0; i < count; i++) {
        size_t kl = strlen(opts[i].key), vl = opts[i].value ? strlen(opts[i].value) : 0u;
        memcpy(pool, opts[i].key, kl + 1u);
        d->options[i].key = pool; pool += kl + 1u;
        memcpy(pool, opts[i].value ? opts[i].value : "", vl + 1u);
        d->options[i].value = pool; pool += vl + 1u;
    }
    d->option_count = count;
    return MAKTABASAUTI_OK;
}

static void release_partial(maktabasauti_decoder_t *d) {      /* zwalnia wszystko poza wejsciem uzytkownika (nie wola io.close) */
    mks_alloc_t a = d->alloc;
    mks_reader_free(&d->reader);
    mks_free(&a, d->options);
    mks_free(&a, d->scratch);
    mks_free(&a, d);
}

typedef struct { size_t index; int32_t score; } candidate_t;

static int name_matches(const char *forced, const char *name) {
    size_t n = strlen(forced);
    return strcmp(forced, name) == 0 || (strncmp(forced, name, n) == 0 && name[n] == '-');   /* "aac" pasuje do "aac-native-mp4" */
}

static int error_rank(maktabasauti_result_t r) {
    switch (r) {
    case MAKTABASAUTI_E_UNSUPPORTED_FEATURE: return 4;
    case MAKTABASAUTI_E_CORRUPT_DATA: case MAKTABASAUTI_E_TRUNCATED: return 3;
    case MAKTABASAUTI_E_NOT_SEEKABLE: return 2;
    default: return 1;
    }
}

maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_decoder_open(const maktabasauti_open_params_t *params, maktabasauti_decoder_t **out) {
    maktabasauti_decoder_t *d;
    const maktabasauti_registry_t *reg;
    maktabasauti_result_t r, best_err = MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    const maktabasauti_allocator_t *user_alloc;
    mks_alloc_t alloc;
    uint8_t *prefix;
    int64_t pn;
    candidate_t *cands;
    size_t ncands = 0, i, attempts = 0;
    const char *forced = NULL, *hint = NULL;
    uint32_t flags = 0;
    int skipped_seekable = 0;

    if (!out) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    *out = NULL;
    if (!params || !MKS_HAS_FIELD(params, maktabasauti_open_params_t, io)) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if (params->abi_version != MAKTABASAUTI_ABI_VERSION) return MAKTABASAUTI_E_ABI_MISMATCH;
    if (params->io.struct_size < offsetof(maktabasauti_io_t, read) + sizeof params->io.read || !params->io.read) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if (MKS_HAS_FIELD(params, maktabasauti_open_params_t, flags)) flags = params->flags;
    if (flags & ~(MAKTABASAUTI_OPEN_REQUIRE_SEEKABLE | MAKTABASAUTI_OPEN_NO_GAPLESS)) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    user_alloc = MKS_HAS_FIELD(params, maktabasauti_open_params_t, allocator) ? params->allocator : NULL;
    reg = MKS_HAS_FIELD(params, maktabasauti_open_params_t, registry) && params->registry ? params->registry : maktabasauti_default_registry();
    if (MKS_HAS_FIELD(params, maktabasauti_open_params_t, codec)) forced = params->codec;
    if (MKS_HAS_FIELD(params, maktabasauti_open_params_t, name_hint)) hint = params->name_hint;
    r = mks_alloc_init(&alloc, user_alloc);
    if (r != MAKTABASAUTI_OK) return r;

    d = (maktabasauti_decoder_t *)mks_calloc(&alloc, sizeof *d);
    if (!d) return MAKTABASAUTI_E_NO_MEMORY;
    d->alloc = alloc;
    d->open_flags = flags;
    memset(&d->io, 0, sizeof d->io);
    d->io.struct_size = sizeof d->io;
    {   /* kopia wejscia tylko do pol, ktore wywolujacy ma w swojej strukturze */
        size_t keep = params->io.struct_size < sizeof d->io ? params->io.struct_size : sizeof d->io;
        memcpy(&d->io, &params->io, keep);
        d->io.struct_size = sizeof d->io;
    }
    if (MKS_HAS_FIELD(params, maktabasauti_open_params_t, log)) { d->log = params->log; }
    if (MKS_HAS_FIELD(params, maktabasauti_open_params_t, log_user)) d->log_user = params->log_user;
    if (MKS_HAS_FIELD(params, maktabasauti_open_params_t, log_level)) d->log_level = params->log_level;
    mks_dither_init(&d->dither, 0);

    if (MKS_HAS_FIELD(params, maktabasauti_open_params_t, option_count)) {
        r = copy_options(d, params->options, params->option_count);
        if (r != MAKTABASAUTI_OK) { release_partial(d); return r; }
    }
    {   const char *dv = h_option(d, "dither");
        if (dv && strcmp(dv, "tpdf") == 0) d->dither.enabled = 1; }

    r = mks_reader_init(&d->reader, &d->alloc, &d->io);
    if (r != MAKTABASAUTI_OK) { release_partial(d); return r; }

    d->host.struct_size = sizeof d->host;
    d->host.user = d;
    d->host.alloc = h_alloc; d->host.realloc = h_realloc; d->host.free = h_free;
    d->host.read = h_read; d->host.peek = h_peek; d->host.seek = h_seek; d->host.tell = h_tell; d->host.size = h_size;
    d->host.map = h_map; d->host.option = h_option; d->host.log = h_log; d->host.input_flags = h_input_flags;

    prefix = (uint8_t *)mks_alloc(&d->alloc, MKS_PROBE_BYTES);
    cands = reg->count ? (candidate_t *)mks_alloc(&d->alloc, reg->count * sizeof *cands) : NULL;
    if (!prefix || (reg->count && !cands)) { mks_free(&d->alloc, prefix); mks_free(&d->alloc, cands); release_partial(d); return MAKTABASAUTI_E_NO_MEMORY; }

    pn = mks_reader_peek(&d->reader, prefix, MKS_PROBE_BYTES);
    if (pn < 0) { r = (maktabasauti_result_t)pn; goto fail; }
    if (pn == 0) { r = MAKTABASAUTI_E_UNSUPPORTED_FORMAT; goto fail; }   /* pusty plik */

    for (i = 0; i < reg->count; i++) {
        const maktabasauti_codec_t *c = reg->codecs[i];
        int32_t score;
        size_t at;
        if (forced && forced[0] && !name_matches(forced, c->name)) continue;
        if ((c->flags & MAKTABASAUTI_CODEC_FLAG_NEEDS_SEEKABLE_INPUT) && !d->io.seek) { skipped_seekable = 1; continue; }
        /* WSZYSTKIE sondy dostaja ograniczony prefiks (<= MKS_PROBE_BYTES): rozpoznawanie nie wczytuje pliku.  Kodek z NEEDS_FULL_BUFFER dostaje cale wejscie dopiero w open()
         * przez host->map (wybranego kandydata), z tym samym buforem czytnika co prefiks. */
        score = c->probe(c, prefix, (size_t)pn, hint);
        if (forced && forced[0] && score <= 0) score = 1;              /* wymuszony kodek otwieramy nawet bez rozpoznania */
        if (score <= 0) continue;
        if (score > 100) score = 100;
        for (at = ncands; at > 0 && cands[at - 1].score < score; at--) cands[at] = cands[at - 1];   /* wg pewnosci malejaco, przy remisie kolejnosc rejestru */
        cands[at].index = i; cands[at].score = score;
        ncands++;
    }
    if (ncands == 0) { r = skipped_seekable ? MAKTABASAUTI_E_NOT_SEEKABLE : best_err; goto fail; }

    for (i = 0; i < ncands; i++) {
        const maktabasauti_codec_t *c = reg->codecs[cands[i].index];
        void *state = NULL;
        int64_t rewound;
        memset(&d->info, 0, sizeof d->info);
        d->info.struct_size = sizeof d->info;
        rewound = mks_reader_seek(&d->reader, 0, 0);
        if (rewound < 0 && attempts > 0) { r = best_err; goto fail; }   /* po nieudanej probie nie da sie wrocic: kolejny kandydat bylby czytany od srodka */
        attempts++;
        r = c->open(c, &d->host, &d->reader, &d->info, &state);
        if (r == MAKTABASAUTI_OK) {
            if (!state || d->info.sample_rate == 0 || d->info.channels == 0 || d->info.channels > 255 || !valid_native_format(d->info.native_format)) {
                if (state) c->close(state);
                dec_log(d, MAKTABASAUTI_LOG_ERROR, "codec returned invalid stream info");
                r = MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
            } else if ((flags & MAKTABASAUTI_OPEN_REQUIRE_SEEKABLE) && !(d->info.flags & MAKTABASAUTI_STREAM_SEEKABLE)) {
                c->close(state);
                r = MAKTABASAUTI_E_NOT_SEEKABLE;
                goto fail;
            } else {
                d->codec = c; d->state = state;
                if (d->info.channel_mask && popcount64(d->info.channel_mask) != d->info.channels) {
                    dec_log(d, MAKTABASAUTI_LOG_WARNING, "channel mask does not match the channel count; treated as unspecified");
                    d->info.channel_mask = 0;
                }
                d->info.struct_size = sizeof d->info;
                break;
            }
        }
        if (r == MAKTABASAUTI_E_NO_MEMORY || r == MAKTABASAUTI_E_IO || r == MAKTABASAUTI_E_WOULD_BLOCK || r == MAKTABASAUTI_E_NOT_SEEKABLE) goto fail;
        if (error_rank(r) > error_rank(best_err)) best_err = r;
    }
    if (!d->codec) { r = best_err; goto fail; }

    mks_free(&d->alloc, prefix);
    mks_free(&d->alloc, cands);
    d->position = 0;
    *out = d;                                            /* TERAZ wejscie przechodzi na wlasnosc dekodera */
    return MAKTABASAUTI_OK;

fail:
    mks_free(&d->alloc, prefix);
    mks_free(&d->alloc, cands);
    /* E_WOULD_BLOCK przy otwarciu: wejscie zostaje u wywolujacego, a gdy sie przewija, wracamy na poczatek, zeby ponowienie open() widzialo komplet danych.  Wejscie nieprzewijalne
     * nie moze tego zagwarantowac: bajty juz z niego pobrane do prefiksu przepadaja (patrz docs/MAKTABASAUTI_API.md: zrodla na zywo daja pierwsze KiB przed open()). */
    if (r == MAKTABASAUTI_E_WOULD_BLOCK && d->io.seek) (void)d->io.seek(d->io.user, 0, 0);
    release_partial(d);                                  /* bez io.close: wejscie zostaje u wywolujacego */
    return r;
}

void MAKTABASAUTI_CALL maktabasauti_decoder_close(maktabasauti_decoder_t *d) {
    maktabasauti_io_t io;
    if (!d) return;
    if (d->codec && d->state) d->codec->close(d->state);
    io = d->io;
    release_partial(d);
    if (io.close) io.close(io.user);                     /* po udanym open wejscie nalezy do dekodera */
}

maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_decoder_get_info(const maktabasauti_decoder_t *d, maktabasauti_stream_info_t *out) {
    uint32_t cap, keep;
    if (!d || !out || out->struct_size < sizeof(uint32_t)) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    keep = out->struct_size;
    cap = keep < sizeof d->info ? keep : (uint32_t)sizeof d->info;
    memcpy(out, &d->info, cap);
    out->struct_size = keep;
    return MAKTABASAUTI_OK;
}

const char *MAKTABASAUTI_CALL maktabasauti_decoder_last_error(const maktabasauti_decoder_t *d) { return d ? d->error : ""; }

maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_decoder_tell(const maktabasauti_decoder_t *d, uint64_t *frame) {
    if (!d || !frame) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    *frame = d->position;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t ensure_scratch(maktabasauti_decoder_t *d, size_t bytes) {
    void *p;
    if (bytes <= d->scratch_cap) return MAKTABASAUTI_OK;
    p = d->scratch ? mks_realloc(&d->alloc, d->scratch, bytes) : mks_alloc(&d->alloc, bytes);
    if (!p) return MAKTABASAUTI_E_NO_MEMORY;
    d->scratch = p; d->scratch_cap = bytes;
    return MAKTABASAUTI_OK;
}

static void fail_sticky(maktabasauti_decoder_t *d, maktabasauti_result_t r) {
    const char *msg = d->codec->last_error ? d->codec->last_error(d->state) : NULL;
    d->sticky = r;
    set_error(d, r, msg && msg[0] ? msg : maktabasauti_result_message(r));
}

maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_decoder_read(maktabasauti_decoder_t *d, void *dst, size_t dst_frames, maktabasauti_sample_format_t format, size_t *frames_read) {
    size_t channels, nb, ob, frames = 0, total_samples;
    maktabasauti_result_t r;
    void *target;
    if (frames_read) *frames_read = 0;
    if (!d || !frames_read || (!dst && dst_frames)) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if (format & MAKTABASAUTI_FORMAT_PLANAR_FLAG) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    if (!valid_native_format(format)) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if (d->sticky) return d->sticky;                    /* lepki blad do seek/reset/flush */
    if (dst_frames == 0) return MAKTABASAUTI_OK;
    if (d->eof) return MAKTABASAUTI_EOF;
    channels = d->info.channels;
    nb = maktabasauti_format_bytes(d->info.native_format);
    ob = maktabasauti_format_bytes(format);
    if (dst_frames > ((size_t)-1) / channels / (nb > ob ? nb : ob)) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    total_samples = dst_frames * channels;
    if (format == d->info.native_format) target = dst;
    else {
        r = ensure_scratch(d, total_samples * nb);
        if (r != MAKTABASAUTI_OK) return r;
        target = d->scratch;
    }
    r = d->codec->read(d->state, target, dst_frames, &frames);
    if (frames > dst_frames) { fail_sticky(d, MAKTABASAUTI_E_INTERNAL); return MAKTABASAUTI_E_INTERNAL; }
    if (frames && format != d->info.native_format) {
        maktabasauti_result_t cr = mks_pcm_convert(d->scratch, d->info.native_format, dst, format, frames * channels, &d->dither);
        if (cr != MAKTABASAUTI_OK) { fail_sticky(d, cr); return cr; }
    }
    *frames_read = frames;
    d->position += frames;
    if (r == MAKTABASAUTI_OK) {
        if (frames == 0) { d->eof = 1; return MAKTABASAUTI_EOF; }     /* kodek nie powinien zwracac OK z zerem ramek: traktujemy jak koniec */
        return MAKTABASAUTI_OK;
    }
    if (r == MAKTABASAUTI_EOF) { d->eof = 1; return frames ? MAKTABASAUTI_OK : MAKTABASAUTI_EOF; }
    if (r == MAKTABASAUTI_E_WOULD_BLOCK) return r;                      /* nielepki: stan zachowany */
    if (r == MAKTABASAUTI_E_FORMAT_CHANGED) {                           /* ramki przed zmiana wydane w starym formacie; odswiez opis i kontynuuj */
        if (d->codec->get_info) {
            maktabasauti_stream_info_t fresh;
            memset(&fresh, 0, sizeof fresh);
            fresh.struct_size = sizeof fresh;
            if (d->codec->get_info(d->state, &fresh) == MAKTABASAUTI_OK && fresh.sample_rate && fresh.channels && valid_native_format(fresh.native_format)) {
                d->info = fresh; d->info.struct_size = sizeof d->info;
            }
        }
        return r;
    }
    if (r > 0) r = MAKTABASAUTI_E_INTERNAL;
    fail_sticky(d, r);
    return r;
}

maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_decoder_seek(maktabasauti_decoder_t *d, uint64_t frame, uint32_t flags, uint64_t *actual) {
    uint64_t act = frame;
    maktabasauti_result_t r;
    if (!d) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if (flags != MAKTABASAUTI_SEEK_EXACT && flags != MAKTABASAUTI_SEEK_APPROX) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if (!(d->info.flags & MAKTABASAUTI_STREAM_SEEKABLE) || !(d->codec->flags & MAKTABASAUTI_CODEC_FLAG_HAS_SEEK) || !d->codec->seek) return MAKTABASAUTI_E_NOT_SEEKABLE;
    if (d->info.total_frames != MAKTABASAUTI_UNKNOWN_LENGTH && frame > d->info.total_frames) return MAKTABASAUTI_E_OUT_OF_RANGE;   /* stan bez zmian */
    r = d->codec->seek(d->state, frame, flags, &act);
    if (r == MAKTABASAUTI_E_OUT_OF_RANGE || r == MAKTABASAUTI_E_INVALID_ARGUMENT || r == MAKTABASAUTI_E_NOT_SEEKABLE) return r;      /* kodek obiecuje stan bez zmian */
    if (r != MAKTABASAUTI_OK) { fail_sticky(d, r); return r; }
    /* EXACT: ladowanie dokladnie na `frame`; jedyny wyjatek to zrodlo o nieznanej dlugosci, gdzie act < frame oznacza "za koncem: wyladowano na koncu" */
    if (flags == MAKTABASAUTI_SEEK_EXACT && act != frame && (d->info.total_frames != MAKTABASAUTI_UNKNOWN_LENGTH || act > frame)) { fail_sticky(d, MAKTABASAUTI_E_INTERNAL); return MAKTABASAUTI_E_INTERNAL; }
    if (act > frame) { fail_sticky(d, MAKTABASAUTI_E_INTERNAL); return MAKTABASAUTI_E_INTERNAL; }   /* APPROX nigdy nie wyladuje dalej niz zadano */
    d->position = act;
    d->eof = 0;
    d->sticky = 0;
    d->error[0] = '\0';
    if (actual) *actual = act;
    return MAKTABASAUTI_OK;
}

maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_decoder_reset(maktabasauti_decoder_t *d) {
    maktabasauti_result_t r;
    if (!d) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    /* wejscie nieprzewijalne (poza trybem "cale wejscie w pamieci"): reset NIGDY sie nie udaje, niezaleznie od tego, ile jeszcze siedzi w buforze czytnika */
    if (!d->io.seek && !d->reader.mem) return MAKTABASAUTI_E_NOT_SEEKABLE;
    if (mks_reader_seek(&d->reader, 0, 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;   /* wejscie nie wraca na poczatek: stan bez zmian */
    if (d->codec->reset) r = d->codec->reset(d->state);
    else if ((d->codec->flags & MAKTABASAUTI_CODEC_FLAG_HAS_SEEK) && d->codec->seek) { uint64_t a = 0; r = d->codec->seek(d->state, 0, MAKTABASAUTI_SEEK_EXACT, &a); }
    else return MAKTABASAUTI_E_NOT_SEEKABLE;
    if (r != MAKTABASAUTI_OK) { fail_sticky(d, r); return r; }
    d->position = 0; d->eof = 0; d->sticky = 0; d->error[0] = '\0';
    return MAKTABASAUTI_OK;
}

maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_decoder_flush(maktabasauti_decoder_t *d) {
    maktabasauti_result_t r;
    if (!d) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if (!d->codec->flush) return MAKTABASAUTI_E_NOT_IMPLEMENTED;
    r = d->codec->flush(d->state);
    if (r != MAKTABASAUTI_OK) { fail_sticky(d, r); return r; }
    d->eof = 0; d->sticky = 0; d->error[0] = '\0';
    return MAKTABASAUTI_OK;
}

maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_decoder_get_tag(const maktabasauti_decoder_t *d, const char *key, char *buf, size_t cap, size_t *needed) {
    if (!d || !key || (!buf && cap) || !needed) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if (!d->codec->get_tag) return MAKTABASAUTI_E_NOT_IMPLEMENTED;
    return d->codec->get_tag(d->state, key, buf, cap, needed);
}

/* ---- kodowanie: zarezerwowane (etap 1: brak kodeka kodujacego) ---- */
maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_encoder_open(const maktabasauti_encoder_params_t *params, maktabasauti_encoder_t **out) {
    if (out) *out = NULL;
    if (!params || !out) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    return MAKTABASAUTI_E_NOT_IMPLEMENTED;
}
maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_encoder_encode(maktabasauti_encoder_t *e, const void *src, size_t frames) { (void)e; (void)src; (void)frames; return MAKTABASAUTI_E_NOT_IMPLEMENTED; }
maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_encoder_finish(maktabasauti_encoder_t *e) { (void)e; return MAKTABASAUTI_E_NOT_IMPLEMENTED; }
void MAKTABASAUTI_CALL maktabasauti_encoder_close(maktabasauti_encoder_t *e) { (void)e; }
