/* Test tool (not a product): decodes the access units of an AAC SSR (object type 3) mono / stereo stream with the lcn decoder to float PCM.
 * usage: sauti_ssr_probe IN.aus RATE_INDEX CHANNELS OUT.f32    (IN.aus: per access unit a 4-byte big-endian length and the bytes, tools/mp4_dump_aus.py) */
#include "sauti_aac_lcn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    SautiAacLcnConfig cfg; SautiAacLcnState *st; FILE *in, *out; unsigned char len4[4]; unsigned long frames = 0, fail = 0, unsup = 0;
    if (argc < 5) { fprintf(stderr, "usage: %s IN.aus RATE_INDEX CHANNELS OUT.f32\n", argv[0]); return 2; }
    memset(&cfg, 0, sizeof cfg); cfg.rate_index = (unsigned)atoi(argv[2]); cfg.channel_config = (unsigned)atoi(argv[3]); cfg.ssr = 1;
    st = (SautiAacLcnState *)calloc(1, sizeof *st); sauti_aac_lcn_state_init(st);
    in = fopen(argv[1], "rb"); out = fopen(argv[4], "wb"); if (!in || !out) { perror("open"); return 1; }
    while (fread(len4, 1, 4, in) == 4) {
        unsigned long n = ((unsigned long)len4[0] << 24) | ((unsigned long)len4[1] << 16) | ((unsigned long)len4[2] << 8) | len4[3];
        unsigned char *au = (unsigned char *)malloc(n ? n : 1); float *pcm = (float *)malloc(sizeof(float) * 1024 * 2); SautiAacReason why = SAUTI_AAC_REASON_OK; int r;
        if (fread(au, 1, n, in) != n) { free(au); free(pcm); break; }
        r = sauti_aac_lcn_decode(&cfg, st, au, n, pcm, &why);
        if (r == 1) fwrite(pcm, sizeof(float), 1024u * cfg.channel_config, out);
        else { if (r == -1) unsup++; else fail++; if (fail + unsup <= 3) fprintf(stderr, "frame %lu: %s (%s)\n", frames, r == -1 ? "unsupported" : "malformed", sauti_aac_reason_name(why)); }
        frames++; free(au); free(pcm);
    }
    fclose(in); fclose(out); fprintf(stderr, "%lu frames, %lu malformed, %lu unsupported\n", frames, fail, unsup);
    return 0;
}
