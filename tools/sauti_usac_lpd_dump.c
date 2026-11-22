/* Test tool: walks the access units of PREFIX.asc + PREFIX.aus (mono, one SCE, LPD / FD frames) with the LPD syntax layer and prints the mode of every frame and how many bits are left at its end.
 * usage: sauti_usac_lpd_dump PREFIX */
#include "sauti_usac.h"
#include "sauti_usac_lpd_dec.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); uint8_t *b; long len;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET); b = (uint8_t *)malloc((size_t)len + 1u);
    if (!b || fread(b, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(b); return NULL; }
    fclose(f); *n = (size_t)len; return b;
}
static unsigned long ones(const SautiUsacBits *b) { unsigned long n = 0; size_t p; for (p = b->pos; p < b->nbits; p++) n += (b->data[p >> 3] >> (7u - (p & 7u))) & 1u; return n; }
static FILE *ref_lsf, *ref_syn, *ref_ovl, *ref_ws; static SautiUsacLpdState lpd_st; static unsigned lpd_seed = 0x3039u; static unsigned frame_no;
static SautiUsacConfig cfg; static SautiUsacAcState ac; static SautiUsacLpdFrame *fr; static unsigned au_no;
static int walk(const uint8_t *au, size_t nbits, int depth) {
    SautiUsacBits b; unsigned el;
    b.data = au; b.nbits = nbits; b.pos = 0;
    const unsigned indep = sauti_usac_get(&b, 1);
    for (el = 0; el < cfg.num_elements; el++) {
        const SautiUsacElement *e = &cfg.e[el];
        if (e->type == SAUTI_USAC_EXT) {
            unsigned len;
            if (!sauti_usac_get(&b, 1)) continue;
            if (sauti_usac_get(&b, 1)) len = e->ext_default_len; else { len = sauti_usac_get(&b, 8); if (len == 255u) len += sauti_usac_get(&b, 16) - 2u; }
            if (!len) continue;
            if (e->ext_payload_frag) (void)sauti_usac_get(&b, 2);
            if (e->ext_type == 3u) {
                SautiUsacBits p = b; unsigned cl, num, k;
                p.nbits = b.pos + (size_t)len * 8u;
                cl = sauti_usac_escaped(&p, 4, 4, 8); p.pos += (size_t)cl * 8u; (void)sauti_usac_get(&p, 2);
                num = sauti_usac_escaped(&p, 2, 4, 0);
                for (k = 0; k < num; k++) {
                    const unsigned al = sauti_usac_escaped(&p, 16, 16, 0);
                    uint8_t *buf = (uint8_t *)calloc(al + 1u, 1u); size_t j;
                    for (j = 0; j < (size_t)al * 8u; j++) { const size_t at = p.pos + j; if ((au[at >> 3] >> (7u - (at & 7u))) & 1u) buf[j >> 3] |= (uint8_t)(0x80u >> (j & 7u)); }
                    printf("  [preroll %u/%u] ", k, num);
                    walk(buf, (size_t)al * 8u, depth + 1); free(buf);
                    p.pos += (size_t)al * 8u;
                }
            }
            b.pos += (size_t)len * 8u;
            continue;
        }
        if (e->type == SAUTI_USAC_SCE) {
            const unsigned core_mode = sauti_usac_get(&b, 1);
            if (core_mode) {
                const int r = sauti_usac_lpd_parse(&b, cfg.core_frame_len, (int)indep, &ac, fr);
                printf("AU %u: LPD r=%d acelp_mode %u mod %u%u%u%u bpf %u last %u fac %u fac_end %u(%u) arith_reset %u lsf_sets %u%u%u%u%u left %ld bits (ones in the rest: %lu)\n", au_no, r, fr->acelp_core_mode, fr->mod[0], fr->mod[1], fr->mod[2], fr->mod[3], fr->bpf_control_info, fr->core_mode_last, fr->fac_data_present, fr->fac_end.present, fr->fac_end.gain, fr->arith_reset, fr->lsf[0].present, fr->lsf[1].present, fr->lsf[2].present, fr->lsf[3].present, fr->lsf[4].present, (long)b.nbits - (long)b.pos, ones(&b));
                if (r == 1 && ref_lsf) {                         /* L1 check against the instrumented reference: LSF sets in Hz */
                    float ref[5][16], mine[5][16]; unsigned q; double worst = 0;
                    memset(mine, 0, sizeof mine);
                    if (fread(ref, sizeof ref, 1, ref_lsf) == 1) {
                        if (!fr->first_lpd_flag) memcpy(mine[0], ref[0], sizeof mine[0]);
                        if (!sauti_usac_lpd_lsf_dequant(fr, mine)) printf("  lsf_dequant FAILED\n");
                        for (q = 0; q < 5u; q++) { unsigned i; const int present = q == 4u || (q == 0u && fr->first_lpd_flag) || (q == 2u && fr->mod[0] < 3u) || (q == 1u && fr->mod[0] < 2u) || (q == 3u && fr->mod[2] < 2u); if (present) for (i = 0; i < 16u; i++) { const double d = fabs((double)mine[q][i] - ref[q][i]); if (d > worst) worst = d; } }
                        printf("  frame %u: max |LSF - reference| = %.6f Hz\n", frame_no, worst);
                    }
                }
                if (r == 1 && ref_syn) {                         /* L2-L4: the decoded frame against libxaac's synthesis */
                    float *o = (float *)malloc(4096 * sizeof(float)), *ref = (float *)malloc(4096 * sizeof(float));
                    if (fread(ref, sizeof(float), cfg.core_frame_len, ref_syn) == cfg.core_frame_len) {
                        double e = 0, p = 0; unsigned i;
                        float tail[1024]; int ws = 0, have = 0;
                        if (!lpd_st.prev_lpd && ref_ovl && fread(tail, sizeof(float), cfg.core_frame_len, ref_ovl) == cfg.core_frame_len) { have = 1; if (ref_ws && fread(&ws, sizeof ws, 1, ref_ws) != 1) ws = 0; }
                        const int ok = sauti_usac_lpd_decode(&lpd_st, fr, cfg.core_frame_len, &lpd_seed, have ? tail : NULL, ws == 2, o);
                        if (ok) for (i = 0; i < cfg.core_frame_len; i++) { const double d = (double)o[i] - ref[i]; e += d * d; p += (double)ref[i] * ref[i]; }
                        printf("  frame %u: synthesis ok=%d  snr %.1f dB (signal rms %.1f)\n", frame_no, ok, e > 0 ? 10 * log10(p / e) : 999.0, sqrt(p / cfg.core_frame_len));
                    }
                    free(o); free(ref);
                }
                frame_no++;
            } else { printf("AU %u: FD frame (not parsed)\n", au_no); lpd_st.prev_lpd = 0; }
            if (core_mode == 0u) return 0;
        } else return 0;
    }
    return 1;
}
int main(int argc, char **argv) {
    char path[1024]; size_t na, nu, p = 0; uint8_t *asc, *aus; unsigned type, bit;
    if (argc < 2) return 2;
    snprintf(path, sizeof path, "%s.asc", argv[1]); asc = slurp(path, &na);
    snprintf(path, sizeof path, "%s.aus", argv[1]); aus = slurp(path, &nu);
    if (!asc || !aus) return 1;
    type = (unsigned)(asc[0] >> 3); bit = 5; if (type == 31) { type = 32u + (unsigned)((asc[0] & 7u) << 3 | asc[1] >> 5); bit = 11; }
    bit += 8;
    if (sauti_usac_config_parse(asc, na, bit, &cfg) != 1) { fprintf(stderr, "config\n"); return 1; }
    fr = (SautiUsacLpdFrame *)malloc(sizeof *fr);
    if (getenv("LPD_REF_DIR")) { snprintf(path, sizeof path, "%s/fsynth.bin", getenv("LPD_REF_DIR")); ref_syn = fopen(path, "rb"); snprintf(path, sizeof path, "%s/ovl_raw.bin", getenv("LPD_REF_DIR")); ref_ovl = fopen(path, "rb"); snprintf(path, sizeof path, "%s/ovl_ws.bin", getenv("LPD_REF_DIR")); ref_ws = fopen(path, "rb"); sauti_usac_lpd_state_init(&lpd_st, cfg.core_rate); snprintf(path, sizeof path, "%s/lsf.bin", getenv("LPD_REF_DIR")); ref_lsf = fopen(path, "rb"); }
    while (p + 4 <= nu) {
        const size_t n = ((size_t)aus[p] << 24) | ((size_t)aus[p + 1] << 16) | ((size_t)aus[p + 2] << 8) | aus[p + 3];
        walk(aus + p + 4, n * 8u, 0); p += 4 + n; au_no++;
    }
    return 0;
}
