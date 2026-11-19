/* MaktabaSauti: rdzen BEZ opakowan starszych dekoderow (budowa przenosna: Windows, macOS, BSD, ARM64 bez NASM).  Rejestr domyslny jest pusty; kodeki dolacza sie przez
 * wlasny rejestr (maktabasauti_registry_create/add) albo modul statyczny.  Zamiast mks_legacy.c. */
#include "mks_internal.h"

const maktabasauti_codec_t *const *mks_legacy_codecs(size_t *count) {
    if (count) *count = 0u;
    return NULL;
}

const maktabasauti_registry_t mks_default_registry_object = { { NULL, NULL, NULL, NULL }, NULL, 0u, 0u, 1 };
