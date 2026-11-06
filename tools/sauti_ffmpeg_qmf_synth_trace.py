#!/usr/bin/env python3
"""SBR-57: trace raw 64-band QMF synthesis input/output in disposable FFmpeg."""
import argparse
from pathlib import Path

MARKER = "/* SBR-57 QMF synthesis trace: diagnostic only. */"
ANCHOR = "    const int step = 128 >> div;"
TAIL = "        out += 64 >> div;\n    }"
LEGACY_LOG_NEWLINE = r"div=%u\\n"
CORRECT_LOG_NEWLINE = r"div=%u\n"
INSERT = r'''#if !USE_FIXED
    /* SBR-57 QMF synthesis trace: diagnostic only. */
    const char *sbr57_x_path = getenv("SAUTI_SBR57_SYNTH_X");
    const char *sbr57_pcm_path = getenv("SAUTI_SBR57_SYNTH_PCM");
    static FILE *sbr57_x_file;
    static FILE *sbr57_pcm_file;
    static unsigned long long sbr57_call;
    INTFLOAT *const sbr57_out_start = out;
    if (!!sbr57_x_path != !!sbr57_pcm_path) {
        fprintf(stderr, "SBR-57: both synthesis trace paths are required\n");
        abort();
    }
    if (sbr57_x_path) {
        float sbr57_x[32][64][2];
        if (div || numTimeSlots != 16 || sizeof(INTFLOAT) != sizeof(float)) {
            fprintf(stderr, "SBR-57: require full-rate 32-slot float synthesis\n");
            abort();
        }
        if (!sbr57_x_file) {
            sbr57_x_file = fopen(sbr57_x_path, "wb");
            sbr57_pcm_file = fopen(sbr57_pcm_path, "wb");
            if (!sbr57_x_file || !sbr57_pcm_file) { perror("SBR-57 trace"); abort(); }
        }
        fprintf(stderr, "SBR-57: synthesis call=%llu v0=%p slots=%d div=%u\n",
                sbr57_call, (void *)v0, numTimeSlots, div);
        for (int sbr57_t = 0; sbr57_t < 32; sbr57_t++)
            for (int sbr57_k = 0; sbr57_k < 64; sbr57_k++) {
                sbr57_x[sbr57_t][sbr57_k][0] = X[0][sbr57_t][sbr57_k];
                sbr57_x[sbr57_t][sbr57_k][1] = X[1][sbr57_t][sbr57_k];
            }
        if (fwrite(sbr57_x, sizeof(float), 32 * 64 * 2, sbr57_x_file) != 32 * 64 * 2 ||
            fflush(sbr57_x_file) != 0) {
            fprintf(stderr, "SBR-57: QMF input trace write failure\n");
            abort();
        }
    }
#endif
'''
POST = r'''#if !USE_FIXED
    if (sbr57_x_path) {
        if (fwrite(sbr57_out_start, sizeof(float), 32 * 64, sbr57_pcm_file) != 32 * 64 ||
            fflush(sbr57_pcm_file) != 0) {
            fprintf(stderr, "SBR-57: PCM trace write failure\n");
            abort();
        }
        ++sbr57_call;
    }
#endif'''


def patch(text):
    if MARKER in text:
        raise ValueError("FFmpeg source already has SBR-57 synthesis trace")
    if "SBR-54w instrumented FFmpeg float QMF" not in text:
        raise ValueError("requires the existing disposable SBR-54w instrumentation")
    if text.count(ANCHOR) != 1 or text.count(TAIL) != 1:
        raise ValueError("unsupported FFmpeg synthesis anchors")
    return text.replace(ANCHOR, ANCHOR + "\n" + INSERT).replace(TAIL, TAIL + "\n" + POST, 1)


def repair_legacy_newline(text):
    if MARKER not in text:
        raise ValueError("requires existing SBR-57 instrumentation")
    if text.count(LEGACY_LOG_NEWLINE) != 1 or CORRECT_LOG_NEWLINE in text:
        raise ValueError("legacy SBR-57 log newline not found uniquely")
    return text.replace(LEGACY_LOG_NEWLINE, CORRECT_LOG_NEWLINE, 1)


def self_test():
    source = "/* SBR-54w instrumented FFmpeg float QMF */\n" + ANCHOR + "\n" + TAIL
    updated = patch(source)
    assert updated.count(MARKER) == 1 and "X[1][sbr57_t][sbr57_k]" in updated
    assert "sbr57_out_start" in updated
    assert r"div=%u\n" in updated and r"div=%u\\n" not in updated
    try:
        patch(updated)
    except ValueError:
        pass
    else:
        raise AssertionError("duplicate synthesis trace accepted")
    legacy = updated.replace(CORRECT_LOG_NEWLINE, LEGACY_LOG_NEWLINE, 1)
    assert repair_legacy_newline(legacy) == updated
    print("PASS SBR-57 FFmpeg synthesis trace anchors (synthetic)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ffmpeg_checkout", nargs="?")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--repair-legacy-newline", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.ffmpeg_checkout:
        parser.error("ffmpeg_checkout is required unless --self-test")
    path = Path(args.ffmpeg_checkout) / "libavcodec" / "aacsbr_template.c"
    try:
        source = path.read_text(encoding="utf-8")
        updated = repair_legacy_newline(source) if args.repair_legacy_newline else patch(source)
        if not args.dry_run:
            path.write_text(updated, encoding="utf-8")
    except (OSError, ValueError) as exc:
        parser.exit(2, f"SBR-57 FFmpeg synthesis trace rejected: {exc}\n")
    print("SBR-57 FFmpeg synthesis trace anchors PASS" if args.dry_run
          else f"SBR-57 instrumented {path}")


if __name__ == "__main__":
    main()
