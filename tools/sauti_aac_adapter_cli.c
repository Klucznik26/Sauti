/* Narzedzie testowe (nie produkt): otwiera plik ADTS natywnymi adapterami AAC Sauti (LC, HE mono, HE stereo) i zapisuje interleaved float32 LE do pliku.
 * Uzycie: sauti_aac_adapter_cli <plik.aac> <wyjscie.f32>   Wiersz wyniku na stdout:
 *   `ADAPTER <nazwa> rate=<Hz> channels=<n> frames=<ile odczytano>` albo `REJECT` (zaden adapter nie przyjal pliku).
 * Kod wyjscia 0 = adapter przyjal plik i dekodowanie doszlo do EOF, 1 = odrzucony, 2 = blad uzycia/wejscia. Zadnego PCM zastepczego: odrzucenie to odrzucenie. */
#include "sauti_decoder.h"
#include "sauti_decoder_aac_native.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    FILE *f;
    uint8_t *bytes;
    long end;
    SautiDecoder dec;
    const SautiDecoderOps *const registry[] = { &sauti_decoder_aac_native_ops, &sauti_decoder_aac_mono_native_ops, &sauti_decoder_aac_multi_native_ops, &sauti_decoder_aac_mp4_native_ops,&sauti_decoder_aac_latm_native_ops,&sauti_decoder_aac_adif_native_ops, &sauti_decoder_aac_he_native_ops, &sauti_decoder_aac_he_cpe_native_ops };
    float *pcm;
    size_t total = 0, got;
    FILE *out;
    if (argc != 3) return 2;
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    if (fseek(f, 0, SEEK_END) != 0 || (end = ftell(f)) <= 0 || end > 2000000000L || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 2; }
    bytes = (uint8_t *)malloc((size_t)end);
    if (!bytes || fread(bytes, 1, (size_t)end, f) != (size_t)end) { fclose(f); free(bytes); return 2; }
    fclose(f);
    if (!sauti_decoder_open(&dec, registry, sizeof registry / sizeof registry[0], bytes, (size_t)end)) { puts("REJECT"); free(bytes); return 1; }
    out = fopen(argv[2], "wb");
    if (!out) { sauti_decoder_close(&dec); free(bytes); return 2; }
    pcm = (float *)malloc(4096U * (size_t)(dec.info.channels ? dec.info.channels : 1U) * sizeof(float));
    if (!pcm) { fclose(out); sauti_decoder_close(&dec); free(bytes); return 2; }
    while ((got = sauti_decoder_read(&dec, pcm, 4096U)) > 0U) {
        fwrite(pcm, sizeof(float) * dec.info.channels, got, out);
        total += got;
    }
    printf("ADAPTER %s rate=%u channels=%u frames=%zu\n", dec.ops->name, dec.info.sample_rate, dec.info.channels, total);
    fclose(out);
    free(pcm);
    sauti_decoder_close(&dec);
    free(bytes);
    return 0;
}
