/* MaktabaSauti: most do starszych dekoderow Sauti (SautiDecoderOps) jako moduly kodekow.  Starsze dekodery maja interfejs "cala pamiec" (open(dane, dlugosc)), wiec te
 * opakowania sa oznaczone MAKTABASAUTI_CODEC_FLAG_NEEDS_FULL_BUFFER: host wczytuje cale wejscie (host->map) i probuje je przy kazdym otwarciu.  Dekodowanie jest to samo co w starym
 * API (wynik PCM bit w bit); strumieniowe moduly zastapia opakowania etapami (docs/MAKTABASAUTI_API.md, etap 2).  W wydaniu NIE ma opakowan opartych o FFmpeg ani o inne
 * zewnetrzne biblioteki (opus, wavpack, libxmp, libarchive): to osobne moduly, gdy beda niezalezne.  Rejestr domyslny jest stala. */
#include "mks_internal.h"
#include "sauti_decoder.h"
#include "sauti_decoder_wav.h"
#include "sauti_decoder_voc.h"
#include "sauti_decoder_aiff.h"
#include "sauti_decoder_aac_native.h"
#include "sauti_decoder_vorbis.h"
#include "sauti_decoder_midi.h"
#include "sauti_decoder_flac.h"
#include "sauti_decoder_avr.h"
#include "sauti_decoder_dvsm.h"
#include "sauti_decoder_dtm.h"
#include "sauti_decoder_ym.h"
#include "sauti_decoder_mp3.h"
#include <string.h>

typedef struct {
    const SautiDecoderOps *ops;
    const char *codec, *container, *profile;
    unsigned lossless;
} legacy_meta_t;

typedef struct {
    const legacy_meta_t *meta;
    const maktabasauti_host_t *host;
    void *state;
    SautiPcmInfo info;
} legacy_state_t;

static uint64_t default_mask(uint32_t channels) {
    switch (channels) {
    case 1: return MAKTABASAUTI_LAYOUT_MONO;
    case 2: return MAKTABASAUTI_LAYOUT_STEREO;
    case 6: return MAKTABASAUTI_LAYOUT_5_1;
    case 8: return MAKTABASAUTI_LAYOUT_7_1;
    default: return 0;                                  /* nie zgadujemy rozmieszczenia, gdy zrodlo go nie podaje */
    }
}

static void copy_name(char *dst, size_t cap, const char *src) {
    size_t n = src ? strlen(src) : 0u;
    if (n >= cap) n = cap - 1u;
    if (n) memcpy(dst, src, n);
    dst[n] = '\0';
}

static int32_t MAKTABASAUTI_CALL legacy_probe(const maktabasauti_codec_t *self, const uint8_t *prefix, size_t n, const char *hint) {
    const legacy_meta_t *m = (const legacy_meta_t *)self->user_data;
    (void)hint;
    if (!prefix || n == 0u) return 0;
    return m->ops->probe(prefix, n) ? 50 : 0;           /* remis rozstrzyga kolejnosc w rejestrze, jak w starym API */
}

static maktabasauti_result_t MAKTABASAUTI_CALL legacy_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *host, maktabasauti_reader_t *reader,
                                                           maktabasauti_stream_info_t *info, void **state) {
    const legacy_meta_t *m = (const legacy_meta_t *)self->user_data;
    const uint8_t *data = NULL;
    size_t size = 0;
    legacy_state_t *s;
    maktabasauti_result_t r;
    SautiPcmInfo pcm;
    *state = NULL;
    r = host->map(host->user, reader, &data, &size);
    if (r != MAKTABASAUTI_OK) return r;
    memset(&pcm, 0, sizeof pcm);
    s = (legacy_state_t *)host->alloc(host->user, sizeof *s);
    if (!s) return MAKTABASAUTI_E_NO_MEMORY;
    memset(s, 0, sizeof *s);
    s->meta = m;
    s->host = host;
    s->state = m->ops->open(data, size, &pcm);
    if (!s->state) { host->free(host->user, s); return MAKTABASAUTI_E_UNSUPPORTED_FEATURE; }   /* starszy dekoder nie podaje przyczyny */
    if (pcm.sample_rate == 0 || pcm.channels == 0 || pcm.channels > 32) {                      /* te same kontrole co w starym rejestrze */
        m->ops->close(s->state);
        host->free(host->user, s);
        return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    }
    s->info = pcm;
    info->sample_rate = pcm.sample_rate;
    info->channels = pcm.channels;
    info->channel_mask = default_mask(pcm.channels);
    info->native_format = MAKTABASAUTI_FORMAT_F32;       /* starsze dekodery oddaja float */
    info->bits_per_sample = 0;
    info->total_frames = pcm.total_frames ? pcm.total_frames : MAKTABASAUTI_UNKNOWN_LENGTH;
    info->flags = MAKTABASAUTI_STREAM_SEEKABLE | (m->lossless ? MAKTABASAUTI_STREAM_LOSSLESS : 0u);
    copy_name(info->codec_name, sizeof info->codec_name, m->codec);
    copy_name(info->codec_profile, sizeof info->codec_profile, m->profile);
    copy_name(info->container_name, sizeof info->container_name, m->container);
    *state = s;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL legacy_read(void *state, void *dst, size_t dst_frames, size_t *frames) {
    legacy_state_t *s = (legacy_state_t *)state;
    size_t got = s->meta->ops->read(s->state, (float *)dst, dst_frames);
    if (got > dst_frames) got = 0;                       /* te same kontrole co w starym API */
    *frames = got;
    return got ? MAKTABASAUTI_OK : MAKTABASAUTI_EOF;
}

static maktabasauti_result_t MAKTABASAUTI_CALL legacy_seek(void *state, uint64_t frame, uint32_t flags, uint64_t *actual) {
    legacy_state_t *s = (legacy_state_t *)state;
    (void)flags;
    if (s->info.total_frames && frame > s->info.total_frames) return MAKTABASAUTI_E_OUT_OF_RANGE;
    if (!s->meta->ops->seek(s->state, frame)) return MAKTABASAUTI_E_NOT_SEEKABLE;   /* starszy dekoder nie podaje przyczyny: przewijanie nie powiodlo sie (np. budzet pracy MIDI), stan bez zmian */
    *actual = frame;                                     /* starsze dekodery przewijaja dokladnie */
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL legacy_reset(void *state) {
    legacy_state_t *s = (legacy_state_t *)state;
    return s->meta->ops->seek(s->state, 0) ? MAKTABASAUTI_OK : MAKTABASAUTI_E_NOT_SEEKABLE;
}

static void MAKTABASAUTI_CALL legacy_close(void *state) {
    legacy_state_t *s = (legacy_state_t *)state;
    const maktabasauti_host_t *host;
    if (!s) return;
    host = s->host;
    s->meta->ops->close(s->state);
    host->free(host->user, s);
}

#define LEGACY(id, symbol, ename, descr, module, codec, container, profile, lossless, flags)                                                                           \
    static const legacy_meta_t meta_##id = { &symbol, codec, container, profile, lossless };                                                                            \
    static const maktabasauti_codec_t codec_##id = {                                                                                                                    \
        sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, ename, descr, module,                                                                                   \
        (flags) | MAKTABASAUTI_CODEC_FLAG_NEEDS_FULL_BUFFER | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0u, &meta_##id,                                                                \
        legacy_probe, legacy_open, legacy_read, legacy_seek, legacy_reset, NULL, NULL, NULL, NULL, legacy_close, { NULL }, { NULL } }

/* kolejnosc jak w odtwarzaczu: przy remisie wygrywa wczesniejszy */
LEGACY(wav,    sauti_decoder_wav_ops,               "wav-pcm",                      "RIFF/WAVE PCM",                        "maktabasauti_legacy", "pcm",    "wav",   "",         1, 0u);
LEGACY(voc,    sauti_decoder_voc_ops,               "creative-voc-pcm",             "Creative VOC PCM",                     "maktabasauti_legacy", "pcm",    "voc",   "",         1, 0u);
LEGACY(aiff,   sauti_decoder_aiff_ops,              "aiff-pcm",                     "AIFF PCM",                             "maktabasauti_legacy", "pcm",    "aiff",  "",         1, 0u);
LEGACY(aac,    sauti_decoder_aac_native_ops,        "aac-lc-native-adts",           "AAC-LC stereo, ADTS",                  "maktabasauti_aac",    "aac",    "adts",  "LC",       0, 0u);
LEGACY(aacm,   sauti_decoder_aac_mono_native_ops,   "aac-lc-native-mono-adts",      "AAC-LC mono, ADTS",                    "maktabasauti_aac",    "aac",    "adts",  "LC",       0, 0u);
LEGACY(aacx,   sauti_decoder_aac_multi_native_ops,  "aac-lc-native-multi-adts",     "AAC-LC multichannel, ADTS",            "maktabasauti_aac",    "aac",    "adts",  "LC",       0, 0u);
LEGACY(aache,  sauti_decoder_aac_he_native_ops,     "aac-he-native-adts",           "HE-AAC v1/v2 mono core, ADTS",         "maktabasauti_aac",    "aac",    "adts",  "HE-AAC",   0, 0u);
LEGACY(aachc,  sauti_decoder_aac_he_cpe_native_ops, "aac-he-cpe-native-adts",       "HE-AAC v1 stereo core, ADTS",          "maktabasauti_aac",    "aac",    "adts",  "HE-AAC",   0, 0u);
LEGACY(aacmp4, sauti_decoder_aac_mp4_native_ops,    "aac-native-mp4",               "AAC in MP4/M4A/MOV (also fragmented)", "maktabasauti_aac",    "aac",    "mp4",   "",         0, 0u);
LEGACY(aaclatm,sauti_decoder_aac_latm_native_ops,   "aac-native-latm",              "AAC in LOAS/LATM",                     "maktabasauti_aac",    "aac",    "loas",  "",         0, 0u);
LEGACY(aacadif,sauti_decoder_aac_adif_native_ops,   "aac-native-adif",              "AAC in ADIF",                          "maktabasauti_aac",    "aac",    "adif",  "",         0, 0u);
LEGACY(vorbis, sauti_decoder_vorbis_ops,            "ogg-vorbis-stb",               "Ogg Vorbis",                           "maktabasauti_legacy", "vorbis", "ogg",   "",         0, 0u);
LEGACY(midi,   sauti_decoder_midi_ops,              "midi-basic-synth",             "MIDI (basic synthesiser)",             "maktabasauti_legacy", "midi",   "midi",  "",         0, MAKTABASAUTI_CODEC_FLAG_EXPERIMENTAL);
LEGACY(flac,   sauti_decoder_flac_ops,              "flac-drflac",                  "FLAC",                                 "maktabasauti_flac",   "flac",   "flac",  "",         1, 0u);
LEGACY(avr,    sauti_decoder_avr_ops,               "atari-avr-pcm",                "Atari AVR PCM",                        "maktabasauti_dvs",    "pcm",    "avr",   "",         1, 0u);
LEGACY(dvsm,   sauti_decoder_dvsm_ops,              "atari-falcon-dvsm-pcm",        "Atari Falcon DVSM (PCM, Pack-2, Pack-4)", "maktabasauti_dvs", "dvs",    "dvs",   "",         0, 0u);
LEGACY(dtm,    sauti_decoder_dtm_ops,               "atari-falcon-digital-tracker", "Atari Falcon Digital Tracker",         "maktabasauti_dvs",    "dtm",    "dtm",   "",         0, 0u);
LEGACY(ym,     sauti_decoder_ym_ops,                "atari-ym2149-experimental",    "Atari YM2149 music (experimental)",    "maktabasauti_legacy", "ym",     "ym",    "",         0, MAKTABASAUTI_CODEC_FLAG_EXPERIMENTAL);
LEGACY(mp3,    sauti_decoder_mp3_ops,               "mp3-minimp3",                  "MPEG audio layer III",                 "maktabasauti_legacy", "mp3",    "mp3",   "",         0, 0u);

static const maktabasauti_codec_t *const legacy_ptrs[] = {
    &codec_wav, &codec_voc, &codec_aiff, &codec_aac, &codec_aacm, &codec_aacx, &codec_aache, &codec_aachc, &codec_aacmp4, &codec_aaclatm, &codec_aacadif,
    &codec_vorbis, &codec_midi, &codec_flac, &codec_avr, &codec_dvsm, &codec_dtm, &codec_ym, &codec_mp3
};

const maktabasauti_codec_t *const *mks_legacy_codecs(size_t *count) {
    if (count) *count = MKS_ARRAY_SIZE(legacy_ptrs);
    return legacy_ptrs;
}

/* rejestr domyslny: stala, bez zapisu w czasie dzialania */
const maktabasauti_registry_t mks_default_registry_object = {
    { NULL, NULL, NULL, NULL }, legacy_ptrs, MKS_ARRAY_SIZE(legacy_ptrs), MKS_ARRAY_SIZE(legacy_ptrs), 1
};
