#!/usr/bin/env python3
"""SBR-54w III: instrument an isolated FFmpeg source checkout, never Sauti.

This tool patches ONLY libavcodec/aacsbr_template.c in the supplied
FFmpeg checkout. It injects the same mono 1024-float core PCM into the
FLOAT sbr_qmf_analysis() entry point and captures its complex W bins
before HF generation. Requires FFmpeg source with expected anchors.
Use a disposable clone: no system libraries or installed FFmpeg modified.
No reference claim until compiled, run, and compared on real shared PCM.
"""
import argparse
from pathlib import Path

MARKER = "/* SBR-54w instrumented FFmpeg float QMF: diagnostic only. */"
ANCHOR = "memcpy(x+288, in, nb*sizeof(x[0]));"
TAIL = "        x += 32;"

INJECTION_BEGIN = """#if !USE_FIXED
    /* SBR-54w: override the *analysis* input from shared core f32le,
     * preserving FFmpeg's own QMF filter history, window and modulation.
     * Mono, one invocation per 1024-sample frame, float build only.
     * Abort loudly when frame count, storage or I/O is inconsistent.
     * DO NOT use this instrumented binary for normal playback. */
    static FILE *sbr54w_core_file;
    static FILE *sbr54w_qmf_file;
    static unsigned long long sbr54w_frame;
    static float sbr54w_tail[6][4][2];
    float sbr54w_pcm[1024];
    const char *sbr54w_core_path = getenv("SAUTI_SBR54W_CORE");
    const char *sbr54w_qmf_path = getenv("SAUTI_SBR54W_QMF");
    if (!!sbr54w_core_path != !!sbr54w_qmf_path) {
        fprintf(stderr, "SBR-54w: both env paths are required\\n");
        abort();
    }
    if (sbr54w_core_path) {
        if (nb != 1024 || sizeof(INTFLOAT) != sizeof(float)) {
            fprintf(stderr, "SBR-54w: require 1024-sample float core\\n");
            abort();
        }
        if (!sbr54w_core_file) {
            sbr54w_core_file = fopen(sbr54w_core_path, "rb");
            if (!sbr54w_core_file) { perror(sbr54w_core_path); abort(); }
            sbr54w_qmf_file = fopen(sbr54w_qmf_path, "wb");
            if (!sbr54w_qmf_file) { perror(sbr54w_qmf_path); abort(); }
        }
        if (fread(sbr54w_pcm, sizeof(float), 1024, sbr54w_core_file) != 1024) {
            fprintf(stderr, "SBR-54w: core PCM exhausted or truncated\\n");
            abort();
        }
        for (int sbr54w_i = 0; sbr54w_i < 1024; sbr54w_i++)
            if (!isfinite(sbr54w_pcm[sbr54w_i])) {
                fprintf(stderr, "SBR-54w: nonfinite core PCM\\n");
                abort();
            }
        in = sbr54w_pcm;
    }
#endif
"""
INJECTION_END = """#if !USE_FIXED
    if (sbr54w_core_path) {
        float sbr54w_record[32][4][2] = {0};
        for (int sbr54w_slot = 0; sbr54w_slot < 32; sbr54w_slot++)
            for (int sbr54w_band = 0; sbr54w_band < 4; sbr54w_band++) {
                int src_slot = sbr54w_slot - 6;
                const INTFLOAT *v;
                if (src_slot >= 0)
                    v = W[buf_idx][src_slot][sbr54w_band + 2];
                else if (sbr54w_frame)
                    v = sbr54w_tail[6 + src_slot][sbr54w_band];
                else
                    continue;
                sbr54w_record[sbr54w_slot][sbr54w_band][0] = v[0];
                sbr54w_record[sbr54w_slot][sbr54w_band][1] = v[1];
            }
        for (int sbr54w_slot = 0; sbr54w_slot < 6; sbr54w_slot++)
            for (int sbr54w_band = 0; sbr54w_band < 4; sbr54w_band++) {
                sbr54w_tail[sbr54w_slot][sbr54w_band][0] =
                    W[buf_idx][26 + sbr54w_slot][sbr54w_band + 2][0];
                sbr54w_tail[sbr54w_slot][sbr54w_band][1] =
                    W[buf_idx][26 + sbr54w_slot][sbr54w_band + 2][1];
            }
        if (fwrite(sbr54w_record, sizeof(float), 32 * 4 * 2,
                   sbr54w_qmf_file) != 32 * 4 * 2 ||
            fflush(sbr54w_qmf_file) != 0) {
            fprintf(stderr, "SBR-54w: QMF export write failure\\n");
            abort();
        }
        sbr54w_frame++;
    }
#endif
"""


def patch(text):
    if MARKER in text:
        raise ValueError("source already instrumented")
    if text.count(ANCHOR) != 1:
        raise ValueError("expected exactly one FFmpeg QMF input anchor")
    if text.count("sbrdsp->qmf_post_shuffle(W[buf_idx][i], z);") != 1:
        raise ValueError("unsupported FFmpeg QMF output layout")
    start = text.index("static void sbr_qmf_analysis(")
    end = text.index("\n}\n#endif", start) + 2
    original = text[start:end]
    if original.count(TAIL) != 1 or "int nb = numTimeSlots * 64;" not in original:
        raise ValueError("unsupported FFmpeg QMF routine; stop")
    # Include standard headers at the file start, rather than changing an
    # unrelated FFmpeg function signature or linking an external helper.
    text = "#include <stdio.h>\n#include <stdlib.h>\n#include <math.h>\n" + MARKER + "\n" + text
    start = text.index("static void sbr_qmf_analysis(")
    end = text.index("\n}\n#endif", start) + 2
    section = text[start:end]
    section = section.replace(ANCHOR, INJECTION_BEGIN + "    " + ANCHOR)
    # The closing hook runs after W generation and before function return.
    closing = "        x += 32;\n    }\n}"
    if section.count(closing) != 1:
        raise ValueError("unrecognized loop closing; stop")
    section = section.replace(closing, "        x += 32;\n    }\n" + INJECTION_END + "}")
    return text[:start] + section + text[end:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ffmpeg_checkout", nargs="?", help="disposable FFmpeg source root")
    parser.add_argument("--dry-run", action="store_true")\n    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    file = Path(args.ffmpeg_checkout) / "libavcodec" / "aacsbr_template.c"
    try:
        before = file.read_text(encoding="utf-8")
        after = patch(before)
        if not args.dry_run:
            file.write_text(after, encoding="utf-8")
    except (OSError, ValueError) as exc:
        parser.exit(2, f"SBR-54w patch rejected: {exc}\n")
    print("SBR-54w: FFmpeg patch anchors PASS" if args.dry_run
          else f"SBR-54w: instrumented {file}")


if __name__ == "__main__":
    main()
