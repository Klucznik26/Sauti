/* Test tool: decodes PREFIX.asc + PREFIX.aus (the pair of tools/sauti_usac_gen.c) with the USAC decoder of the library into raw little-endian f32 (interleaved).
 * usage: sauti_usac_raw PREFIX OUT.f32 */
#include "sauti_usac.h"
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
int main(int argc, char **argv) {
    char path[1024]; size_t na, nu, p = 0, bit = 0, frames = 0; uint8_t *asc, *aus; SautiUsacConfig cfg; SautiUsacState *st; float *pcm; FILE *out; unsigned type; int r;
    if (argc < 3) { fprintf(stderr, "usage: sauti_usac_raw PREFIX OUT.f32\n"); return 2; }
    snprintf(path, sizeof path, "%s.asc", argv[1]); asc = slurp(path, &na);
    snprintf(path, sizeof path, "%s.aus", argv[1]); aus = slurp(path, &nu);
    if (!asc || !aus) { fprintf(stderr, "cannot read input\n"); return 1; }
    type = (unsigned)(asc[0] >> 3); bit = 5;
    if (type == 31) { type = 32u + (unsigned)((asc[0] & 7u) << 3 | asc[1] >> 5); bit = 11; }
    { const unsigned sfi = (asc[bit >> 3] >> (7 - (bit & 7))) & 1u; (void)sfi; }
    bit += 4; bit += 4;                                                    /* samplingFrequencyIndex, channelConfiguration (an explicit rate is not generated) */
    if (type != 42) { fprintf(stderr, "not a USAC AudioSpecificConfig (object type %u)\n", type); return 1; }
    r = sauti_usac_config_parse(asc, na, bit, &cfg);
    if (r != 1) { fprintf(stderr, "config parse: %d\n", r); return 1; }
    st = (SautiUsacState *)malloc(sizeof *st); pcm = (float *)malloc(sizeof(float) * 8192u * 8u);
    sauti_usac_state_init(st, &cfg);
    out = fopen(argv[2], "wb");
    while (p + 4 <= nu) {
        SautiAacReason why;
        const size_t n = ((size_t)aus[p] << 24) | ((size_t)aus[p + 1] << 16) | ((size_t)aus[p + 2] << 8) | aus[p + 3];
        r = sauti_usac_decode(&cfg, st, aus + p + 4, n, pcm, &why);
        if (r == -1 && getenv("SAUTI_RAW_CONTINUE")) { p += 4 + n; frames++; continue; }       /* diagnostics: step over frames of unsupported tools (the state is what the decoder keeps) */
        if (r != 1) { fprintf(stderr, "frame %zu: result %d reason %d\n", frames, r, (int)why); return 1; }
        fwrite(pcm, sizeof(float), (size_t)cfg.out_frame_len * cfg.out_channels, out);
        p += 4 + n; frames++;
    }
    fclose(out);
    fprintf(stderr, "%zu frames, %u ch, %u Hz, %u samples per frame\n", frames, cfg.out_channels, cfg.sample_rate, cfg.out_frame_len);
    return 0;
}
