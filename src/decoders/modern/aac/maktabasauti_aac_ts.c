/* MaktabaSauti, modul maktabasauti_aac: AAC w strumieniu transportowym MPEG-2 (TS, takze M2TS z 4-bajtowym znacznikiem czasu i pakiety 204 B) dla wspolnego silnika.  Wejscie jest
 * czytane jeden raz, sekwencyjnie: PAT -> PMT -> pierwszy strumien AAC (stream_type 0x0F = ADTS), z pakietow PID tego strumienia sklejany jest elementarny strumien (ladunki PES bez
 * naglowkow PES), a z niego wycinane sa ramki ADTS (maktabasauti_aac_aulist.h).  Takze LATM / LOAS (stream_type 0x11, DVB) przez sauti_aac_latm_convert_element.  Nieobslugiwane (fail-closed): strumien zaszyfrowany (transport_scrambling_control), zmiana
 * konfiguracji LATM w srodku, program bez AAC.  Sekcje PAT / PMT moga zajmowac wiele pakietow; ciaglosc (continuity_counter) nie jest wymagana - brakujace pakiety daja ramki urwane, ktore silnik pomija. */
#include "maktabasauti_aac_aulist.h"
#include "sauti_aac_latm.h"

typedef struct { int pmt_pid, aac_pid, aac_type; uint8_t sec[4096]; size_t sec_len, sec_need; int sec_pid; } ts_st_t;

static int ts_packet_size(const uint8_t *p, size_t n, unsigned *size, unsigned *lead) {
    static const unsigned sizes[3] = { 188, 192, 204 };
    unsigned i, k;
    for (i = 0; i < 3u; i++) {
        const unsigned sz = sizes[i], ld = sz == 192u ? 4u : 0u;
        if (n < ld + 4u * sz + 1u) continue;
        for (k = 0; k < 4u; k++) if (p[ld + k * sz] != 0x47u) break;
        if (k == 4u) { *size = sz; *lead = ld; return 1; }
    }
    return 0;
}
/* section reassembly for PAT / PMT: returns 1 when a complete section is in st->sec */
static int sec_feed(ts_st_t *st, int pid, int pusi, const uint8_t *pl, size_t n) {
    if (pusi) {
        size_t ptr;
        if (!n) return 0;
        ptr = pl[0]; if (1u + ptr > n) return 0;
        pl += 1u + ptr; n -= 1u + ptr;
        st->sec_len = 0; st->sec_need = 0; st->sec_pid = pid;
    } else if (st->sec_pid != pid || !st->sec_len) return 0;
    if (st->sec_len + n > sizeof st->sec) { st->sec_len = 0; return 0; }
    memcpy(st->sec + st->sec_len, pl, n); st->sec_len += n;
    if (st->sec_len >= 3u && !st->sec_need) st->sec_need = 3u + ((((size_t)st->sec[1] & 0x0fu) << 8) | st->sec[2]);
    if (st->sec_need && st->sec_need <= sizeof st->sec && st->sec_len >= st->sec_need) return 1;
    return 0;
}
static maktabasauti_result_t ts_prepare(dec_t *d) {
    const maktabasauti_host_t *h = d->host; aulist_t *L; ts_st_t *st; uint8_t head[204 * 5 + 8], *pk; int64_t hn; unsigned psz, lead;
    size_t es_len = 0, at; maktabasauti_result_t res = MAKTABASAUTI_OK;
    uint8_t *es = NULL; size_t es_cap = 0; int pes_open = 0;
    hn = h->peek(h->user, d->rd, head, sizeof head);
    if (hn < 0) return (maktabasauti_result_t)hn;
    if (!ts_packet_size(head, (size_t)hn, &psz, &lead)) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    L = (aulist_t *)au_alloc(d, sizeof *L); st = (ts_st_t *)au_alloc(d, sizeof *st); pk = (uint8_t *)au_alloc(d, psz);
    if (!L || !st || !pk) { if (L) d->host->free(h->user, L); if (st) h->free(h->user, st); if (pk) h->free(h->user, pk); return MAKTABASAUTI_E_NO_MEMORY; }
    memset(L, 0, sizeof *L); memset(st, 0, sizeof *st); st->pmt_pid = -1; st->aac_pid = -1; st->aac_type = 0; L->adts = 1; d->cptr = L;
    d->seekable = 1;
    for (;;) {
        size_t got = 0; unsigned pid, afc, off; int pusi; const uint8_t *tp;
        while (got < psz) {
            int64_t m = h->read(h->user, d->rd, pk + got, psz - got);
            if (m < 0) { res = (maktabasauti_result_t)m; goto done; }
            if (m == 0) break;
            got += (size_t)m;
        }
        if (got < psz) break;                                                /* the end (a cut last packet is dropped) */
        tp = pk + lead;
        if (tp[0] != 0x47u) continue;                                        /* lost sync inside the stream: the packet is skipped */
        pusi = (tp[1] & 0x40u) != 0; pid = (((unsigned)tp[1] & 0x1fu) << 8) | tp[2]; afc = (tp[3] >> 4) & 3u;
        if (tp[3] >> 6) { if ((int)pid == st->aac_pid || pid == 0u || (int)pid == st->pmt_pid) { res = MAKTABASAUTI_E_UNSUPPORTED_FEATURE; goto done; } continue; }   /* scrambled */
        if (!(afc & 1u)) continue;                                           /* no payload */
        off = 4u;
        if (afc & 2u) { off += 1u + tp[4]; if (off > 188u) continue; }
        if (pid == 0u && st->pmt_pid < 0) {
            if (sec_feed(st, 0, pusi, tp + off, 188u - off) && st->sec[0] == 0) {
                size_t p0 = 8, end = st->sec_need > 4u ? st->sec_need - 4u : 0;
                while (p0 + 4u <= end) { const unsigned prog = ((unsigned)st->sec[p0] << 8) | st->sec[p0 + 1]; if (prog) { st->pmt_pid = (int)((((unsigned)st->sec[p0 + 2] & 0x1fu) << 8) | st->sec[p0 + 3]); break; } p0 += 4; }
                st->sec_len = 0;
            }
        } else if (st->pmt_pid >= 0 && (int)pid == st->pmt_pid && st->aac_pid < 0) {
            if (sec_feed(st, (int)pid, pusi, tp + off, 188u - off) && st->sec[0] == 2) {
                const size_t end = st->sec_need > 4u ? st->sec_need - 4u : 0; size_t p0 = 12u + ((((size_t)st->sec[10] & 0x0fu) << 8) | st->sec[11]);
                while (p0 + 5u <= end) {
                    const unsigned type = st->sec[p0], epid = (((unsigned)st->sec[p0 + 1] & 0x1fu) << 8) | st->sec[p0 + 2]; const size_t info = (((size_t)st->sec[p0 + 3] & 0x0fu) << 8) | st->sec[p0 + 4];
                    if (type == 0x0Fu || type == 0x11u) { st->aac_pid = (int)epid; st->aac_type = (int)type; break; }          /* ADTS / LATM (LOAS) */
                    p0 += 5u + info;
                }
                if (st->aac_pid >= 0) res = MAKTABASAUTI_OK;
                st->sec_len = 0;
            }
        } else if (st->aac_pid >= 0 && (int)pid == st->aac_pid) {
            const uint8_t *pl = tp + off; size_t n = 188u - off;
            if (pusi) {
                if (n >= 9u && pl[0] == 0 && pl[1] == 0 && pl[2] == 1) { const size_t skip = 9u + pl[8]; if (skip > n) continue; pl += skip; n -= skip; pes_open = 1; }
                else continue;
            } else if (!pes_open) continue;
            if (n) {
                if (es_len + n > es_cap) {
                    size_t cap = es_cap ? es_cap * 2u : 65536u; uint8_t *nb;
                    while (cap < es_len + n) cap *= 2u;
                    if (cap > AULIST_MAX_BYTES) { res = MAKTABASAUTI_E_UNSUPPORTED_FEATURE; goto done; }
                    nb = (uint8_t *)au_alloc(d, cap);
                    if (!nb) { res = MAKTABASAUTI_E_NO_MEMORY; goto done; }
                    if (es_len) memcpy(nb, es, es_len);
                    if (es) h->free(h->user, es);
                    es = nb; es_cap = cap;
                }
                memcpy(es + es_len, pl, n); es_len += n;
            }
        }
    }
    if (st->aac_type == 0x11) {                                              /* LOAS: AudioSyncStream elements -> the frames of the engine (as maktabasauti_aac_loas.c) */
        SautiAacLatmState ls; const size_t qcap = 8191u + 7u * 64u + 16u; uint8_t *q = (uint8_t *)au_alloc(d, qcap); uint16_t lens[64];
        if (!q) { res = MAKTABASAUTI_E_NO_MEMORY; goto done; }
        sauti_aac_latm_state_init(&ls);
        at = 0;
        while (at + 3u <= es_len) {
            size_t len; unsigned cnt = 0, k; int rc; size_t o = 0;
            if (es[at] != 0x56u || (es[at + 1] & 0xE0u) != 0xE0u) { at++; continue; }
            len = ((size_t)(es[at + 1] & 0x1Fu) << 8) | es[at + 2];
            if (!len || at + 3u + len > es_len) { at++; continue; }
            rc = sauti_aac_latm_convert_element(&ls, es + at + 3u, len, q, qcap, lens, 64u, &cnt);
            if (rc == -2) { if (L->count) { res = MAKTABASAUTI_E_UNSUPPORTED_FEATURE; break; } sauti_aac_latm_state_init(&ls); rc = sauti_aac_latm_convert_element(&ls, es + at + 3u, len, q, qcap, lens, 64u, &cnt); }
            if (rc == 1) for (k = 0; k < cnt; k++) {
                const size_t a = au_append(d, L, q + o, lens[k]);
                if (a == (size_t)-1 || !au_add(d, L, a, lens[k])) { res = MAKTABASAUTI_E_NO_MEMORY; break; }
                o += lens[k];
            }
            if (res != MAKTABASAUTI_OK) break;
            at += 3u + len;
        }
        h->free(h->user, q);
        if (res == MAKTABASAUTI_OK) {                                        /* USAC / ELD configuration from the StreamMuxConfig, as the LOAS container does */
            const uint8_t *asc; size_t an;
            d->usac_ready = 0; d->eld_ready = 0;
            if (sauti_aac_latm_usac_asc(&ls, &asc, &an) && sauti_usac_asc_parse(asc, an, &d->usac) == 1) { d->usac_ready = 1; d->usac_asc_len = an < sizeof d->usac_asc ? an : sizeof d->usac_asc; memcpy(d->usac_asc, asc, d->usac_asc_len); }
            if (sauti_aac_latm_eld_asc(&ls, &asc, &an) && sauti_aac_er_asc_parse(asc, an, &d->eld_asc) == 1 && d->eld_asc.ld_sbr) d->eld_ready = 1;
        }
    } else {                                                                 /* ADTS frames of the elementary stream */
    at = 0;
    while (at + 7u <= es_len) {
        aac_adts_hdr_t hd;
        if (!aac_adts_parse_header(es + at, es_len - at, &hd) || at + hd.frame_bytes > es_len) { at++; continue; }
        { const size_t o = au_append(d, L, es + at, hd.frame_bytes);
          if (o == (size_t)-1 || !au_add(d, L, o, hd.frame_bytes)) { res = MAKTABASAUTI_E_NO_MEMORY; goto done; } }
        at += hd.frame_bytes;
    }
    }
    if (res == MAKTABASAUTI_OK && (st->aac_pid < 0 || !L->count)) res = MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
done:
    if (es) h->free(h->user, es);
    h->free(h->user, pk); h->free(h->user, st);
    if (res != MAKTABASAUTI_OK) au_release_list(d);
    return res;
}
static void ts_release(dec_t *d) { au_release_list(d); }

const aac_container_t aac_ts_container = { "mpegts", au_fetch, au_resync, au_scan, NULL, NULL, ts_release, ts_prepare, au_seek_to, au_position, NULL, 1 };

/* sync bytes at 188 (192, 204) intervals */
int aac_ts_probe(const uint8_t *p, size_t n) { unsigned s, l; return p && n >= 4u * 188u + 1u && ts_packet_size(p, n, &s, &l); }
