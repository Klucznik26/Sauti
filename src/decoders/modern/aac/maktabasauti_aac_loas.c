/* MaktabaSauti, modul maktabasauti_aac: kontener LOAS/LATM (AudioSyncStream) dla wspolnego silnika strumieniowego (maktabasauti_aac_engine.h).  Kazdy element LOAS (3 bajty naglowka:
 * synchronizacja 0x2B7 + 13-bitowa dlugosc) jest pobierany w calosci (peek, potem read: WOULD_BLOCK nie zuzywa danych), konwertowany do jednej lub kilku ramek ADTS
 * (sauti_aac_latm_convert_element) i podawany silnikowi po jednej.  Konfiguracja strumienia (AudioSpecificConfig z pierwszego elementu) jest stala; powtarzana konfiguracja
 * (useSameStreamMux = 0) musi byc taka sama, zmiana = E_UNSUPPORTED_FEATURE.  Elementy przed pierwsza konfiguracja sa pomijane (jak w starym demuxerze). */
#include "maktabasauti_aac_engine.h"
#include "sauti_aac_latm.h"

#define LOAS_ELEM_MAX (3u + 8191u)
#define LOAS_Q_MAX (8191u + 7u * 64u)

typedef struct {
    SautiAacLatmState st;
    uint8_t elem[LOAS_ELEM_MAX + 8u];
    uint8_t q[LOAS_Q_MAX + 16u];
    uint16_t lens[64];
    unsigned n, next;
    size_t off;
} loas_priv_t;

static loas_priv_t *priv_of(dec_t *d) {
    if (!d->cptr) {
        loas_priv_t *p = (loas_priv_t *)d->host->alloc(d->host->user, sizeof *p);
        if (!p) return NULL;
        memset(p, 0, sizeof *p);
        sauti_aac_latm_state_init(&p->st);
        d->cptr = p;
    }
    return (loas_priv_t *)d->cptr;
}

static int loas_sync(const uint8_t *p) { return p[0] == 0x56u && (p[1] & 0xE0u) == 0xE0u; }
static size_t loas_len(const uint8_t *p) { return (size_t)(((p[1] & 0x1Fu) << 8) | p[2]); }

static int pop_frame(dec_t *d, loas_priv_t *p) {
    if (p->next >= p->n) return 0;
    d->frame_len = p->lens[p->next];
    memcpy(d->frame, p->q + p->off, d->frame_len);
    p->off += d->frame_len; p->next++;
    return 1;
}

static maktabasauti_result_t loas_fetch(dec_t *d) {
    const maktabasauti_host_t *h = d->host; loas_priv_t *p = priv_of(d);
    if (!p) return MAKTABASAUTI_E_NO_MEMORY;
    if (pop_frame(d, p)) return MAKTABASAUTI_OK;
    /* poczatek strumienia (po otwarciu, seek do poczatku, reset): konfiguracja wraca do stanu "nieznana", bo elementy przed pierwsza konfiguracja maja byc pominiete tak samo
     * jak przy pierwszym odczycie; w srodku strumienia konfiguracja jest juz znana (punkt kontrolny istnieje dopiero po pierwszej zdekodowanej ramce) */
    if (d->core.frames_in == 0 && d->core.in_offset == 0) sauti_aac_latm_state_init(&p->st);
    for (;;) {
        int64_t n, m; size_t len; int rc; unsigned cnt = 0;
        n = h->peek(h->user, d->rd, p->elem, 3u);
        if (n < 0) return (maktabasauti_result_t)n;
        if (n == 0) return MAKTABASAUTI_EOF;
        if (n < 3) return (p->elem[0] == 0x56u && (n < 2 || (p->elem[1] & 0xE0u) == 0xE0u)) ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_E_CORRUPT_DATA;
        if (!loas_sync(p->elem)) return MAKTABASAUTI_E_CORRUPT_DATA;
        len = loas_len(p->elem);
        if (!len) return MAKTABASAUTI_E_CORRUPT_DATA;
        m = h->peek(h->user, d->rd, p->elem, 3u + len);
        if (m < 0) return (maktabasauti_result_t)m;
        if ((uint64_t)m < 3u + len) return MAKTABASAUTI_E_TRUNCATED;
        m = h->read(h->user, d->rd, p->elem, 3u + len);
        if (m < 0) return (maktabasauti_result_t)m;
        if ((uint64_t)m != 3u + len) return MAKTABASAUTI_E_IO;
        rc = sauti_aac_latm_convert_element(&p->st, p->elem + 3, len, p->q, sizeof p->q, p->lens, 64u, &cnt);
        if (rc == 0) continue;                                            /* element bez danych audio (przed pierwsza konfiguracja): pomijamy */
        if (rc == -1) return MAKTABASAUTI_E_CORRUPT_DATA;
        if (rc == -2) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
        p->n = cnt; p->next = 0; p->off = 0;
        (void)pop_frame(d, p);
        return MAKTABASAUTI_OK;
    }
}

/* po uszkodzeniu: najblizszy bajt wygladajacy na element LOAS, ktorego dlugosc prowadzi do kolejnej synchronizacji (gdy widac ja w oknie) */
static maktabasauti_result_t loas_resync(dec_t *d) {
    const maktabasauti_host_t *h = d->host; size_t skipped = 0; int first = 1;
    while (skipped < ((size_t)1 << 20)) {
        uint8_t win[4096]; int64_t n; size_t i, adv;
        n = h->peek(h->user, d->rd, win, sizeof win);
        if (n < 0) return (maktabasauti_result_t)n;
        if (n < 6) { if (n > 0) (void)h->read(h->user, d->rd, win, (size_t)n); return MAKTABASAUTI_EOF; }
        for (i = first ? 1u : 0u; i + 3u <= (size_t)n; i++) {
            size_t len;
            if (!loas_sync(win + i)) continue;
            len = loas_len(win + i);
            if (!len) continue;
            if (i + 3u + len + 3u <= (size_t)n && !loas_sync(win + i + 3u + len)) continue;      /* widac nastepny element: musi miec synchronizacje */
            if (i) { int64_t rd = h->read(h->user, d->rd, win, i); if (rd < 0) return (maktabasauti_result_t)rd; }
            return MAKTABASAUTI_OK;
        }
        adv = (size_t)n > 3u ? (size_t)n - 3u : (size_t)n;
        { int64_t rd = h->read(h->user, d->rd, win, adv); if (rd < 0) return (maktabasauti_result_t)rd; skipped += (size_t)rd; }
        first = 0;
    }
    return MAKTABASAUTI_EOF;
}

static maktabasauti_result_t loas_scan(dec_t *d, int64_t resume, uint64_t *count, int *consistent) {
    const maktabasauti_host_t *h = d->host; loas_priv_t *p = priv_of(d); SautiAacLatmState tmp; uint64_t c;
    if (!p) return MAKTABASAUTI_E_NO_MEMORY;
    tmp = p->st; c = 1u + (p->n - p->next);                              /* pierwsza ramka i reszta podramek pierwszego elementu */
    *consistent = 1;
    for (;;) {
        uint8_t hdr[3]; int64_t n, m; size_t len; int rc; unsigned cnt = 0; uint8_t *q = p->q + 0; (void)q;
        n = h->peek(h->user, d->rd, hdr, 3u);
        if (n < 0) return (maktabasauti_result_t)n;
        if (n < 3 || !loas_sync(hdr)) break;
        len = loas_len(hdr);
        if (!len) break;
        m = h->peek(h->user, d->rd, p->elem, 3u + len);
        if (m < 0) return (maktabasauti_result_t)m;
        if ((uint64_t)m < 3u + len) break;                               /* urwany element: nie liczymy */
        m = h->read(h->user, d->rd, p->elem, 3u + len);
        if (m < 0) return (maktabasauti_result_t)m;
        {   uint8_t out[LOAS_Q_MAX + 16u]; uint16_t lens[64];
            rc = sauti_aac_latm_convert_element(&tmp, p->elem + 3, len, out, sizeof out, lens, 64u, &cnt); }
        if (rc == 1) c += cnt;
        else if (rc == -2) { *consistent = 0; break; }
        else if (rc == -1) break;
    }
    if (h->seek(h->user, d->rd, resume, 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;
    *count = c;
    return MAKTABASAUTI_OK;
}

static int loas_boundary(const dec_t *d) { const loas_priv_t *p = (const loas_priv_t *)d->cptr; return !p || p->next >= p->n; }
static void loas_discard(dec_t *d) { loas_priv_t *p = (loas_priv_t *)d->cptr; if (p) { p->n = p->next = 0; p->off = 0; } }
static void loas_release(dec_t *d) { if (d->cptr) { d->host->free(d->host->user, d->cptr); d->cptr = NULL; } }

static const aac_container_t loas_container = { "loas", loas_fetch, loas_resync, loas_scan, loas_boundary, loas_discard, loas_release, NULL, NULL, NULL, NULL };

static int32_t MAKTABASAUTI_CALL loas_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    (void)self; (void)hint;
    return (p && sauti_aac_latm_probe(p, n)) ? 85 : 0;
}
static maktabasauti_result_t MAKTABASAUTI_CALL loas_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *host, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    return aac_eng_open(self, &loas_container, host, rd, info, state);
}

const maktabasauti_codec_t maktabasauti_aac_loas_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "aac-loas", "AAC in LOAS/LATM (LC, HE-AAC v1/v2), streaming", "maktabasauti_aac",
    MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0u, NULL,
    loas_probe, loas_open, aac_eng_read, aac_eng_seek, aac_eng_reset, aac_eng_flush, NULL, aac_eng_last_error, NULL, aac_eng_close, { NULL }, { NULL }
};
const maktabasauti_codec_t *maktabasauti_aac_loas_codec(void) { return &maktabasauti_aac_loas_desc; }
