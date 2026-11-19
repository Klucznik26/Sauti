/* MaktabaSauti: rejestr kodekow.  Rejestr domyslny jest stala zdefiniowana w mks_legacy.c (niezmienny, bezpieczny bez synchronizacji); wlasny rejestr to tablica wskaznikow na
 * opisy kodekow (opisy musza zyc dluzej niz rejestr). */
#include "mks_internal.h"
#include <string.h>

extern const maktabasauti_registry_t mks_default_registry_object;

const maktabasauti_registry_t *MAKTABASAUTI_CALL maktabasauti_default_registry(void) { return &mks_default_registry_object; }

maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_registry_create(const maktabasauti_allocator_t *allocator, maktabasauti_registry_t **out) {
    mks_alloc_t a;
    maktabasauti_registry_t *r;
    maktabasauti_result_t res;
    if (!out) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    *out = NULL;
    res = mks_alloc_init(&a, allocator);
    if (res != MAKTABASAUTI_OK) return res;
    r = (maktabasauti_registry_t *)mks_calloc(&a, sizeof *r);
    if (!r) return MAKTABASAUTI_E_NO_MEMORY;
    r->alloc = a;
    *out = r;
    return MAKTABASAUTI_OK;
}

static int codec_valid(const maktabasauti_codec_t *c) {
    return c && MKS_HAS_FIELD(c, maktabasauti_codec_t, close) && c->abi_version == MAKTABASAUTI_ABI_VERSION && c->name && c->name[0] && strlen(c->name) < 32u &&
           c->probe && c->open && c->read && c->close;
}

maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_registry_add(maktabasauti_registry_t *registry, const struct maktabasauti_codec *codec) {
    size_t i;
    if (!registry || registry->is_default) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if (!codec_valid(codec)) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    for (i = 0; i < registry->count; i++)
        if (strcmp(registry->codecs[i]->name, codec->name) == 0) return MAKTABASAUTI_E_INVALID_ARGUMENT;   /* nazwy sa jednoznaczne */
    if (registry->count == registry->cap) {
        size_t ncap = registry->cap ? registry->cap * 2u : 8u;
        const maktabasauti_codec_t **arr;
        if (ncap > ((size_t)-1) / sizeof *arr) return MAKTABASAUTI_E_NO_MEMORY;
        arr = (const maktabasauti_codec_t **)mks_alloc(&registry->alloc, ncap * sizeof *arr);
        if (!arr) return MAKTABASAUTI_E_NO_MEMORY;
        if (registry->count) memcpy((void *)arr, registry->codecs, registry->count * sizeof *arr);
        mks_free(&registry->alloc, (void *)registry->codecs);
        registry->codecs = (const maktabasauti_codec_t *const *)arr;
        registry->cap = ncap;
    }
    ((const maktabasauti_codec_t **)registry->codecs)[registry->count++] = codec;
    return MAKTABASAUTI_OK;
}

maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_registry_add_registry(maktabasauti_registry_t *registry, const maktabasauti_registry_t *other) {
    size_t i;
    if (!registry || !other) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    for (i = 0; i < other->count; i++) {
        maktabasauti_result_t r = maktabasauti_registry_add(registry, other->codecs[i]);
        if (r != MAKTABASAUTI_OK) return r;
    }
    return MAKTABASAUTI_OK;
}

void MAKTABASAUTI_CALL maktabasauti_registry_destroy(maktabasauti_registry_t *registry) {
    mks_alloc_t a;
    if (!registry || registry->is_default) return;
    a = registry->alloc;
    mks_free(&a, (void *)registry->codecs);
    mks_free(&a, registry);
}

size_t MAKTABASAUTI_CALL maktabasauti_registry_codec_count(const maktabasauti_registry_t *registry) { return registry ? registry->count : 0u; }

static void copy_text(char *dst, size_t cap, const char *src) {
    size_t n = src ? strlen(src) : 0u;
    if (n >= cap) n = cap - 1u;
    if (n) memcpy(dst, src, n);
    dst[n] = '\0';
}

maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_registry_codec_info(const maktabasauti_registry_t *registry, size_t index, maktabasauti_codec_info_t *out) {
    const maktabasauti_codec_t *c;
    uint32_t cap;
    maktabasauti_codec_info_t info;
    if (!registry || !out || out->struct_size < sizeof(uint32_t) * 2u) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if (index >= registry->count) return MAKTABASAUTI_E_OUT_OF_RANGE;
    c = registry->codecs[index];
    memset(&info, 0, sizeof info);
    info.struct_size = sizeof info;
    info.flags = MAKTABASAUTI_CODEC_CAN_DECODE | ((c->flags & MAKTABASAUTI_CODEC_FLAG_EXPERIMENTAL) ? MAKTABASAUTI_CODEC_EXPERIMENTAL : 0u);
    copy_text(info.name, sizeof info.name, c->name);
    copy_text(info.description, sizeof info.description, c->description);
    copy_text(info.module, sizeof info.module, c->module);
    cap = out->struct_size < sizeof info ? out->struct_size : (uint32_t)sizeof info;   /* zapisujemy tylko tyle, ile zmiesci struktura wywolujacego */
    {
        uint32_t keep = out->struct_size;
        memcpy(out, &info, cap);
        out->struct_size = keep;
    }
    return MAKTABASAUTI_OK;
}
