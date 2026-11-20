/* Cel 1: wszystkie sondy rejestru domyslnego na dowolnych danych (dokladny bufor: dane sa kopiowane do osobnej alokacji, wiec ASan widzi kazdy odczyt poza koniec). */
#include "fuzz_common.h"
#include "mks_internal.h"
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    size_t n, i; const maktabasauti_codec_t *const *c = mks_legacy_codecs(&n);
    uint8_t *copy = (uint8_t *)malloc(size ? size : 1);
    if (!copy) return 0;
    if (size) memcpy(copy, data, size);
    for (i = 0; i < n; i++) {
        int32_t s = c[i]->probe(c[i], copy, size, (i & 1) ? "x.bin" : NULL);
        if (s < 0 || s > 100) abort();
    }
    free(copy);
    return 0;
}
