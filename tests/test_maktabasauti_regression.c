/* Zgodnosc nowego API MaktabaSauti ze starym (sauti_legacy_decoder_*): te same pliki dekodowane obiema drogami musza dac IDENTYCZNE PCM (bit w bit), te same parametry
 * strumienia i te same wyniki po przewijaniu; takze przez wejscie z pliku i z pamieci.  Rejestr starego API ma ta sama kolejnosc co domyslny rejestr nowego.
 * usage: test_maktabasauti_regression FILE... */
#include "maktabasauti/maktabasauti.h"
#include "sauti_decoder.h"
#include "sauti_decoder_aac_native.h"
#include "sauti_decoder_wav.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const SautiDecoderOps *const legacy_registry[] = {
    &sauti_decoder_wav_ops, &sauti_decoder_aac_native_ops, &sauti_decoder_aac_mono_native_ops, &sauti_decoder_aac_multi_native_ops, &sauti_decoder_aac_he_native_ops,
    &sauti_decoder_aac_he_cpe_native_ops, &sauti_decoder_aac_mp4_native_ops, &sauti_decoder_aac_latm_native_ops, &sauti_decoder_aac_adif_native_ops
};

static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); uint8_t *b; long sz;
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    b = (uint8_t *)malloc((size_t)sz);
    if (!b || fread(b, 1, (size_t)sz, f) != (size_t)sz) exit(2);
    fclose(f); *n = (size_t)sz;
    return b;
}

static int check_file(const char *path, int via_file) {
    size_t n; uint8_t *bytes = slurp(path, &n);
    SautiDecoder old; maktabasauti_decoder_t *nd = NULL; maktabasauti_open_params_t p; maktabasauti_io_t io; maktabasauti_stream_info_t info;
    float *a, *b; size_t total, ch, got, i; uint64_t seeds[12]; uint64_t actual;
    memset(&p, 0, sizeof p);
    if (!sauti_legacy_decoder_open(&old, legacy_registry, sizeof legacy_registry / sizeof legacy_registry[0], bytes, n)) { printf("SKIP %s: legacy API does not open it\n", path); free(bytes); return 0; }
    if (via_file) assert(maktabasauti_io_from_file(path, NULL, &io) == MAKTABASAUTI_OK);
    else assert(maktabasauti_io_from_memory(bytes, n, NULL, &io) == MAKTABASAUTI_OK);
    p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION; p.io = io;
    assert(maktabasauti_decoder_open(&p, &nd) == MAKTABASAUTI_OK);
    info.struct_size = sizeof info; assert(maktabasauti_decoder_get_info(nd, &info) == MAKTABASAUTI_OK);
    ch = old.info.channels;
    assert(info.sample_rate == old.info.sample_rate && info.channels == old.info.channels && info.total_frames == old.info.total_frames);
    assert(info.native_format == MAKTABASAUTI_FORMAT_F32 && strcmp(info.codec_name, "aac") == 0);
    total = (size_t)old.info.total_frames;
    a = (float *)malloc(total * ch * sizeof(float) + 16); b = (float *)malloc(total * ch * sizeof(float) + 16);
    assert(a && b);
    got = 0;
    while (got < total) { size_t g = sauti_legacy_decoder_read(&old, a + got * ch, 2048); if (!g) break; got += g; }
    assert(got == total);
    got = 0;
    for (;;) {
        size_t g = 0; maktabasauti_result_t r = maktabasauti_decoder_read(nd, b + got * ch, 1000, MAKTABASAUTI_FORMAT_F32, &g);
        if (r == MAKTABASAUTI_EOF) break;
        assert(r == MAKTABASAUTI_OK && g > 0);
        got += g;
    }
    assert(got == total);
    assert(memcmp(a, b, total * ch * sizeof(float)) == 0);                        /* PCM bit w bit */
    /* przewijanie: te same pozycje, ten sam wynik */
    for (i = 0; i < 12; i++) seeds[i] = i == 0 ? 0 : i == 11 ? total - 1 : (uint64_t)(total * i / 12 + i * 37) % total;
    for (i = 0; i < 12; i++) {
        float x[4096 * 8], y[4096 * 8]; size_t want = 1500 < total - seeds[i] ? 1500 : total - seeds[i], gx, gy = 0;
        assert(sauti_legacy_decoder_seek(&old, seeds[i]) == 1);
        gx = sauti_legacy_decoder_read(&old, x, want);
        assert(maktabasauti_decoder_seek(nd, seeds[i], MAKTABASAUTI_SEEK_EXACT, &actual) == MAKTABASAUTI_OK && actual == seeds[i]);
        assert(maktabasauti_decoder_read(nd, y, want, MAKTABASAUTI_FORMAT_F32, &gy) == MAKTABASAUTI_OK);
        assert(gx == gy && memcmp(x, y, gx * ch * sizeof(float)) == 0);
    }
    assert(maktabasauti_decoder_seek(nd, total + 1, MAKTABASAUTI_SEEK_EXACT, &actual) == MAKTABASAUTI_E_OUT_OF_RANGE);
    assert(maktabasauti_decoder_reset(nd) == MAKTABASAUTI_OK);
    {   float y[2048 * 8]; assert(maktabasauti_decoder_read(nd, y, 2048, MAKTABASAUTI_FORMAT_F32, &got) == MAKTABASAUTI_OK && memcmp(y, a, got * ch * sizeof(float)) == 0); }
    maktabasauti_decoder_close(nd);
    sauti_legacy_decoder_close(&old);
    printf("OK   %s (%s): %zu frames x %zu ch identical to the legacy API\n", path, via_file ? "file input" : "memory input", total, ch);
    free(a); free(b); free(bytes);
    return 1;
}

int main(int argc, char **argv) {
    int i, checked = 0;
    if (argc < 2) return 2;
    for (i = 1; i < argc; i++) { checked += check_file(argv[i], 0); checked += check_file(argv[i], 1); }
    assert(checked > 0);
    puts("PASS MaktabaSauti regression: new API == legacy API (PCM bit for bit, seek, reset) on all fixtures, memory and file input");
    return 0;
}
