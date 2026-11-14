#!/usr/bin/env python3
"""SBR-55: patch an already disposable SBR-54w FFmpeg checkout for QMF stages.

The patch is diagnostic-only and requires the prior SBR-54w instrumentation:
it writes 64 float values immediately after sum64x5 (pre-shuffle) and after
the MDCT (before post-shuffle), but only for active shared-PCM frames.  It
never changes Sauti or an installed FFmpeg.  Use a throwaway checkout only.
"""
import argparse
from pathlib import Path

MARKER = "/* SBR-55 QMF stage trace: diagnostic only. */"
PRE_ANCHOR = "        sbrdsp->sum64x5(z);"
POST_ANCHOR = "        mdct_fn(mdct, z, z + 64, sizeof(INTFLOAT));"
PRE = '''#if !USE_FIXED
    /* SBR-55 QMF stage trace: diagnostic only. */
    if (sbr54w_active) {
        static FILE *sbr55_pre_file;
        const char *sbr55_pre_path = getenv("SAUTI_SBR55_QMF_PRE");
        const char *sbr55_post_path = getenv("SAUTI_SBR55_QMF_POST");
        if (!!sbr55_pre_path != !!sbr55_post_path) {
            fprintf(stderr, "SBR-55: both QMF stage paths are required\\n");
            abort();
        }
        if (sbr55_pre_path) {
            if (!sbr55_pre_file) {
                sbr55_pre_file = fopen(sbr55_pre_path, "wb");
                if (!sbr55_pre_file) { perror(sbr55_pre_path); abort(); }
            }
            if (fwrite(z, sizeof(float), 64, sbr55_pre_file) != 64 ||
                fflush(sbr55_pre_file) != 0) {
                fprintf(stderr, "SBR-55: pre-shuffle trace write failure\\n");
                abort();
            }
        }
    }
#endif
'''
POST = '''#if !USE_FIXED
    if (sbr54w_active) {
        static FILE *sbr55_post_file;
        const char *sbr55_pre_path = getenv("SAUTI_SBR55_QMF_PRE");
        const char *sbr55_post_path = getenv("SAUTI_SBR55_QMF_POST");
        if (!!sbr55_pre_path != !!sbr55_post_path) {
            fprintf(stderr, "SBR-55: both QMF stage paths are required\\n");
            abort();
        }
        if (sbr55_post_path) {
            if (!sbr55_post_file) {
                sbr55_post_file = fopen(sbr55_post_path, "wb");
                if (!sbr55_post_file) { perror(sbr55_post_path); abort(); }
            }
            if (fwrite(z, sizeof(float), 64, sbr55_post_file) != 64 ||
                fflush(sbr55_post_file) != 0) {
                fprintf(stderr, "SBR-55: post-MDCT trace write failure\\n");
                abort();
            }
        }
    }
#endif
'''


def patch(text):
    if MARKER in text:
        raise ValueError("FFmpeg source already has SBR-55 stage trace")
    if "SBR-54w instrumented FFmpeg float QMF" not in text:
        raise ValueError("requires the existing disposable SBR-54w instrumentation")
    if text.count(PRE_ANCHOR) != 1 or text.count(POST_ANCHOR) != 1:
        raise ValueError("unsupported FFmpeg QMF stage anchors")
    if "sbr54w_active" not in text or "#if !USE_FIXED" not in text:
        raise ValueError("missing SBR-54w float active-frame guard")
    text = text.replace(PRE_ANCHOR, PRE_ANCHOR + "\n" + PRE)
    text = text.replace(POST_ANCHOR, POST_ANCHOR + "\n" + POST)
    return text


def self_test():
    source = """/* SBR-54w instrumented FFmpeg float QMF */
#if !USE_FIXED
int sbr54w_active;
#endif
        sbrdsp->sum64x5(z);
        mdct_fn(mdct, z, z + 64, sizeof(INTFLOAT));
"""
    updated = patch(source)
    assert updated.count(MARKER) == 1
    assert updated.count("fwrite(z, sizeof(float), 64") == 2
    try:
        patch(updated)
    except ValueError:
        pass
    else:
        raise AssertionError("duplicate stage patch accepted")
    print("PASS SBR-55 FFmpeg stage trace anchors (synthetic)")


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
        original = path.read_text(encoding="utf-8")
        updated = patch(original)
        if not args.dry_run:
            path.write_text(updated, encoding="utf-8")
    except (OSError, ValueError) as exc:
        parser.exit(2, f"SBR-55 FFmpeg stage trace rejected: {exc}\\n")
    print("SBR-55 FFmpeg stage trace anchors PASS" if args.dry_run
          else f"SBR-55 instrumented {path}")


if __name__ == "__main__":
    main()
