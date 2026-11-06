#!/usr/bin/env python3
"""SBR-58: trace FFmpeg's deinterleaved full-rate QMF synthesis V blocks."""
import argparse
from pathlib import Path

MARKER = "/* SBR-58 QMF synthesis V trace: diagnostic only. */"
PRE_ANCHOR = "    INTFLOAT *v;"
CAPTURE_ANCHOR = "        dsp->vector_fmul    (out, v                , sbr_qmf_window"
TAIL = "        out += 64 >> div;\n    }"
PRE = r'''#if !USE_FIXED
    /* SBR-58 QMF synthesis V trace: diagnostic only. */
    const char *sbr58_v_path = getenv("SAUTI_SBR58_SYNTH_V");
    static FILE *sbr58_v_file;
    float sbr58_v[32][128];
    if (sbr58_v_path) {
        if (div || numTimeSlots != 16 || sizeof(INTFLOAT) != sizeof(float)) {
            fprintf(stderr, "SBR-58: require full-rate 32-slot float synthesis\n");
            abort();
        }
        if (!sbr58_v_file) {
            sbr58_v_file = fopen(sbr58_v_path, "wb");
            if (!sbr58_v_file) { perror("SBR-58 trace"); abort(); }
        }
    }
#endif
'''
CAPTURE = r'''#if !USE_FIXED
        if (sbr58_v_path)
            memcpy(sbr58_v[i], v, sizeof(sbr58_v[i]));
#endif
'''
POST = r'''#if !USE_FIXED
    if (sbr58_v_path) {
        if (fwrite(sbr58_v, sizeof(float), 32 * 128, sbr58_v_file) != 32 * 128 ||
            fflush(sbr58_v_file) != 0) {
            fprintf(stderr, "SBR-58: V trace write failure\n");
            abort();
        }
    }
#endif'''


def patch(text):
    if MARKER in text:
        raise ValueError("FFmpeg source already has SBR-58 synthesis V trace")
    if "SBR-57 QMF synthesis trace: diagnostic only." not in text:
        raise ValueError("requires the existing disposable SBR-57 instrumentation")
    if (text.count(PRE_ANCHOR) != 1 or text.count(CAPTURE_ANCHOR) != 1 or
            text.count(TAIL) != 1):
        raise ValueError("unsupported FFmpeg synthesis anchors")
    text = text.replace(PRE_ANCHOR, PRE_ANCHOR + "\n" + PRE, 1)
    text = text.replace(CAPTURE_ANCHOR, CAPTURE + CAPTURE_ANCHOR, 1)
    return text.replace(TAIL, TAIL + "\n" + POST, 1)


def self_test():
    source = "\n".join((
        "/* SBR-57 QMF synthesis trace: diagnostic only. */",
        PRE_ANCHOR, CAPTURE_ANCHOR, TAIL))
    updated = patch(source)
    assert updated.count(MARKER) == 1
    assert "memcpy(sbr58_v[i], v" in updated and "fwrite(sbr58_v" in updated
    try:
        patch(updated)
    except ValueError:
        pass
    else:
        raise AssertionError("duplicate V trace accepted")
    print("PASS SBR-58 FFmpeg synthesis V trace anchors (synthetic)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ffmpeg_checkout", nargs="?")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.ffmpeg_checkout:
        parser.error("ffmpeg_checkout is required unless --self-test")
    path = Path(args.ffmpeg_checkout) / "libavcodec" / "aacsbr_template.c"
    try:
        source = path.read_text(encoding="utf-8")
        updated = patch(source)
        if not args.dry_run:
            path.write_text(updated, encoding="utf-8")
    except (OSError, ValueError) as exc:
        parser.exit(2, f"SBR-58 FFmpeg synthesis V trace rejected: {exc}\n")
    print("SBR-58 FFmpeg synthesis V trace anchors PASS" if args.dry_run
          else f"SBR-58 instrumented {path}")


if __name__ == "__main__":
    main()
