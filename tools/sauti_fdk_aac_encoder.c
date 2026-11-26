/* Test-fixture tool (not a product): drives the Fraunhofer FDK AAC encoder (libfdk-aac, built OUTSIDE the repository) on a raw 16-bit PCM file and writes the access units and the AudioSpecificConfig.
 * usage: sauti_fdk_aac_encoder IN.s16le RATE CHANNELS AOT BITRATE GRANULE OUT.aus OUT.asc     (environment: SBRMODE, SBRRATIO = AACENC_SBR_MODE / AACENC_SBR_RATIO: ratio 1 = single rate, 2 = dual rate of the low-delay SBR)
 * OUT.aus: every access unit as a 4-byte big-endian length and its bytes (the input of tools/mux_raw_aus_mp4.py); OUT.asc: the AudioSpecificConfig.
 * build:   cc -O1 -I$FDK/include/fdk-aac -o build/sauti_fdk_aac_encoder tools/sauti_fdk_aac_encoder.c -L$FDK/lib -lfdk-aac -Wl,-rpath,$FDK/lib -lm */
#include "aacenc_lib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    if (argc < 9) { fprintf(stderr, "usage: %s IN.s16le RATE CHANNELS AOT BITRATE GRANULE OUT.aus OUT.asc\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "rb"), *o = fopen(argv[7], "wb"), *c = fopen(argv[8], "wb");
    const int rate = atoi(argv[2]), ch = atoi(argv[3]), aot = atoi(argv[4]), br = atoi(argv[5]), gran = atoi(argv[6]);
    HANDLE_AACENCODER h; static short in[2048 * 8 * 4]; UCHAR out[20480]; AACENC_InfoStruct info;
    if (!f || !o || !c || ch < 1 || ch > 8) return 2;
    if (aacEncOpen(&h, 0, (UINT)ch) != AACENC_OK) return 2;
    if (aacEncoder_SetParam(h, AACENC_AOT, (UINT)aot) != AACENC_OK) { fprintf(stderr, "aot\n"); return 3; }
    aacEncoder_SetParam(h, AACENC_SAMPLERATE, (UINT)rate); aacEncoder_SetParam(h, AACENC_CHANNELMODE, getenv("CHMODE") ? (CHANNEL_MODE)atoi(getenv("CHMODE")) : (ch == 1 ? MODE_1 : ch == 2 ? MODE_2 : ch == 6 ? MODE_1_2_2_1 : MODE_1_2_2));
    aacEncoder_SetParam(h, AACENC_BITRATE, (UINT)br);
    if (aacEncoder_SetParam(h, AACENC_GRANULE_LENGTH, (UINT)gran) != AACENC_OK) { fprintf(stderr, "granule\n"); return 4; }
    if (getenv("SBRMODE")) aacEncoder_SetParam(h, AACENC_SBR_MODE, (UINT)atoi(getenv("SBRMODE")));
    if (getenv("SBRRATIO")) aacEncoder_SetParam(h, AACENC_SBR_RATIO, (UINT)atoi(getenv("SBRRATIO")));
    aacEncoder_SetParam(h, AACENC_TRANSMUX, 0); aacEncoder_SetParam(h, AACENC_AFTERBURNER, 1);
    if (aacEncEncode(h, NULL, NULL, NULL, NULL) != AACENC_OK) { fprintf(stderr, "init failed\n"); return 5; }
    aacEncInfo(h, &info); fprintf(stderr, "frameLength %u confSize %u\n", info.frameLength, info.confSize);
    fwrite(info.confBuf, 1, info.confSize, c); fclose(c);
    for (;;) {
        size_t n = fread(in, 2, (size_t)info.frameLength * (size_t)ch, f); int inid = IN_AUDIO_DATA, ins = (int)(n * 2), insize = 2, oid = OUT_BITSTREAM_DATA, osz = sizeof out, oes = 1;
        void *ib[1] = { in }, *ob[1] = { out }; AACENC_BufDesc ibd = { 0 }, obd = { 0 }; AACENC_InArgs ia = { 0 }; AACENC_OutArgs oa = { 0 }; AACENC_ERROR e;
        if (n < (size_t)info.frameLength * (size_t)ch) memset(in + n, 0, ((size_t)info.frameLength * (size_t)ch - n) * 2);
        ibd.numBufs = 1; ibd.bufs = ib; ibd.bufferIdentifiers = &inid; ibd.bufSizes = &ins; ibd.bufElSizes = &insize;
        obd.numBufs = 1; obd.bufs = ob; obd.bufferIdentifiers = &oid; obd.bufSizes = &osz; obd.bufElSizes = &oes;
        ia.numInSamples = n ? (int)(info.frameLength * (UINT)ch) : -1;
        e = aacEncEncode(h, n ? &ibd : NULL, &obd, &ia, &oa);
        if (e == AACENC_ENCODE_EOF) break;
        if (e != AACENC_OK) { fprintf(stderr, "enc err %d\n", (int)e); return 6; }
        if (oa.numOutBytes) { const unsigned l = (unsigned)oa.numOutBytes; const unsigned char b4[4] = { (unsigned char)(l >> 24), (unsigned char)(l >> 16), (unsigned char)(l >> 8), (unsigned char)l }; fwrite(b4, 1, 4, o); fwrite(out, 1, oa.numOutBytes, o); }
        if (!n) break;
    }
    fclose(o); fclose(f); aacEncClose(&h); return 0;
}
