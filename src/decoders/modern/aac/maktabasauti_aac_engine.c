#include "maktabasauti_aac_engine.h"
#include <stdlib.h>

/* 1 = naglowek poprawny (profil LC, indeks czestotliwosci 0..12, konfiguracja kanalow 0..7, dlugosc >= naglowek), 0 = nie.  Naglowek wewnetrzny AAC-LD / ELD (layer = 01, patrz aac_er_wrap) tez jest
 * przyjmowany i oznaczony hd->er. */
int aac_adts_parse_header(const uint8_t *p, size_t n, aac_adts_hdr_t *h) {
    unsigned prot_absent, sfi, cfg, fl, layer;
    if (n < 7u || p[0] != 0xFFu || (p[1] & 0xF0u) != 0xF0u) return 0;
    layer = (unsigned)(p[1] >> 1) & 3u;
    if (layer > 1u) return 0;
    prot_absent = p[1] & 1u;
    if (!prot_absent && n < 9u) return 0;
    h->er = layer; h->er_aot = 0u; h->er_480 = 0u;
    if (layer) {
        if (!prot_absent) return 0;                                      /* naglowek wewnetrzny nigdy nie ma CRC */
        h->er_aot = (p[2] >> 6) == 1u ? 39u : ((p[2] >> 6) == 0u ? 23u : ((p[2] >> 6) == 2u ? 2u : 1u));          /* 2 = AAC-LC z ramka 960, 3 = AAC Main (z bitem prywatnym: AAC LTP) */
        h->er_480 = (unsigned)(p[2] >> 1) & 1u;
        if (((p[6] >> 2) & 0x3fu) == 0x10u) { h->er_aot = 17u; h->er_480 = (unsigned)(p[3] >> 4) & 1u; }               /* ER AAC LC (kod w polu zapelnienia bufora, 960 linii: bit `home`) */
        else if (((p[6] >> 2) & 0x3fu) == 0x11u) { h->er_aot = 19u; h->er_480 = 0u; }                              /* ER AAC LTP */
        else if (((p[6] >> 2) & 0x3fu) == 0x12u) { h->er_aot = 42u; h->er_480 = 0u; }                              /* USAC (xHE-AAC): konfiguracja jest w dec_t, ramka niesie tylko jednostke dostepu */
        else if (h->er_aot == 1u && h->er_480) { h->er_aot = 4u; h->er_480 = 0u; }
        else if (h->er_aot == 1u) h->er_480 = (unsigned)(p[3] >> 4) & 1u;                                           /* Main: bit `home` = 960 linii */
    } else if ((p[2] >> 6) == 3u) { h->er = 1u; h->er_aot = 4u; h->er_480 = 0u; }                              /* profil 3 = AAC LTP */
    else if ((p[2] >> 6) == 0u) { h->er = 1u; h->er_aot = 1u; h->er_480 = 0u; }    /* profil 0 = AAC Main: ta sama sciezka co ramki z naglowkiem wewnetrznym (dekoder sauti_aac_lcn) */
    else if ((p[2] >> 6) != 1u) return 0;                                /* profil 1 = AAC-LC */
    sfi = (unsigned)(p[2] >> 2) & 15u;
    if (sfi > 12u) return 0;
    cfg = (unsigned)(((p[2] & 1u) << 2) | (p[3] >> 6));
    fl = (unsigned)(((p[3] & 3u) << 11) | (p[4] << 3) | (p[5] >> 5));
    if (fl < (prot_absent ? 7u : 9u)) return 0;
    h->rate_index = sfi; h->rate = aac_adts_rates[sfi]; h->config = cfg; h->frame_bytes = fl;
    h->header_bytes = prot_absent ? 7u : 9u; h->raw_blocks = (unsigned)(p[6] & 3u) + 1u; h->crc_present = prot_absent ? 0u : 1u;
    return 1;
}

int aac_same_stream(const dec_t *d, const aac_adts_hdr_t *h) {
    return h->rate_index == d->rate_index && h->config == d->config && h->raw_blocks == 1u && h->crc_present == d->crc_present &&
           (h->er ? (d->variant == V_ER && h->er_aot == d->er_aot && h->er_480 == d->er_480) : d->variant != V_ER);
}

/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
/* pobieranie ramki: peek (kompletna ramka albo nic), potem read.  Zwraca OK (d->frame, d->frame_len), EOF (czysty koniec na granicy ramki), E_TRUNCATED (urwana ramka),
 * E_CORRUPT_DATA (nie ma poprawnego naglowka w tym miejscu), albo blad wejscia (E_WOULD_BLOCK, E_IO...) -- w tych przypadkach nic nie zostalo zuzyte. */

static maktabasauti_result_t eng_fetch(dec_t *d) {
    maktabasauti_result_t r;
    if (d->have_frame) return MAKTABASAUTI_OK;
    r = d->cont->fetch(d);
    if (r == MAKTABASAUTI_OK) d->have_frame = 1;
    return r;
}

/* the decoders of sauti_aac_lcn / sauti_aac_lcm serve AAC-LC with 960 lines (2), Main (1), LTP (4), ER AAC LC (17) and ER AAC LTP (19) */
static int aot_is_lcn(unsigned aot) { return aot == 1u || aot == 2u || aot == 4u || aot == 17u || aot == 19u; }
static void lcn_config(SautiAacLcnConfig *lc, unsigned aot, unsigned rate_index, unsigned config, unsigned f960) {
    memset(lc, 0, sizeof *lc);
    lc->rate_index = rate_index; lc->channel_config = config;
    lc->main = aot == 1u; lc->ltp = aot == 4u || aot == 19u; lc->er = aot == 17u || aot == 19u;
    lc->frame960 = (aot == 1u || aot == 17u) && f960;
}
/* AAC Main / LTP wielokanalowy: stan na stercie hosta; kopia gleboka dla punktow kontrolnych i stanu poczatkowego */
static void lcm_free(dec_t *d, core_t *c) { if (c->lcm) d->host->free(d->host->user, c->lcm); c->lcm = NULL; if (c->usac_hs) d->host->free(d->host->user, c->usac_hs); c->usac_hs = NULL; }
static SautiUsacState *usac_dup(dec_t *d, const SautiUsacState *src) {
    SautiUsacState *n;
    if (!src) return NULL;
    n = (SautiUsacState *)d->host->alloc(d->host->user, sizeof *n);
    if (n) memcpy(n, src, sizeof *n);
    return n;
}
static SautiAacLcmState *lcm_dup(dec_t *d, const SautiAacLcmState *src) {
    SautiAacLcmState *n;
    if (!src) return NULL;
    n = (SautiAacLcmState *)d->host->alloc(d->host->user, sizeof *n);
    if (n) memcpy(n, src, sizeof *n);
    return n;
}
static int host_opt_is(const maktabasauti_host_t *h, const char *key, const char *val) { const char *v = h->option ? h->option(h->user, key) : NULL; return v && strcmp(v, val) == 0; }
static void core_reset(dec_t *d) {
    lcm_free(d, &d->core);
    memset(&d->core, 0, sizeof d->core);
    switch (d->variant) {
    case V_LC_STEREO: (void)sauti_aac_lc_synthesis_init(&d->core.u.lc[0], 0); (void)sauti_aac_lc_synthesis_init(&d->core.u.lc[1], 0); break;
    case V_LC_MONO: (void)sauti_aac_lc_synthesis_init(&d->core.u.lc[0], 0); break;
    case V_LC_MULTI:
        if (d->config == 0u) (void)sauti_aac_lc_multi_init_layout(&d->core.u.multi, &d->layout, 0);
        else (void)sauti_aac_lc_multi_init(&d->core.u.multi, d->config, 0);
        break;
    case V_ER:
        if (d->er_aot == 42u) {
            d->core.usac_hs = (SautiUsacState *)d->host->alloc(d->host->user, sizeof *d->core.usac_hs);
            if (d->core.usac_hs) sauti_usac_state_init(d->core.usac_hs, &d->usac);
        }
        else if (aot_is_lcn(d->er_aot) && d->config > 2u) {                         /* Main / LTP / ER wielokanalowy */
            SautiAacLcnConfig lc; lcn_config(&lc, d->er_aot, d->rate_index, d->config, d->er_480);
            d->core.lcm = (SautiAacLcmState *)d->host->alloc(d->host->user, sizeof *d->core.lcm);
            if (d->core.lcm) sauti_aac_lcm_state_init(d->core.lcm, &lc);
        } else if (d->er_aot == 39u && d->eldsbr) {
            eldsbr_t *e = &d->core.u.eld;
            const unsigned slots = d->er_480 ? 15u : 16u;
            sauti_aac_er_state_init(&e->er);
            e->pl.time_slots = slots; e->pl.crc = d->eld_asc.ld_sbr_crc; e->pl.have_header = 1u; e->pl.header = d->eld_asc.ld_header;
            (void)sauti_aac_sbr_channel_init(&e->channel[0]); (void)sauti_aac_sbr_channel_init(&e->channel[1]);
            e->sac_on = d->eld_asc.ld_sac_len != 0u;                                      /* ELD v2: MPS 212 niskiego opoznienia + SBR na banku QMF MPS-LD */
            if (e->sac_on && sauti_aac_eld_sac_config_parse(d->eld_asc.ld_sac, d->eld_asc.ld_sac_len, &e->sacc) != 1) e->sac_on = 0u;
            memset(&e->ms, 0, sizeof e->ms); memset(&e->sac, 0, sizeof e->sac);
            e->channel[0].ld = e->channel[1].ld = d->eldsbr; e->channel[0].ld_mps = e->channel[1].ld_mps = e->sac_on; e->channel[0].frame_slots = e->channel[1].frame_slots = slots;
        } else if (aot_is_lcn(d->er_aot) && d->he960) {
            he960_t *h = &d->core.u.h9;
            sauti_aac_lcn_state_init(&h->lcn);
            h->mono.time_slots = 15u; h->cpe.time_slots = 15u;
            (void)sauti_aac_sbr_channel_init(&h->channel[0]); (void)sauti_aac_sbr_channel_init(&h->channel[1]);
            h->channel[0].frame_slots = 30u; h->channel[1].frame_slots = 30u;
        } else if (aot_is_lcn(d->er_aot)) sauti_aac_lcn_state_init(&d->core.u.lcn);
        else sauti_aac_er_state_init(&d->core.u.er);
        break;
    case V_HE_MONO: case V_HE_CPE: {
        he_state_t *s = &d->core.u.he;
        memset(s, 0, sizeof *s);
        (void)sauti_aac_he_adts_mono_init(&s->mono); (void)sauti_aac_he_adts_cpe_init(&s->cpe);
        (void)sauti_aac_sbr_channel_init(&s->channel[0]); (void)sauti_aac_sbr_channel_init(&s->channel[1]);
        break; }
    }
}

static int all_finite(const float *p, size_t n) { size_t i; for (i = 0; i < n; i++) if (!isfinite(p[i])) return 0; return 1; }

/* ---- produkcja jednego bloku PCM do d->cache (z ramki d->frame) -------------------------------------------------------------------------------------------------- */
typedef enum { P_OK = 0, P_NEED_MORE, P_FAIL, P_UNSUPPORTED } prod_t;

static prod_t decode_lc(dec_t *d) {
    SautiAacReason reason = SAUTI_AAC_REASON_OK; int r;
    switch (d->variant) {
    case V_LC_STEREO: r = sauti_aac_lc_adts_stereo_decode_ex(d->frame, d->frame_len, d->core.u.lc, d->cache, &reason); if (r == 1 && !all_finite(d->cache, 2048u)) r = 0; break;
    case V_LC_MONO: r = sauti_aac_lc_adts_mono_decode_ex(d->frame, d->frame_len, &d->core.u.lc[0], d->cache, &reason); if (r == 1 && !all_finite(d->cache, 1024u)) r = 0; break;
    default:
        r = sauti_aac_lc_adts_multi_decode_ex(d->frame, d->frame_len, &d->core.u.multi, d->cache, &reason);
        if (r == 1 && !all_finite(d->cache, (size_t)1024u * d->out_channels)) r = 0;
        break;
    }
    return r == 1 ? P_OK : (r < 0 ? P_UNSUPPORTED : P_FAIL);
}

/* HE: render PS dla czekajacej ramki; `lookahead` = siatka nastepnej ramki albo NULL na koncu strumienia (cisza) */
static int he_render_ps(dec_t *d, const SautiAacSbrComplex *lookahead, float *pcm_stereo) {
    he_state_t *s = &d->core.u.he;
    SautiAacSbrComplex *qmf38 = d->scratch_qmf38;
    memcpy(qmf38, s->pending, sizeof(s->pending));
    if (lookahead) memcpy(qmf38 + 32u * 64u, lookahead, 6u * 64u * sizeof(qmf38[0]));
    else memset(qmf38 + 32u * 64u, 0, 6u * 64u * sizeof(qmf38[0]));
    return sauti_aac_he_ps_bridge_render(&s->pending_payload, qmf38, 38u * 64u, &s->ps, pcm_stereo, 4096u);
}

/* ramka bez danych PS: te same parametry co poprzednia ramka, plasko na cala ramke (jak w starym adapterze) */
static void he_ps_hold(const SautiAacPsFrame *prev, SautiAacPsFrame *out, unsigned slots) {
    const unsigned last = prev->resolved_num_env ? prev->resolved_num_env - 1u : 0u;
    memset(out, 0, sizeof(*out));
    out->header = prev->header;
    out->resolved_num_env = 1u; out->border_position[0] = (uint8_t)(slots - 1u); out->slots = slots;
    out->iid_count = prev->iid_count; out->icc_count = prev->icc_count;
    memcpy(out->iid[0], prev->iid[last], sizeof(out->iid[0]));
    memcpy(out->icc[0], prev->icc[last], sizeof(out->icc[0]));
}

/* dekoduje d->frame stanem HE; P_OK = blok w cache, P_NEED_MORE = ramka zuzyta, blok bedzie po nastepnej (PS), P_FAIL / P_UNSUPPORTED */
/* AAC-LD / AAC-ELD: jednostka dostepu za 7-bajtowym naglowkiem wewnetrznym */
static prod_t decode_usac(dec_t *d) {
    SautiAacReason reason = SAUTI_AAC_REASON_OK; int r;
    if (!d->usac_ready || d->frame_len < 7u || !d->core.usac_hs) return P_FAIL;
    r = sauti_usac_decode(&d->usac, d->core.usac_hs, d->frame + 7u, d->frame_len - 7u, d->cache, &reason);
    if (r == 1 && d->drc_plan && d->drc_plan->ok) (void)sauti_unidrc_process(d->drc_plan, &d->core.usac_hs->drc, &d->usac, d->frame + 7u, d->frame_len - 7u, d->core.usac_hs->drc_pos, d->core.usac_hs->drc_len, d->cache);
    if (r == 1 && !all_finite(d->cache, (size_t)d->frame_pcm * d->out_channels)) r = 0;
    return r == 1 ? P_OK : (r < 0 ? P_UNSUPPORTED : P_FAIL);
}
/* HE-AAC v2 z ramka 960: render PS ramki czekajacej (30 kolumn QMF + 6 szczelin wyprzedzenia z nastepnej ramki albo cisza na koncu strumienia); wyjscie 1920 probek na kanal */
static int he960_render_ps(dec_t *d, const SautiAacSbrComplex *lookahead, float *pcm_stereo) {
    he960_t *s = &d->core.u.h9;
    SautiAacSbrComplex *qmf38 = d->scratch_qmf38;
    float st[4096];
    memset(qmf38, 0, 38u * 64u * sizeof(qmf38[0]));
    memcpy(qmf38, s->pending, 30u * 64u * sizeof(qmf38[0]));
    if (lookahead) memcpy(qmf38 + 30u * 64u, lookahead, 6u * 64u * sizeof(qmf38[0]));
    if (!sauti_aac_he_ps_bridge_render(&s->pending_payload, qmf38, 38u * 64u, &s->ps, st, 4096u)) return 0;
    memcpy(pcm_stereo, st, 3840u * sizeof(float));
    return 1;
}
/* HE-AAC z ramka 960 (mono / stereo; mono z PS = HE-AAC v2): rdzen LC 960, ladunek SBR z pola wypelnienia (15 szczelin czasowych), 30 szczelin QMF, 1920 probek na kanal */
static prod_t decode_he960(dec_t *d) {
    he960_t *s = &d->core.u.h9;
    SautiAacLcnConfig lc; SautiAacLcnSbr sb; SautiAacReason why = SAUTI_AAC_REASON_OK;
    float core[2u * 1024u], pcm[2][1920]; unsigned i, k, ch = d->config; int r;
    const uint8_t *raw = d->frame + 7u; const size_t rawn = d->frame_len - 7u;
    SautiAacSbrComplex *grid = d->scratch_grid;
    lcn_config(&lc, 2u, d->rate_index, d->config, 1u);
    r = sauti_aac_lcn_decode_sbr(&lc, &s->lcn, raw, rawn, core, &why, &sb);
    if (r != 1) return r < 0 ? P_UNSUPPORTED : P_FAIL;
    if (!sb.present || (ch != 1u && ch != 2u)) return P_FAIL;
    if (ch == 1u) {
        SautiAacSbrPayloadState ns = s->mono; SautiAacSbrPayloadFrame pl;
        ns.ps.slots = 30u;                                                             /* PS: 30 kolumn QMF na ramke 960 */
        if (!sauti_aac_sbr_payload_temporal_parse_ex(raw, rawn, sb.start_bit, sb.bits, d->out_rate, &ns, &pl, NULL)) return P_FAIL;
        if (pl.has_ps && !d->ps_mode) { d->ps_mode = 1; d->out_channels = 2u; }       /* PS wykryte w pierwszej ramce: wyjscie stereo */
        if (d->ps_mode) {
            if (!pl.has_ps) {
                if (!s->pending_payload.has_ps) return P_FAIL;                         /* nie ma czego powtorzyc */
                he_ps_hold(&s->pending_payload.ps, &pl.ps, 30u); pl.has_ps = 1u;
            }
            if (!sauti_aac_sbr_channel_frame(&s->channel[0], core, &pl, grid)) return P_FAIL;
            s->mono = ns;
            if (s->pending_valid) {
                if (!he960_render_ps(d, grid, d->cache)) return P_FAIL;
                memcpy(s->pending, grid, sizeof(s->pending));
                s->pending_payload = pl;
                return all_finite(d->cache, 3840u) ? P_OK : P_FAIL;
            }
            memcpy(s->pending, grid, sizeof(s->pending));
            s->pending_payload = pl; s->pending_valid = 1u;
            return P_NEED_MORE;
        }
        if (!sauti_aac_sbr_channel_frame(&s->channel[0], core, &pl, grid) || !sauti_aac_sbr_channel_synthesize(&s->channel[0], grid, d->cache)) return P_FAIL;
        s->mono = ns;
        return all_finite(d->cache, 1920u) ? P_OK : P_FAIL;
    } else {
        SautiAacSbrPayloadCpeState ns = s->cpe; SautiAacSbrPayloadCpeFrame pl; float c[2][1024];
        SautiAacSbrComplex (*g2)[SAUTI_AAC_SBR_CHANNEL_GRID] = d->scratch_g2;
        if (!sauti_aac_sbr_payload_cpe_parse(raw, rawn, sb.start_bit, sb.bits, d->out_rate, &ns, &pl)) return P_FAIL;
        for (i = 0; i < 960u; i++) { c[0][i] = core[2u * i]; c[1][i] = core[2u * i + 1u]; }
        for (k = 0; k < 2u; k++)
            if (!sauti_aac_sbr_channel_frame(&s->channel[k], c[k], &pl.channel[k], g2[k]) || !sauti_aac_sbr_channel_synthesize(&s->channel[k], g2[k], pcm[k])) return P_FAIL;
        s->cpe = ns;
        for (i = 0; i < 1920u; i++) { d->cache[2u * i] = pcm[0][i]; d->cache[2u * i + 1u] = pcm[1][i]; }
        return all_finite(d->cache, 3840u) ? P_OK : P_FAIL;
    }
}
/* AAC-ELD z niskoopoznieniowym SBR: rdzen ELD, potem ladunek SBR z reszty jednostki dostepu, CLDFB; jednokrotny (eldsbr 1): wyjscie n probek na kanal w czestotliwosci rdzenia, dwukrotny (eldsbr 2): 2n probek, synteza 64-pasmowa */
static prod_t decode_eldsbr(dec_t *d) {
    eldsbr_t *s = &d->core.u.eld;
    SautiAacErConfig c; SautiAacReason why = SAUTI_AAC_REASON_OK; SautiAacSbrPayloadLdState ns = s->pl; SautiAacSbrPayloadFrame fr[2];
    float core[2u * 512u], cc[1024], pcm[1024]; size_t end = 0; unsigned cpl = 0, ch = d->config, n = d->er_480 ? 480u : 512u, no = n * d->eldsbr, i, k; int r;
    const uint8_t *raw = d->frame + 7u; const size_t rawn = d->frame_len - 7u;
    SautiAacSbrComplex *grid = d->scratch_grid;
    c.aot = 39u; c.rate_index = d->rate_index; c.frame480 = d->er_480; c.channel_config = ch;
    if (ch != 1u && ch != 2u) return P_UNSUPPORTED;
    r = sauti_aac_er_decode_ex(&c, &s->er, raw, rawn, core, &why, &end);
    if (r != 1) return r < 0 ? P_UNSUPPORTED : P_FAIL;
    if (!sauti_aac_sbr_payload_ld_parse(raw, rawn, end, rawn * 8u, 2u * d->core_rate, ch, &ns, fr, &cpl)   /* the frequency tables are those of the SBR processing rate = twice the core rate, at the single rate and at the dual rate (FDK: sbrProcSmplRate) */) return P_FAIL;
    if (s->sac_on) {                                                                  /* ELD v2: siatka QMF mono -> MPS 212 -> dwie siatki -> dwie syntezy */
        SautiUsacMpsState ms = s->ms; SautiAacEldSacDec sd = s->sac; SautiUsacMpsFrame mf; static SautiAacSbrComplex g0[64u * 16u], g1[64u * 16u];
        size_t fe = 0; const size_t sbr_end = end + fr[0].payload_data_bits;
        if (ch != 1u || d->eldsbr != 2u) return P_UNSUPPORTED;
        if (!sauti_aac_eld_sac_frame_parse(&s->sacc, raw, sbr_end, rawn * 8u, &ms, &mf, &fe)) return P_FAIL;
        for (i = 0; i < n; i++) cc[i] = core[i];
        if (!sauti_aac_sbr_channel_frame(&s->channel[0], cc, &fr[0], grid)) return P_FAIL;
        if (!sauti_aac_eld_sac_apply(&sd, &s->sacc, &mf, s->sacc.time_slots, grid, g0, g1)) return P_FAIL;
        for (k = 0; k < 2u; k++) {
            if (!sauti_aac_sbr_channel_synthesize(&s->channel[k], k ? g1 : g0, pcm)) return P_FAIL;
            for (i = 0; i < no; i++) d->cache[2u * i + k] = pcm[i];
        }
        s->pl = ns; s->ms = ms; s->sac = sd;
        return all_finite(d->cache, (size_t)no * 2u) ? P_OK : P_FAIL;
    }
    for (k = 0; k < ch; k++) {
        for (i = 0; i < n; i++) cc[i] = core[ch * i + k];
        if (!sauti_aac_sbr_channel_frame(&s->channel[k], cc, &fr[k], grid) || !sauti_aac_sbr_channel_synthesize(&s->channel[k], grid, pcm)) return P_FAIL;
        for (i = 0; i < no; i++) d->cache[ch * i + k] = pcm[i];
    }
    s->pl = ns;
    return all_finite(d->cache, (size_t)no * ch) ? P_OK : P_FAIL;
}
static prod_t decode_er(dec_t *d) {
    SautiAacErConfig c; SautiAacReason reason = SAUTI_AAC_REASON_OK; int r;
    if (d->er_aot == 42u) return decode_usac(d);
    if (d->he960) return decode_he960(d);
    if (d->eldsbr) return decode_eldsbr(d);
    if (aot_is_lcn(d->er_aot) && d->config > 2u) {                           /* wielokanalowy: sauti_aac_lcm */
        SautiAacLcnConfig lc; lcn_config(&lc, d->er_aot, d->rate_index, d->config, d->er_480);
        if (!d->core.lcm) return P_FAIL;
        r = sauti_aac_lcm_decode(&lc, d->core.lcm, d->frame + 7u, d->frame_len - 7u, d->cache, &reason);
        if (r == 1 && !all_finite(d->cache, (size_t)d->frame_pcm * d->out_channels)) r = 0;
        return r == 1 ? P_OK : (r < 0 ? P_UNSUPPORTED : P_FAIL);
    }
    if (aot_is_lcn(d->er_aot)) {                                             /* AAC-LC z ramka 960, Main, LTP, ER LC, ER LTP */
        SautiAacLcnConfig lc; lcn_config(&lc, d->er_aot, d->rate_index, d->config, d->er_480);
        if (d->crc_present) r = sauti_aac_lcn_decode_adts(&lc, &d->core.u.lcn, d->frame + 9u, d->frame_len - 9u, d->cache, &reason, d->frame, ((unsigned)d->frame[7] << 8) | d->frame[8]);
        else r = sauti_aac_lcn_decode(&lc, &d->core.u.lcn, d->frame + 7u, d->frame_len - 7u, d->cache, &reason);
        if (r == 1 && !all_finite(d->cache, (size_t)d->frame_pcm * d->out_channels)) r = 0;
        return r == 1 ? P_OK : (r < 0 ? P_UNSUPPORTED : P_FAIL);
    }
    c.aot = d->er_aot; c.rate_index = d->rate_index; c.frame480 = d->er_480; c.channel_config = d->config;
    r = sauti_aac_er_decode(&c, &d->core.u.er, d->frame + 7u, d->frame_len - 7u, d->cache, &reason);
    if (r == 1 && !all_finite(d->cache, (size_t)d->frame_pcm * d->out_channels)) r = 0;
    return r == 1 ? P_OK : (r < 0 ? P_UNSUPPORTED : P_FAIL);
}
static prod_t decode_he(dec_t *d) {
    he_state_t *s = &d->core.u.he;
    SautiAacReason reason = SAUTI_AAC_REASON_OK;
    if (d->variant == V_HE_MONO) {
        float core[1024]; SautiAacSbrPayloadFrame payload; int r;
        SautiAacSbrComplex *grid = d->scratch_grid;
        r = sauti_aac_he_adts_mono_temporal_decode_ex(d->frame, d->frame_len, &s->mono, core, &payload, &reason);
        if (r != 1) return r < 0 ? P_UNSUPPORTED : P_FAIL;
        if (d->ps_mode) {
            if (!payload.has_ps) {
                if (!s->pending_payload.has_ps) return P_FAIL;           /* nie ma czego powtorzyc */
                he_ps_hold(&s->pending_payload.ps, &payload.ps, 32u); payload.has_ps = 1u;
            }
        }
        if (!sauti_aac_sbr_channel_frame(&s->channel[0], core, &payload, grid)) return P_FAIL;
        if (!d->ps_mode) {
            if (!sauti_aac_sbr_channel_synthesize(&s->channel[0], grid, d->cache)) return P_FAIL;
            return all_finite(d->cache, HE_FRAME_PCM) ? P_OK : P_FAIL;
        }
        if (s->pending_valid) {
            float st[4096];
            if (!he_render_ps(d, grid, st)) return P_FAIL;
            memcpy(d->cache, st, sizeof(st));
            memcpy(s->pending, grid, sizeof(s->pending));
            s->pending_payload = payload;
            return all_finite(d->cache, (size_t)HE_FRAME_PCM * 2u) ? P_OK : P_FAIL;
        }
        memcpy(s->pending, grid, sizeof(s->pending));
        s->pending_payload = payload; s->pending_valid = 1u;
        return P_NEED_MORE;
    } else {
        float core2[2u * 1024u], c[2][1024], pcm2[2][HE_FRAME_PCM]; unsigned i, k; int r;
        SautiAacSbrPayloadCpeFrame pl;
        SautiAacSbrComplex (*g2)[SAUTI_AAC_SBR_CHANNEL_GRID] = d->scratch_g2;
        r = sauti_aac_he_adts_cpe_decode_ex(d->frame, d->frame_len, &s->cpe, core2, &pl, &reason);
        if (r != 1) return r < 0 ? P_UNSUPPORTED : P_FAIL;
        for (i = 0; i < 1024u; i++) { c[0][i] = core2[2u * i]; c[1][i] = core2[2u * i + 1u]; }
        for (k = 0; k < 2u; k++)
            if (!sauti_aac_sbr_channel_frame(&s->channel[k], c[k], &pl.channel[k], g2[k]) || !sauti_aac_sbr_channel_synthesize(&s->channel[k], g2[k], pcm2[k])) return P_FAIL;
        for (i = 0; i < HE_FRAME_PCM; i++) { d->cache[2u * i] = pcm2[0][i]; d->cache[2u * i + 1u] = pcm2[1][i]; }
        return all_finite(d->cache, (size_t)HE_FRAME_PCM * 2u) ? P_OK : P_FAIL;
    }
}

/* koniec znanej liczby ramek: nie czytamy dalej (bajty za ostatnia poprawna ramka, np. znacznik, sa pomijane) */
static int at_known_end(const dec_t *d) { return d->total_known && d->core.frames_in >= d->known_frames; }

static void checkpoint_maybe(dec_t *d) {
    checkpoint_t *cp; size_t i;
    if (d->cont->boundary && !d->cont->boundary(d)) return;
    if (!d->seekable || d->core.pcm_out == 0 || d->core.pcm_out % d->cp_stride != 0) return;
    if (d->cp_count && d->cps[d->cp_count - 1]->core.pcm_out >= d->core.pcm_out) return;     /* ten blok juz zapisany (powtorzony odczyt po seek) */
    if (d->cp_count == d->cp_cap) {                                       /* pula pelna: zostaw co drugi punkt, stride x2 */
        size_t keep = 0;
        for (i = 0; i < d->cp_count; i++) {
            if (d->cps[i]->core.pcm_out % (d->cp_stride * 2u) == 0) d->cps[keep++] = d->cps[i];
            else { lcm_free(d, &d->cps[i]->core); d->host->free(d->host->user, d->cps[i]); }
        }
        d->cp_count = keep; d->cp_stride *= 2u;
        if (d->core.pcm_out % d->cp_stride != 0) return;
    }
    cp = (checkpoint_t *)d->host->alloc(d->host->user, sizeof *cp);
    if (!cp) return;                                                      /* brak pamieci: bez punktu kontrolnego, odczyt dziala dalej */
    cp->core = d->core;
    cp->core.lcm = lcm_dup(d, d->core.lcm);
    cp->core.usac_hs = usac_dup(d, d->core.usac_hs);
    if ((d->core.lcm && !cp->core.lcm) || (d->core.usac_hs && !cp->core.usac_hs)) { lcm_free(d, &cp->core); d->host->free(d->host->user, cp); return; }     /* brak pamieci: bez punktu kontrolnego */
    d->cps[d->cp_count++] = cp;
}

/* produkuje nastepny blok PCM do cache.  OK (cache_len>0), EOF, albo blad (WOULD_BLOCK: stan nietkniety) */
static maktabasauti_result_t produce(dec_t *d) {
    d->cache_pos = d->cache_len = 0;
    for (;;) {
        maktabasauti_result_t fr; prod_t p;
        if (d->variant == V_ER && d->he960 && d->ps_mode && d->core.u.h9.pending_valid && (d->src_eof || at_known_end(d))) {          /* HE-AAC v2 960: zalegly blok, cisza jako wyprzedzenie */
            he960_t *s = &d->core.u.h9;
            if (s->flushed) return MAKTABASAUTI_EOF;
            if (!he960_render_ps(d, NULL, d->cache)) return MAKTABASAUTI_E_CORRUPT_DATA;
            s->pending_valid = 0; s->flushed = 1;
            if (!all_finite(d->cache, 3840u)) return MAKTABASAUTI_E_CORRUPT_DATA;
            goto produced;
        }
        if ((d->variant == V_HE_MONO) && d->ps_mode && d->core.u.he.pending_valid && (d->src_eof || at_known_end(d) || d->fmt_pending)) {
            he_state_t *s = &d->core.u.he;
            if (s->flushed) return MAKTABASAUTI_EOF;
            if (!he_render_ps(d, NULL, d->cache)) return MAKTABASAUTI_E_CORRUPT_DATA;
            s->pending_valid = 0; s->flushed = 1;
            if (!all_finite(d->cache, (size_t)HE_FRAME_PCM * 2u)) return MAKTABASAUTI_E_CORRUPT_DATA;
            goto produced;
        }
        if (d->src_eof || at_known_end(d)) return MAKTABASAUTI_EOF;
        fr = eng_fetch(d);
        if (fr == MAKTABASAUTI_EOF) { d->src_eof = 1; continue; }
        if (fr == MAKTABASAUTI_E_CORRUPT_DATA) {                           /* zla ramka: ustaw wejscie na nastepnej poprawnej, zglos blad (historia dekodera jest juz nieciagla: flush) */
            maktabasauti_result_t rr = d->cont->resync(d);
            if (rr == MAKTABASAUTI_E_WOULD_BLOCK || rr == MAKTABASAUTI_E_IO) return rr;
            if (rr == MAKTABASAUTI_EOF) {                                  /* do konca wejscia nie ma juz poprawnej ramki: smieci na koncu (ID3v1, APE...) to koniec strumienia, nie blad;
                                                                            * HE-AAC v2 dostaje przy tym zaleglie wyprowadzenie ostatniego bloku (cisza jako wyprzedzenie) */
                d->src_eof = 1;
                if (d->core.frames_in > 0) continue;
            }
            return MAKTABASAUTI_E_CORRUPT_DATA;
        }
        if (fr == MAKTABASAUTI_E_TRUNCATED) { d->src_eof = 1; return fr; }   /* urwana ramka na koncu: nic wiecej nie przyjdzie */
        if (fr != MAKTABASAUTI_OK) return fr;
        {   aac_adts_hdr_t hd;
            if (aac_adts_parse_header(d->frame, d->frame_len, &hd) && !aac_same_stream(d, &hd)) {              /* zmiana parametrow w srodku strumienia */
                if (hd.raw_blocks != 1u) { d->have_frame = 0; return MAKTABASAUTI_E_UNSUPPORTED_FEATURE; }
                if (d->variant == V_HE_MONO && d->ps_mode && d->core.u.he.pending_valid && !d->core.u.he.flushed) { d->fmt_pending = 1; continue; }   /* najpierw zalegly blok HE-AAC v2 */
                return MAKTABASAUTI_E_FORMAT_CHANGED;                                                          /* ramka zostaje w d->frame: czyta ja aac_eng_read po przestawieniu */
            } }
        p = d->variant == V_ER ? decode_er(d) : ((d->variant <= V_LC_MULTI) ? decode_lc(d) : decode_he(d));
        d->have_frame = 0;                                                 /* ramka zuzyta (takze, gdy dekodowanie sie nie powiodlo: pomijamy ja) */
        d->core.in_offset = d->cont->position ? d->cont->position(d) : d->host->tell(d->host->user, d->rd);
        if ((p == P_UNSUPPORTED || p == P_FAIL) && d->resyncing && d->resyncing < 2000u) { d->resyncing++; continue; }   /* po flush: czekamy na ramke, od ktorej dekodowanie sie wznawia */
        if (p == P_UNSUPPORTED) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
        if (p == P_FAIL) return MAKTABASAUTI_E_CORRUPT_DATA;
        d->resyncing = 0;
        d->core.frames_in++;
        if (p == P_NEED_MORE) continue;
        goto produced;
    }
produced:
    d->core.pcm_out++;
    d->cache_len = d->frame_pcm;
    checkpoint_maybe(d);
    return MAKTABASAUTI_OK;
}

/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
static void drop_checkpoints(dec_t *d) {
    size_t i;
    for (i = 0; i < d->cp_count; i++) { lcm_free(d, &d->cps[i]->core); d->host->free(d->host->user, d->cps[i]); }
    d->cp_count = 0; d->cp_stride = (d->variant >= V_HE_MONO) ? 256u : 64u;
}

static uint64_t default_mask(unsigned channels);
static maktabasauti_result_t eng_classify(dec_t *d, const aac_adts_hdr_t *hdp);
static void fill_info(const dec_t *d, maktabasauti_stream_info_t *info) {
    memset(info, 0, sizeof *info);
    info->struct_size = sizeof *info;
    info->sample_rate = d->out_rate; info->channels = d->out_channels; info->channel_mask = default_mask(d->out_channels);
    info->native_format = MAKTABASAUTI_FORMAT_F32;
    info->total_frames = d->trimmed ? d->avail : (d->total_known ? d->known_frames * d->frame_pcm : MAKTABASAUTI_UNKNOWN_LENGTH);
    info->flags = d->seekable ? MAKTABASAUTI_STREAM_SEEKABLE : 0u;
    strcpy(info->codec_name, "aac"); strncpy(info->container_name, d->cont->name, sizeof info->container_name - 1u);
    strcpy(info->codec_profile, d->variant == V_ER ? (d->er_aot == 42u ? "USAC" : d->er_aot == 39u ? "ELD" : (d->er_aot == 2u ? (d->he960 ? "HE-AAC" : "LC") : (d->er_aot == 1u ? "Main" : (d->er_aot == 4u ? "LTP" : (d->er_aot == 17u ? "ER-LC" : (d->er_aot == 19u ? "ER-LTP" : "LD")))))) : (d->variant >= V_HE_MONO ? "HE-AAC" : "LC"));
}

maktabasauti_result_t MAKTABASAUTI_CALL aac_eng_get_info(const void *state, maktabasauti_stream_info_t *info) {
    const dec_t *d = (const dec_t *)state;
    uint32_t keep = info->struct_size;
    fill_info(d, info);
    info->struct_size = keep ? keep : (uint32_t)sizeof *info;
    return MAKTABASAUTI_OK;
}

/* zmiana parametrow w srodku strumienia (czestotliwosc, uklad kanalow, CRC): ramka z nowymi parametrami czeka w d->frame; dekoder jest klasyfikowany od nowa na tej ramce i startuje ze
 * swiezym stanem.  Po zmianie dlugosc jest nieznana, a przewijanie wylaczone (pozycje PCM sprzed zmiany mialy inny format: punkty kontrolne i licznik ramek nie obowiazuja). */
static maktabasauti_result_t reconfigure(dec_t *d) {
    aac_adts_hdr_t hd; maktabasauti_result_t r;
    if (!aac_adts_parse_header(d->frame, d->frame_len, &hd) || hd.raw_blocks != 1u) { d->have_frame = 0; return MAKTABASAUTI_E_UNSUPPORTED_FEATURE; }
    r = eng_classify(d, &hd);
    if (r != MAKTABASAUTI_OK) { d->have_frame = 0; return r; }                                          /* nowe parametry nieobslugiwane */
    d->cache_pos = d->cache_len = 0; d->fmt_pending = 0; d->fmt_changed = 1;
    d->seekable = 0; d->total_known = 0; d->trimmed = 0; d->trim_skip = 0; d->avail = 0; d->skip_left = 0; d->resyncing = 0;
    drop_checkpoints(d);
    d->core.in_offset = d->cont->position ? d->cont->position(d) : d->host->tell(d->host->user, d->rd);
    return MAKTABASAUTI_OK;
}

maktabasauti_result_t MAKTABASAUTI_CALL aac_eng_read(void *state, void *dst, size_t n, size_t *frames) {
    dec_t *d = (dec_t *)state; float *out = (float *)dst; size_t done = 0; const size_t ch = d->out_channels;
    *frames = 0;
    if (d->pend != MAKTABASAUTI_OK) { maktabasauti_result_t r = d->pend; return r; }
    if (d->trimmed) {                                                                                    /* wyjscie konczy sie po `avail` ramkach (przyciecie) */
        if (d->position >= d->avail) return MAKTABASAUTI_EOF;
        if (n > d->avail - d->position) n = (size_t)(d->avail - d->position);
    }     /* odlozony blad: lepki do flush/seek/reset (rdzen tez go pamieta) */
    while (done < n) {
        size_t take;
        if (d->cache_pos == d->cache_len) {
            maktabasauti_result_t r = produce(d);
            if (r == MAKTABASAUTI_E_FORMAT_CHANGED) {                      /* ramki sprzed zmiany wydajemy najpierw; potem przestawiamy dekoder i zglaszamy zmiane */
                if (done) { *frames = done; return MAKTABASAUTI_OK; }
                r = reconfigure(d);
                if (r != MAKTABASAUTI_OK) { d->pend = r; return r; }
                *frames = 0;
                return MAKTABASAUTI_E_FORMAT_CHANGED;
            }
            if (r != MAKTABASAUTI_OK) {
                *frames = done;
                if (r == MAKTABASAUTI_EOF) return done ? MAKTABASAUTI_OK : MAKTABASAUTI_EOF;
                if (r == MAKTABASAUTI_E_WOULD_BLOCK) return done ? MAKTABASAUTI_OK : r;                  /* nielepki: stan nietkniety, mozna powtorzyc */
                if (done) { d->pend = r; return MAKTABASAUTI_OK; }                                    /* najpierw oddajemy to, co sie udalo */
                d->pend = r;
                return r;
            }
        }
        if (d->skip_left) {                                                                              /* przyciecie na poczatku: ramki sa dekodowane i wyrzucane */
            size_t drop = d->cache_len - d->cache_pos;
            if (drop > d->skip_left) drop = (size_t)d->skip_left;
            d->cache_pos += drop; d->skip_left -= drop;
            continue;
        }
        take = n - done; if (take > d->cache_len - d->cache_pos) take = d->cache_len - d->cache_pos;
        memcpy(out + done * ch, d->cache + d->cache_pos * ch, take * ch * sizeof(float));
        d->cache_pos += take; done += take; d->position += take;
    }
    *frames = done;
    return MAKTABASAUTI_OK;
}

static void restore_core(dec_t *d, const core_t *c) {
    if (d->cont->discard) d->cont->discard(d);
    lcm_free(d, &d->core);
    d->core = *c;
    d->core.lcm = lcm_dup(d, c->lcm);
    d->core.usac_hs = usac_dup(d, c->usac_hs);
    d->cache_pos = d->cache_len = 0; d->have_frame = 0; d->src_eof = 0; d->pend = MAKTABASAUTI_OK;
}

maktabasauti_result_t MAKTABASAUTI_CALL aac_eng_seek(void *state, uint64_t frame, uint32_t flags, uint64_t *actual) {
    dec_t *d = (dec_t *)state; uint64_t tb, within, dt = frame + d->trim_skip; const core_t *best = &d->cp0; size_t i; int continue_current; (void)flags;
    if (!d->seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;
    if (d->trimmed ? frame > d->avail : (d->total_known && frame > d->known_frames * d->frame_pcm)) return MAKTABASAUTI_E_OUT_OF_RANGE;
    tb = dt / d->frame_pcm; within = dt % d->frame_pcm;                                                  /* dt: pozycja w strumieniu zdekodowanym (przed przycieciem) */
    if (d->cache_len && d->core.pcm_out && tb == d->core.pcm_out - 1u && d->pend == MAKTABASAUTI_OK) {      /* blok, ktory juz jest w cache */
        d->cache_pos = (size_t)within; d->skip_left = 0; d->position = frame; *actual = frame; return MAKTABASAUTI_OK;
    }
    for (i = 0; i < d->cp_count; i++) if (d->cps[i]->core.pcm_out <= tb && d->cps[i]->core.pcm_out >= best->pcm_out) best = &d->cps[i]->core;
    /* kontynuacja od biezacego stanu, gdy cel jest za nim i zaden punkt kontrolny nie jest blizej */
    continue_current = d->pend == MAKTABASAUTI_OK && tb >= d->core.pcm_out && best->pcm_out <= d->core.pcm_out;
    if (!continue_current) {
        maktabasauti_result_t pr = d->cont->seek_to ? d->cont->seek_to(d, best->in_offset)
            : ((d->host->seek(d->host->user, d->rd, best->in_offset, 0) < 0) ? MAKTABASAUTI_E_NOT_SEEKABLE : MAKTABASAUTI_OK);
        if (pr != MAKTABASAUTI_OK) return pr;                                                             /* stan bez zmian */
        restore_core(d, best);
    }
    d->cache_pos = d->cache_len = 0;
    while (d->core.pcm_out < tb) {                                                                       /* bloki do (bez) celu: tylko dekodowanie, PCM jest wyrzucany */
        maktabasauti_result_t r = produce(d);
        if (r == MAKTABASAUTI_EOF) { d->cache_pos = d->cache_len = 0; goto landed_at_end; }
        if (r != MAKTABASAUTI_OK) return r;
    }
    d->cache_pos = d->cache_len = 0;
    d->skip_left = 0;
    if (!(d->total_known && dt >= d->known_frames * d->frame_pcm)) {                                     /* blok zawierajacy cel */
        maktabasauti_result_t r = produce(d);
        if (r == MAKTABASAUTI_OK) d->cache_pos = (size_t)within;
        else if (r == MAKTABASAUTI_EOF) goto landed_at_end;
        else return r;
    } else { d->cache_len = d->cache_pos = 0; }
    d->position = frame; *actual = frame;
    return MAKTABASAUTI_OK;
landed_at_end:                                                                                           /* za koncem strumienia (dlugosc nieznana): ladujemy na koncu */
    d->cache_pos = d->cache_len = 0;
    d->position = d->core.pcm_out * d->frame_pcm; if (d->position >= d->trim_skip) d->position -= d->trim_skip; else d->position = 0;
    *actual = d->position;
    return MAKTABASAUTI_OK;
}

maktabasauti_result_t MAKTABASAUTI_CALL aac_eng_reset(void *state) {
    dec_t *d = (dec_t *)state;
    if (d->fmt_changed) return MAKTABASAUTI_E_NOT_SEEKABLE;                                              /* po zmianie parametrow punkt 0 opisuje stary format */
    if (d->cont->seek_to) { maktabasauti_result_t pr = d->cont->seek_to(d, d->cp0.in_offset); if (pr != MAKTABASAUTI_OK) return pr; }   /* kontenery o wlasnej pozycji (MP4: numer probki) */
    restore_core(d, &d->cp0);
    d->skip_left = d->trim_skip;
    d->position = 0;                                                      /* rdzen przewinal wejscie na poczatek przed wywolaniem */
    return MAKTABASAUTI_OK;
}

/* flush: porzuca historie dekodera i kontynuuje od biezacej pozycji wejscia (po uszkodzeniu produce() juz ustawilo je na nastepnej poprawnej ramce).  Po flush licznik ramek i punkty
 * kontrolne nie obowiazuja, wiec przewijanie jest wylaczone (tryb strumienia na zywo). */
maktabasauti_result_t MAKTABASAUTI_CALL aac_eng_flush(void *state) {
    dec_t *d = (dec_t *)state;
    if (d->cont->discard) d->cont->discard(d);
    core_reset(d);
    d->core.in_offset = d->cont->position ? d->cont->position(d) : d->host->tell(d->host->user, d->rd);
    d->cache_pos = d->cache_len = 0; d->have_frame = 0; d->pend = MAKTABASAUTI_OK;
    drop_checkpoints(d); d->total_known = 0; d->seekable = 0; d->resyncing = 1; d->skip_left = 0; d->trimmed = 0;
    return MAKTABASAUTI_OK;
}

const char *MAKTABASAUTI_CALL aac_eng_last_error(const void *state) { const dec_t *d = (const dec_t *)state; return d->err[0] ? d->err : NULL; }

void MAKTABASAUTI_CALL aac_eng_close(void *state) {
    dec_t *d = (dec_t *)state;
    const maktabasauti_host_t *h;
    if (!d) return;
    h = d->host;
    if (d->cont->release) d->cont->release(d);
    drop_checkpoints(d);
    lcm_free(d, &d->core); lcm_free(d, &d->cp0);
    if (d->drc_plan) h->free(h->user, d->drc_plan);
    if (d->cps) h->free(h->user, d->cps);
    h->free(h->user, d);
}

/* ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- */

static uint64_t default_mask(unsigned channels) {
    switch (channels) { case 1: return MAKTABASAUTI_LAYOUT_MONO; case 2: return MAKTABASAUTI_LAYOUT_STEREO; case 6: return MAKTABASAUTI_LAYOUT_5_1; case 8: return MAKTABASAUTI_LAYOUT_7_1; default: return 0; }
}

/* sekwencyjne przejscie po naglowkach: liczy ramki ADTS od biezacej pozycji (po pierwszej ramce) do konca albo pierwszej niepoprawnej; wraca na `resume` */

/* wariant wg PIERWSZEJ ramki (z d->frame, naglowek hd), w kolejnosci starego rejestru; probne dekodowanie na swiezym stanie, potem stan wraca do poczatkowego.  Uzywane przy otwarciu
 * i przy zmianie parametrow w srodku strumienia (E_FORMAT_CHANGED).  Ustawia czestotliwosc, kanaly, rozmiar bloku PCM i wariant; OK albo E_UNSUPPORTED_FEATURE. */
static maktabasauti_result_t eng_classify(dec_t *d, const aac_adts_hdr_t *hdp) {
    const aac_adts_hdr_t hd = *hdp; prod_t pr = P_FAIL; variant_t chosen = V_LC_STEREO;
    d->rate_index = hd.rate_index; d->core_rate = hd.rate; d->config = hd.config; d->crc_present = hd.crc_present;
    d->he960 = 0u;
    if (hd.er && hd.er_aot == 42u) {                                   /* USAC: konfiguracja z ASC (kontener), probne dekodowanie pierwszej ramki */
        prod_t up;
        if (!d->usac_ready || d->usac.aac_rate_index != hd.rate_index || d->usac.channel_config != hd.config || d->usac.out_channels < 1u || d->usac.out_channels > SAUTI_USAC_MAX_CHANNELS || d->usac.channel_config > 7u) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
        d->core_rate = d->usac.core_rate;
        d->variant = V_ER; d->er_aot = 42u; d->er_480 = 0u; d->core_channels = d->out_channels = d->usac.out_channels; d->ps_mode = 0;
        if ((size_t)d->usac.out_frame_len * d->usac.out_channels > CACHE_FLOATS) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;     /* the block of one frame must fit the cache */
        d->frame_pcm = d->usac.out_frame_len; d->out_rate = d->usac.sample_rate;
        if (host_opt_is(d->host, "aac.drc", "on") || (d->host->option && d->host->option(d->host->user, "aac.loudness_target"))) {      /* uniDRC: opcja wyjsciowa, domyslnie wylaczona */
            SautiUniDrcOptions o; const char *v;
            memset(&o, 0, sizeof o); o.enable = 1u; o.drc = host_opt_is(d->host, "aac.drc", "on"); o.boost = o.compress = 1.0f; o.loudness_target = -24.0f;
            if ((v = d->host->option ? d->host->option(d->host->user, "aac.drc_effect") : NULL) != NULL) o.effect = (unsigned)atoi(v);
            if ((v = d->host->option ? d->host->option(d->host->user, "aac.loudness_target") : NULL) != NULL) { o.loudness_normalise = 1; o.loudness_target = (float)atof(v); }
            if ((v = d->host->option ? d->host->option(d->host->user, "aac.drc_boost") : NULL) != NULL) o.boost = (float)atof(v);
            if ((v = d->host->option ? d->host->option(d->host->user, "aac.drc_compress") : NULL) != NULL) o.compress = (float)atof(v);
            d->drc_plan = (SautiUniDrcPlan *)d->host->alloc(d->host->user, sizeof *d->drc_plan);
            if (d->drc_plan && !sauti_unidrc_plan(d->drc_plan, d->usac_asc, d->usac_asc_len, &d->usac, &o)) { d->host->free(d->host->user, d->drc_plan); d->drc_plan = NULL; }
        }
        core_reset(d);
        up = decode_usac(d);
        core_reset(d);
        return up == P_OK ? MAKTABASAUTI_OK : MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    }
    if (hd.er) {                                                       /* AAC-LD / AAC-ELD: jeden wariant, bez wykrywania */
        SautiAacErConfig c; unsigned n;
        c.aot = hd.er_aot; c.rate_index = hd.rate_index; c.frame480 = hd.er_480; c.channel_config = hd.config;
        if (aot_is_lcn(hd.er_aot)) { SautiAacLcnConfig lc; lcn_config(&lc, hd.er_aot, hd.rate_index, hd.config, hd.er_480); n = hd.config > 2u ? (sauti_aac_lcm_channels(&lc) ? 1024u : 0u) : sauti_aac_lcn_frame_samples(&lc); }
        else n = sauti_aac_er_frame_samples(&c);
        if (!n || (hd.crc_present && (hd.er_aot == 39u || hd.er_aot == 23u || hd.er_aot == 2u || hd.er_aot == 17u || hd.er_aot == 19u || hd.config > 2u))) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;   /* error check: AAC Main / LTP and (via sauti_aac_lcn) ADTS only; LD / ELD and the 960 layout have none */
        d->variant = V_ER; d->er_aot = hd.er_aot; d->er_480 = hd.er_480;
        d->core_channels = hd.config > 2u ? sauti_aac_lc_multi_config_channels(hd.config) : hd.config; d->out_channels = d->core_channels; d->ps_mode = 0;
        d->frame_pcm = n; d->out_rate = d->core_rate; d->he960 = 0u; d->eldsbr = 0u;
        if (hd.er_aot == 39u && d->eld_ready && d->eld_asc.ld_sbr) {                          /* ELD z SBR niskoopoznieniowym: jednokrotny (wyjscie w czestotliwosci rdzenia) albo dwukrotny (wyjscie 2x) */
            if (hd.config < 1u || hd.config > 2u) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
            d->eldsbr = d->eld_asc.ld_sbr_downsampled ? 2u : 1u;
            if (d->eld_asc.ld_sac_len) {                                                   /* ELD v2: mono rdzen + MPS 212 niskiego opoznienia = wyjscie stereo; tylko dwukrotny SBR (64 pasma QMF) */
                SautiAacEldSacConfig sc;
                if (hd.config != 1u || d->eldsbr != 2u || sauti_aac_eld_sac_config_parse(d->eld_asc.ld_sac, d->eld_asc.ld_sac_len, &sc) != 1 || sc.time_slots != (hd.er_480 ? 15u : 16u) || sc.mps.temp_shape != 0u) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
                d->out_channels = 2u;
            }
            d->frame_pcm = n * d->eldsbr; d->out_rate = d->core_rate * d->eldsbr;
        }
        core_reset(d);
        pr = decode_er(d);
        if (pr == P_UNSUPPORTED && hd.er_aot == 2u && hd.config >= 1u && hd.config <= 2u) {      /* HE-AAC z ramka 960: SBR (wykrywany z pierwszej ramki) */
            d->he960 = 1u; d->frame_pcm = 1920u; d->out_rate = d->core_rate * 2u;
            core_reset(d);
            pr = decode_er(d);
            if (!(pr == P_OK || pr == P_NEED_MORE)) { d->he960 = 0u; d->ps_mode = 0; d->out_channels = d->core_channels; }
        }
        if (!(pr == P_OK || pr == P_NEED_MORE)) { lcm_free(d, &d->core); return MAKTABASAUTI_E_UNSUPPORTED_FEATURE; }
        core_reset(d);
        return MAKTABASAUTI_OK;
    }
    d->er_aot = 0u; d->er_480 = 0u;
    /* wariant wg PIERWSZEJ ramki, w kolejnosci starego rejestru; probne dekodowanie na swiezym stanie, potem stan wraca do poczatkowego */
    {   static const variant_t order_stereo[] = { V_LC_STEREO, V_HE_CPE }, order_mono[] = { V_LC_MONO, V_HE_MONO }, order_multi[] = { V_LC_MULTI };
        const variant_t *order = hd.config == 2u ? order_stereo : hd.config == 1u ? order_mono : order_multi;
        size_t norder = hd.config == 2u ? 2u : hd.config == 1u ? 2u : 1u, k;
        if (hd.config == 0u || hd.config >= 3u) {
            if (hd.crc_present) { return MAKTABASAUTI_E_UNSUPPORTED_FEATURE; }             /* wielokanalowy LC bez CRC (jak wczesniej) */
            if (hd.config == 0u) {
                int sc = sauti_aac_lc_multi_scan_pce(d->frame, d->frame_len, &d->layout);
                if (sc != 1) { return MAKTABASAUTI_E_UNSUPPORTED_FEATURE; }
            } else if (!sauti_aac_lc_multi_config_channels(hd.config)) { return MAKTABASAUTI_E_UNSUPPORTED_FEATURE; }
        }
        for (k = 0; k < norder; k++) {
            d->variant = order[k];
            d->core_channels = hd.config == 0u ? d->layout.channels : (order[k] == V_LC_MULTI ? sauti_aac_lc_multi_config_channels(hd.config) : (hd.config == 1u ? 1u : 2u));
            d->ps_mode = 0;
            core_reset(d);
            d->out_channels = d->core_channels;
            d->frame_pcm = order[k] >= V_HE_MONO ? HE_FRAME_PCM : LC_FRAME_PCM;
            d->out_rate = order[k] >= V_HE_MONO ? d->core_rate * 2u : d->core_rate;
            if (order[k] == V_HE_MONO) {                                   /* PS wykrywane z pierwszej ramki (jak w starym adapterze) */
                float core1[1024]; SautiAacSbrPayloadFrame payload1; SautiAacReason why = SAUTI_AAC_REASON_OK;
                if (sauti_aac_he_adts_mono_temporal_decode_ex(d->frame, d->frame_len, &d->core.u.he.mono, core1, &payload1, &why) != 1) { pr = P_FAIL; continue; }
                if (payload1.has_ps) { d->ps_mode = 1; d->out_channels = 2u; }
                core_reset(d);
            }
            pr = order[k] <= V_LC_MULTI ? decode_lc(d) : decode_he(d);
            if (pr == P_OK || pr == P_NEED_MORE) { chosen = order[k]; break; }
        }
        if (!(pr == P_OK || pr == P_NEED_MORE)) { return MAKTABASAUTI_E_UNSUPPORTED_FEATURE; }   /* pierwsza ramka nie dekoduje sie zadnym wariantem */
    }
    d->variant = chosen;
    core_reset(d);
    return MAKTABASAUTI_OK;
}

maktabasauti_result_t aac_eng_open(const maktabasauti_codec_t *self, const aac_container_t *cont, const maktabasauti_host_t *host, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    dec_t *d = NULL; maktabasauti_result_t r; aac_adts_hdr_t hd; uint32_t flags; const char *scan_opt, *gapless_opt; int64_t after_first;
    *state = NULL;
    d = (dec_t *)host->alloc(host->user, sizeof *d);
    if (!d) return MAKTABASAUTI_E_NO_MEMORY;
    memset(d, 0, sizeof *d);
    d->codec = self; d->cont = cont; d->host = host; d->rd = rd;
    flags = host->input_flags ? host->input_flags(host->user, rd) : 0u;
    d->seekable = (flags & MAKTABASAUTI_INPUT_SEEKABLE) != 0;
    if (cont->prepare) { r = cont->prepare(d); if (r != MAKTABASAUTI_OK) goto fail; }
    r = eng_fetch(d);
    if (r == MAKTABASAUTI_E_CORRUPT_DATA || r == MAKTABASAUTI_E_TRUNCATED || r == MAKTABASAUTI_EOF) r = MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    if (r != MAKTABASAUTI_OK) goto fail;
    (void)aac_adts_parse_header(d->frame, d->frame_len, &hd);
    if (hd.raw_blocks != 1u) { r = MAKTABASAUTI_E_UNSUPPORTED_FEATURE; goto fail; }   /* wiele raw_data_block w ramce: niewspierane (jak wczesniej) */
    after_first = cont->position ? cont->position(d) : host->tell(host->user, rd);

    r = eng_classify(d, &hd);
    if (r != MAKTABASAUTI_OK) goto fail;
    d->core.in_offset = 0; lcm_free(d, &d->cp0); d->cp0 = d->core; d->cp0.lcm = lcm_dup(d, d->core.lcm); d->cp0.usac_hs = usac_dup(d, d->core.usac_hs);                                                          /* blok 0: ramka 0 jest jeszcze w d->frame (have_frame) */
    d->cp_stride = d->variant >= V_HE_MONO && d->variant != V_ER ? 256u : 64u;
    d->cp_max = CHECKPOINT_BUDGET / sizeof(checkpoint_t); if (d->cp_max > CHECKPOINT_MAX) d->cp_max = CHECKPOINT_MAX; if (d->cp_max < 4u) d->cp_max = 4u;
    d->cp_cap = d->cp_max;
    d->cps = (checkpoint_t **)host->alloc(host->user, d->cp_cap * sizeof *d->cps);
    if (!d->cps) { r = MAKTABASAUTI_E_NO_MEMORY; goto fail; }

    /* dlugosc: przejscie po naglowkach (tylko gdy wejscie sie przewija) */
    scan_opt = host->option ? host->option(host->user, "aac.scan") : NULL;
    if (!scan_opt && host->option) scan_opt = host->option(host->user, "adts.scan");
    d->total_known = 0;
    if ((d->seekable || cont->scan_without_seek) && !(scan_opt && (!strcmp(scan_opt, "off") || !strcmp(scan_opt, "0")))) {
        uint64_t count = 0; int consistent = 0;
        r = d->cont->scan(d, after_first, &count, &consistent);
        if (r != MAKTABASAUTI_OK) goto fail;
        if (consistent) { d->total_known = 1; d->known_frames = count; }
    }
    gapless_opt = host->option ? host->option(host->user, "gapless") : NULL;                             /* MAKTABASAUTI_OPEN_NO_GAPLESS: bez przyciecia priming/dopelnienia */
    if (cont->trim && !(gapless_opt && !strcmp(gapless_opt, "off"))) {                                   /* przyciecie gapless: wymaga znanej dlugosci (albo tylko skip) */
        uint64_t skip = 0, avail = 0;
        if (!cont->trim(d, d->total_known ? d->known_frames * d->frame_pcm : 0u, &skip, &avail)) { r = MAKTABASAUTI_E_UNSUPPORTED_FEATURE; goto fail; }
        if (skip || avail) { d->trimmed = d->total_known || avail != 0u; d->trim_skip = skip; d->avail = avail; d->skip_left = skip; }
    }
    d->have_frame = 1;                                                                                 /* pierwsza ramka czeka w buforze: pierwszy produce() ja zdekoduje */
    d->core.in_offset = after_first;
    /* ramka 0 jest w buforze, a punkt kontrolny 0 ma wejscie na jej poczatku (0): przywrocenie punktu 0 pobierze ja ponownie */

    fill_info(d, info);
    *state = d;
    return MAKTABASAUTI_OK;
fail:
    if (d) { if (cont->release) cont->release(d); lcm_free(d, &d->core); lcm_free(d, &d->cp0); if (d->cps) host->free(host->user, d->cps); host->free(host->user, d); }
    return r;
}

