#!/usr/bin/env python3
"""SBR-55: trace only the five FFmpeg products summed into QMF z[0]."""
import argparse
from pathlib import Path

MARKER = "/* SBR-55 QMF branch-0 trace: diagnostic only. */"
ANCHOR = "        dsp->vector_fmul_reverse(z, sbr_qmf_window_ds, x, 320);"
INSERT = '''#if !USE_FIXED
    /* SBR-55 QMF branch-0 trace: diagnostic only. */
    if (sbr54w_active) {
        static FILE *sbr55_branch0_file;
        const char *sbr55_branch0_path = getenv("SAUTI_SBR55_QMF_BRANCH0");
        if (sbr55_branch0_path) {
            const float sbr55_branch0[5] = { z[0], z[64], z[128], z[192], z[256] };
            if (!sbr55_branch0_file) {
                sbr55_branch0_file = fopen(sbr55_branch0_path, "wb");
                if (!sbr55_branch0_file) { perror(sbr55_branch0_path); abort(); }
            }
            if (fwrite(sbr55_branch0, sizeof(float), 5, sbr55_branch0_file) != 5 ||
                fflush(sbr55_branch0_file) != 0) {
                fprintf(stderr, "SBR-55: branch-0 trace write failure\\n");
                abort();
            }
        }
    }
#endif
'''


def patch(text):
    if MARKER in text:
        raise ValueError("FFmpeg source already has SBR-55 branch-0 trace")
    if "SBR-55 QMF stage trace: diagnostic only." not in text:
        raise ValueError("requires the existing disposable SBR-55 stage instrumentation")
    if text.count(ANCHOR) != 1 or "sbr54w_active" not in text:
        raise ValueError("unsupported FFmpeg QMF product anchor")
    return text.replace(ANCHOR, ANCHOR + "\n" + INSERT)


def self_test():
    source = """/* SBR-55 QMF stage trace: diagnostic only. */
int sbr54w_active;
        dsp->vector_fmul_reverse(z, sbr_qmf_window_ds, x, 320);
"""
    updated = patch(source)
    assert updated.count(MARKER) == 1 and "z[256]" in updated
    try:
        patch(updated)
    except ValueError:
        pass
    else:
        raise AssertionError("duplicate branch-0 patch accepted")
    print("PASS SBR-55 FFmpeg branch-0 trace anchors (synthetic)")


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
        text = path.read_text(encoding="utf-8")
        updated = patch(text)
        if not args.dry_run:
            path.write_text(updated, encoding="utf-8")
    except (OSError, ValueError) as exc:
        parser.exit(2, f"SBR-55 FFmpeg branch-0 trace rejected: {exc}\n")
    print("SBR-55 FFmpeg branch-0 trace anchors PASS" if args.dry_run
          else f"SBR-55 instrumented {path}")


if __name__ == "__main__":
    main()
