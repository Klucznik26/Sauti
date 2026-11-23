/* Test tool: walks the access units of PREFIX.asc + PREFIX.aus (AAC-ELD with the low-delay SBR tool: the raw units written by an encoder, as tools/mux_raw_aus_mp4.py takes them), decodes the core with sauti_aac_er_decode_ex and parses the SBR payload that follows
 * it; prints the grid of every channel (one line per channel and frame) in the format of the instrumented FDK of docs/AAC_COMPLETENESS_PLAN.md, so the two can be diffed.  usage: sauti_eld_sbr_trace PREFIX  (build like tools/sauti_usac_raw.c) */
#include "sauti_aac_er.h"
#include "sauti_aac_sbr_payload.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t *slurp(const char *path, size_t *n) { FILE *f = fopen(path, "rb"); long len; uint8_t *b; if (!f) return NULL; fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET); b = (uint8_t *)malloc((size_t)len + 1u); if (fread(b, 1, (size_t)len, f) != (size_t)len) return NULL; fclose(f); *n = (size_t)len; return b; }
int main(int argc, char **argv) { (void)argc;
    char path[512]; size_t na, nu, p = 0; uint8_t *asc, *aus; SautiAacErAsc a; SautiAacErConfig cfg; static SautiAacErState st; static float pcm[2048]; SautiAacSbrPayloadLdState ls; unsigned frame = 0;
    snprintf(path, sizeof path, "%s.asc", argv[1]); asc = slurp(path, &na); snprintf(path, sizeof path, "%s.aus", argv[1]); aus = slurp(path, &nu);
    if (!asc || !aus) return 1;
    if (sauti_aac_er_asc_parse(asc, na, &a) != 1) { fprintf(stderr, "asc parse failed\n"); return 2; }
    fprintf(stderr, "aot %u rate_index %u chan %u frame480 %u ld_sbr %u down %u crc %u bits %u hdr: amp %u start %u stop %u xover %u\n", a.aot, a.rate_index, a.channel_config, a.frame480, a.ld_sbr, a.ld_sbr_downsampled, a.ld_sbr_crc, a.bits, a.ld_header.amp_res, a.ld_header.start_freq, a.ld_header.stop_freq, a.ld_header.xover_band);
    cfg.aot = a.aot; cfg.rate_index = a.rate_index; cfg.frame480 = a.frame480; cfg.channel_config = a.channel_config;
    sauti_aac_er_state_init(&st);
    memset(&ls, 0, sizeof ls); ls.have_header = 1; ls.header = a.ld_header; ls.time_slots = a.frame480 ? 15u : 16u; ls.crc = a.ld_sbr_crc;
    while (p + 4 <= nu) {
        size_t n = ((size_t)aus[p] << 24) | ((size_t)aus[p + 1] << 16) | ((size_t)aus[p + 2] << 8) | aus[p + 3], end = 0; SautiAacReason why; SautiAacSbrPayloadFrame fr[2]; unsigned cpl = 0, c;
        int r = sauti_aac_er_decode_ex(&cfg, &st, aus + p + 4, n, pcm, &why, &end);
        if (r != 1) { fprintf(stderr, "frame %u core failed %d\n", frame, r); return 3; }
        r = sauti_aac_sbr_payload_ld_parse(aus + p + 4, n, end, n * 8u, a.ld_sbr_downsampled ? 2u * 48000u : 48000u, a.channel_config, &ls, fr, &cpl);
        if (!r) { fprintf(stderr, "frame %u sbr parse failed (core end bit %zu of %zu)\n", frame, end, n * 8u); return 4; }
        for (c = 0; c < a.channel_config; c++) { unsigned u; printf("OSBR n%u cpl %u c%u env %u:", a.channel_config, cpl, c, fr[c].grid.env_count); for (u = 0; u <= fr[c].grid.env_count; u++) printf("%u ", fr[c].grid.env_border[u]); printf(" fr"); for (u = 0; u < fr[c].grid.env_count; u++) printf("%u", fr[c].grid.freq_res[u]); printf(" noise %u:", fr[c].grid.noise_count); for (u = 0; u <= fr[c].grid.noise_count; u++) printf("%u ", fr[c].grid.noise_border[u]); printf("\n"); }
        p += 4 + n; frame++;
    }
    fprintf(stderr, "%u frames ok\n", frame);
    return 0;
}
